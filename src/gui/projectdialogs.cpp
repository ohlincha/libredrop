#include "projectdialogs.h"

#include <QDate>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>
#include <cmath>

namespace {

QString cell(double v, int prec = 4) { return std::isfinite(v) ? QString::number(v, 'g', prec) : QString(); }
double value(QTableWidget* t, int r, int c) {
    const QTableWidgetItem* it = t->item(r, c);
    bool ok = false;
    const double v = it ? it->text().trimmed().replace(QLatin1Char(','), QLatin1Char('.')).toDouble(&ok) : 0;
    return ok ? v : ld::kNaN;
}
QString text(QTableWidget* t, int r, int c) { return t->item(r, c) ? t->item(r, c)->text().trimmed() : QString(); }

// A dialog around an editable table with Add/Remove rows; returns the table when accepted (nullptr otherwise).
struct TableDialog {
    QDialog d;
    QTableWidget* t;
    QHBoxLayout* extra;
    TableDialog(QWidget* parent, const QString& title, const QString& intro, const QStringList& head) : d(parent) {
        d.setWindowTitle(title);
        d.resize(1000, 460);
        auto* l = new QVBoxLayout(&d);
        auto* lab = new QLabel(intro);
        lab->setWordWrap(true);
        l->addWidget(lab);
        t = new QTableWidget(0, int(head.size()));
        t->setHorizontalHeaderLabels(head);
        t->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        t->horizontalHeader()->setStretchLastSection(true);
        l->addWidget(t, 1);
        auto* row = new QHBoxLayout;
        auto* add = new QPushButton(QObject::tr("Add row"));
        auto* rem = new QPushButton(QObject::tr("Remove row"));
        QObject::connect(add, &QPushButton::clicked, t, [this] { t->insertRow(t->rowCount()); });
        QObject::connect(rem, &QPushButton::clicked, t, [this] {
            if (t->currentRow() >= 0) t->removeRow(t->currentRow());
        });
        row->addWidget(add);
        row->addWidget(rem);
        extra = new QHBoxLayout;
        row->addLayout(extra);
        row->addStretch(1);
        auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        QObject::connect(bb, &QDialogButtonBox::accepted, &d, &QDialog::accept);
        QObject::connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
        row->addWidget(bb);
        l->addLayout(row);
    }
    void set(int r, int c, const QString& s) { t->setItem(r, c, new QTableWidgetItem(s)); }
};

}  // namespace

bool editLiquids(QWidget* parent, ld::Project& p) {
    TableDialog td(parent, QObject::tr("Liquids"),
                   QObject::tr("<p>The project's liquid data. Density and surface tension are used for the drop shape "
                               "(Bond number) and the pendant drop; viscosity and the dispersive/polar parts for later "
                               "analyses. Keep a source for every value; mark doubtful ones.</p>"),
                   {QObject::tr("Name"), QObject::tr("ρ (kg/m³)"), QObject::tr("γ (mN/m)"), QObject::tr("η (mPa·s)"),
                    QObject::tr("γ dispersive"), QObject::tr("γ polar"), QObject::tr("T (°C)"), QObject::tr("Source")});
    for (const ld::LiquidData& l : p.liquids) {
        const int r = td.t->rowCount();
        td.t->insertRow(r);
        td.set(r, 0, l.name), td.set(r, 1, cell(l.density)), td.set(r, 2, cell(l.tension)), td.set(r, 3, cell(l.viscosity));
        td.set(r, 4, cell(l.dispersive)), td.set(r, 5, cell(l.polar)), td.set(r, 6, cell(l.temperature)), td.set(r, 7, l.source);
        td.t->item(r, 7)->setToolTip(l.source);
    }
    if (td.d.exec() != QDialog::Accepted) return false;
    std::vector<ld::LiquidData> out;
    for (int r = 0; r < td.t->rowCount(); ++r) {
        ld::LiquidData l;
        l.name = text(td.t, r, 0);
        if (l.name.isEmpty()) continue;
        l.density = value(td.t, r, 1), l.tension = value(td.t, r, 2), l.viscosity = value(td.t, r, 3);
        l.dispersive = value(td.t, r, 4), l.polar = value(td.t, r, 5);
        const double T = value(td.t, r, 6);
        l.temperature = std::isfinite(T) ? T : 25;
        l.source = text(td.t, r, 7);
        out.push_back(l);
    }
    p.liquids = out;
    return true;
}

bool editCalibrations(QWidget* parent, ld::Project& p, int frameWidth, double scale, std::function<double()> measureNeedlePx) {
    TableDialog td(parent, QObject::tr("Calibration"),
                   QObject::tr("<p>Image scale per camera mode (frame width). Recordings use the newest entry for their "
                               "width. Volume, base width, sorptivity and surface tension depend on it; angles do not. "
                               "<i>From the needle</i>: enter the needle's outer diameter (measured with a caliper or "
                               "micrometer) and the needle is measured in the open recording.</p>"),
                   {QObject::tr("Frame width (px)"), QObject::tr("Scale (px/mm)"), QObject::tr("Needle (mm)"),
                    QObject::tr("Date"), QObject::tr("Method")});
    for (const ld::Calibration& c : p.calibrations) {
        const int r = td.t->rowCount();
        td.t->insertRow(r);
        td.set(r, 0, QString::number(c.frameWidth)), td.set(r, 1, cell(c.pxPerMm, 6)), td.set(r, 2, cell(c.needleMm));
        td.set(r, 3, c.date), td.set(r, 4, c.method);
    }
    auto* fromScale = new QPushButton(QObject::tr("Add current scale"));
    fromScale->setEnabled(frameWidth > 0 && scale > 0);
    fromScale->setToolTip(QObject::tr("The scale now set in the main window, for the open recording's camera mode"));
    QObject::connect(fromScale, &QPushButton::clicked, td.t, [&] {
        const int r = td.t->rowCount();
        td.t->insertRow(r);
        td.set(r, 0, QString::number(frameWidth)), td.set(r, 1, cell(scale, 6));
        td.set(r, 3, QDate::currentDate().toString(Qt::ISODate)), td.set(r, 4, QObject::tr("scale as set"));
    });
    auto* fromNeedle = new QPushButton(QObject::tr("From the needle…"));
    fromNeedle->setEnabled(frameWidth > 0 && bool(measureNeedlePx));
    QObject::connect(fromNeedle, &QPushButton::clicked, td.t, [&] {
        const double px = measureNeedlePx();
        if (!std::isfinite(px)) {
            QMessageBox::information(&td.d, QObject::tr("Calibration"),
                                     QObject::tr("The needle could not be measured in this frame: open a recording, move to a "
                                                 "frame where the needle comes down from the top edge, and try again."));
            return;
        }
        bool ok = false;
        const double mm = QInputDialog::getDouble(&td.d, QObject::tr("Needle diameter"),
                                                  QObject::tr("The needle is %1 px wide here. Its outer diameter (mm):").arg(px, 0, 'f', 1),
                                                  0.5, 0.05, 5.0, 3, &ok);
        if (!ok) return;
        const int r = td.t->rowCount();
        td.t->insertRow(r);
        td.set(r, 0, QString::number(frameWidth)), td.set(r, 1, cell(px / mm, 6)), td.set(r, 2, cell(mm));
        td.set(r, 3, QDate::currentDate().toString(Qt::ISODate));
        td.set(r, 4, QObject::tr("needle %1 px").arg(px, 0, 'f', 1));
    });
    td.extra->addWidget(fromScale);
    td.extra->addWidget(fromNeedle);
    if (td.d.exec() != QDialog::Accepted) return false;
    std::vector<ld::Calibration> out;
    for (int r = 0; r < td.t->rowCount(); ++r) {
        ld::Calibration c;
        c.frameWidth = text(td.t, r, 0).toInt();
        c.pxPerMm = value(td.t, r, 1);
        if (c.frameWidth <= 0 || !std::isfinite(c.pxPerMm)) continue;
        c.needleMm = value(td.t, r, 2), c.date = text(td.t, r, 3), c.method = text(td.t, r, 4);
        out.push_back(c);
    }
    p.calibrations = out;
    return true;
}
