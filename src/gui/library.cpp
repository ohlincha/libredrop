#include "library.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <cmath>
#include <set>

namespace {
enum Kind { SampleItem = 1, RecordingItem = 2 };
const QString kNoSample = QObject::tr("(no sample yet)");
}

LibraryPanel::LibraryPanel(QWidget* parent) : QWidget(parent) {
    title_ = new QLabel;
    title_->setWordWrap(true);
    tree_ = new QTreeWidget;
    tree_->setHeaderLabels({tr("Sample / recording"), tr("Liquid"), tr("Date")});
    tree_->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    tree_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    tree_->setToolTip(tr("<p>Double-click a recording to open it. Select samples or recordings and press Compare. "
                         "★ marks a control sample, ◆ a glass (evaporation) reference.</p>"));
    auto* add = new QPushButton(tr("Add…"));
    add->setToolTip(tr("Add recordings to the project (their capture .json files fill in sample and liquid)"));
    auto* editB = new QPushButton(tr("Edit…"));
    auto* cmp = new QPushButton(tr("Compare"));
    cmp->setObjectName(QStringLiteral("compare"));
    auto* row = new QHBoxLayout;
    row->addWidget(add);
    row->addWidget(editB);
    row->addStretch(1);
    row->addWidget(cmp);
    auto* l = new QVBoxLayout(this);
    l->setContentsMargins(4, 4, 4, 4);
    l->addWidget(title_);
    l->addLayout(row);
    l->addWidget(tree_, 1);
    connect(add, &QPushButton::clicked, this, [this] {
        if (!project_) return;
        addRecordings(QFileDialog::getOpenFileNames(this, tr("Add recordings"), QString(),
                                                    tr("Videos (*.mkv *.avi *.mp4 *.mov);;All files (*)")));
    });
    connect(editB, &QPushButton::clicked, this, [this] { edit(tree_->currentItem()); });
    connect(cmp, &QPushButton::clicked, this, &LibraryPanel::compareSelected);
    connect(tree_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* it) {
        if (it->data(0, Qt::UserRole).toInt() == RecordingItem)
            emit openRecording(project_->absolute(it->data(0, Qt::UserRole + 1).toString()));
        else
            edit(it);
    });
    refresh();
}

void LibraryPanel::setProject(std::optional<ld::Project> p) {
    project_ = std::move(p);
    refresh();
}

bool LibraryPanel::save() {
    QString err;
    if (project_ && !project_->save(&err)) {
        QMessageBox::warning(this, tr("Project"), err);
        return false;
    }
    refresh();
    return true;
}

ld::Recording* LibraryPanel::recordingFor(const QString& abs) {
    if (!project_) return nullptr;
    const QString c = QFileInfo(abs).canonicalFilePath();
    for (ld::Recording& r : project_->recordings)
        if (QFileInfo(project_->absolute(r.file)).canonicalFilePath() == c) return &r;
    return nullptr;
}

void LibraryPanel::refresh() {
    tree_->clear();
    if (!project_) {
        title_->setText(tr("No project open. <b>Project ▸ New project…</b> makes a folder for a study: samples, "
                           "recordings, liquids and references in one place."));
        setEnabled(true);
        return;
    }
    title_->setText(tr("<b>%1</b><br><small>%2</small>").arg(project_->name.toHtmlEscaped(), project_->dir.toHtmlEscaped()));
    std::vector<QString> names;
    for (const ld::Sample& s : project_->samples) names.push_back(s.name);
    bool unassigned = false;
    for (const ld::Recording& r : project_->recordings) unassigned |= r.sample.isEmpty() || !project_->sample(r.sample);
    if (unassigned) names.push_back(QString());
    for (const QString& n : names) {
        auto* top = new QTreeWidgetItem(tree_);
        const ld::Sample* s = n.isEmpty() ? nullptr : project_->sample(n);
        top->setText(0, s ? (s->control ? QStringLiteral("★ ") : QString()) + n : kNoSample);
        top->setData(0, Qt::UserRole, SampleItem);
        top->setData(0, Qt::UserRole + 1, n);
        if (s) {
            QStringList tip;
            for (const auto& [k, v] : {std::pair{tr("material"), s->material}, {tr("batch"), s->batch}, {tr("side"), s->side},
                                        {tr("direction"), s->direction}, {tr("treatment"), s->treatment}, {tr("notes"), s->notes}})
                if (!v.isEmpty()) tip << QStringLiteral("%1: %2").arg(k, v.toHtmlEscaped());
            top->setToolTip(0, tip.join(QStringLiteral("<br>")));
        }
        int count = 0;
        for (const ld::Recording& r : project_->recordings) {
            const bool mine = n.isEmpty() ? (r.sample.isEmpty() || !project_->sample(r.sample)) : r.sample == n;
            if (!mine) continue;
            auto* it = new QTreeWidgetItem(top);
            // short label: the time from the capture tool's file name (the date is a column), else the file name
            const QString base = QFileInfo(r.file).completeBaseName();
            const QRegularExpressionMatch m = QRegularExpression(QStringLiteral("_(\\d{8})_(\\d{2})(\\d{2})(\\d{2})$")).match(base);
            const QString label = m.hasMatch() ? QStringLiteral("%1:%2:%3").arg(m.captured(2), m.captured(3), m.captured(4)) : base;
            it->setText(0, (r.glassReference ? QStringLiteral("◆ ") : QString()) + label);
            it->setText(1, r.liquid);
            it->setText(2, r.date);
            it->setData(0, Qt::UserRole, RecordingItem);
            it->setData(0, Qt::UserRole + 1, r.file);
            QString tip = r.file.toHtmlEscaped();
            if (std::isfinite(r.temperature))
                tip += QStringLiteral("<br>%1 °C%2").arg(r.temperature, 0, 'f', 1)
                           .arg(std::isfinite(r.humidity) ? QStringLiteral(", %1 %RH").arg(r.humidity, 0, 'f', 0) : QString());
            if (!r.notes.isEmpty()) tip += QStringLiteral("<br>") + r.notes.toHtmlEscaped();
            it->setToolTip(0, tip);
            ++count;
        }
        top->setText(1, tr("%n recording(s)", nullptr, count));
        top->setExpanded(true);
    }
}

void LibraryPanel::addRecordings(const QStringList& paths) {
    if (!project_ || paths.isEmpty()) return;
    bool outside = false;
    for (const QString& p : paths) outside |= project_->relative(QFileInfo(p).absoluteFilePath()).startsWith(QStringLiteral(".."));
    bool copy = false;
    if (outside) {
        const auto b = QMessageBox::question(this, tr("Add recordings"),
                                             tr("Copy the recordings into the project folder (recordings/)? Then the project "
                                                "holds everything. Otherwise it refers to them where they are."),
                                             QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::Yes);
        if (b == QMessageBox::Cancel) return;
        copy = b == QMessageBox::Yes;
    }
    QStringList problems;
    for (const QString& p : paths) {
        QString err;
        if (!project_->addRecording(p, copy, nullptr, &err)) problems << err;
    }
    save();
    if (!problems.isEmpty()) QMessageBox::information(this, tr("Add recordings"), problems.join(QLatin1Char('\n')));
}

void LibraryPanel::edit(QTreeWidgetItem* it) {
    if (!project_ || !it) return;
    QDialog d(this);
    auto* form = new QFormLayout(&d);
    auto line = [&](const QString& label, const QString& v) {
        auto* e = new QLineEdit(v);
        form->addRow(label, e);
        return e;
    };
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(bb, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    if (it->data(0, Qt::UserRole).toInt() == SampleItem) {
        const QString old = it->data(0, Qt::UserRole + 1).toString();
        ld::Sample* s = project_->sample(old);
        if (!s) return;
        d.setWindowTitle(tr("Sample"));
        auto* name = line(tr("Name"), s->name);
        auto* material = line(tr("Material"), s->material);
        auto* batch = line(tr("Batch"), s->batch);
        auto* side = line(tr("Side"), s->side);
        auto* dir = line(tr("Direction"), s->direction);
        auto* treat = line(tr("Treatment"), s->treatment);
        auto* notes = line(tr("Notes"), s->notes);
        auto* control = new QCheckBox(tr("Control / reference sample (glass, PTFE, a known separator)"));
        control->setChecked(s->control);
        form->addRow(QString(), control);
        form->addRow(bb);
        if (d.exec() != QDialog::Accepted) return;
        const QString nn = name->text().trimmed();
        if (nn.isEmpty() || (nn != old && project_->sample(nn))) {
            QMessageBox::warning(this, tr("Sample"), tr("The name must be new and not empty."));
            return;
        }
        s->name = nn, s->material = material->text(), s->batch = batch->text(), s->side = side->text();
        s->direction = dir->text(), s->treatment = treat->text(), s->notes = notes->text(), s->control = control->isChecked();
        for (ld::Recording& r : project_->recordings)
            if (r.sample == old) r.sample = nn;
    } else {
        ld::Recording* r = recordingFor(project_->absolute(it->data(0, Qt::UserRole + 1).toString()));
        if (!r) return;
        d.setWindowTitle(QFileInfo(r->file).fileName());
        auto* sample = new QComboBox;
        sample->setEditable(true);
        for (const ld::Sample& s : project_->samples) sample->addItem(s.name);
        sample->setCurrentText(r->sample);
        form->addRow(tr("Sample (new name: a new sample)"), sample);
        auto* liquid = new QComboBox;
        liquid->setEditable(true);
        for (const ld::LiquidData& l : project_->liquids) liquid->addItem(l.name);
        liquid->setCurrentText(r->liquid);
        form->addRow(tr("Liquid"), liquid);
        auto* date = line(tr("Date (yyyy-mm-dd)"), r->date);
        auto* vol = new QDoubleSpinBox;
        vol->setRange(0, 100);
        vol->setDecimals(2);
        vol->setSuffix(tr(" µL"));
        vol->setSpecialValueText(tr("not noted"));
        vol->setValue(std::isfinite(r->volumeUl) ? r->volumeUl : 0);
        form->addRow(tr("Dispensed volume"), vol);
        auto* notes = line(tr("Notes"), r->notes);
        form->addRow(bb);
        if (d.exec() != QDialog::Accepted) return;
        r->sample = sample->currentText().trimmed();
        if (!r->sample.isEmpty() && !project_->sample(r->sample)) project_->samples.push_back({r->sample});
        r->liquid = liquid->currentText().trimmed(), r->date = date->text().trimmed(), r->notes = notes->text();
        r->volumeUl = vol->value() > 0 ? vol->value() : ld::kNaN;
    }
    save();
}

void LibraryPanel::compareSelected() {
    if (!project_) return;
    QStringList paths, names;
    std::set<QString> seen;
    auto addRec = [&](const ld::Recording& r) {
        if (r.glassReference || !seen.insert(r.file).second) return;
        paths << project_->absolute(r.file);
        const QString n = r.sample.isEmpty() ? QFileInfo(r.file).completeBaseName() : r.sample;
        names << (r.liquid.isEmpty() ? n : QStringLiteral("%1 · %2").arg(n, r.liquid));   // a sample with each liquid
    };
    QList<QTreeWidgetItem*> sel = tree_->selectedItems();
    if (sel.isEmpty())
        for (int i = 0; i < tree_->topLevelItemCount(); ++i) sel << tree_->topLevelItem(i);   // nothing chosen: all
    for (QTreeWidgetItem* it : sel) {
        if (it->data(0, Qt::UserRole).toInt() == RecordingItem) {
            if (const ld::Recording* r = recordingFor(project_->absolute(it->data(0, Qt::UserRole + 1).toString()))) addRec(*r);
        } else {
            const QString n = it->data(0, Qt::UserRole + 1).toString();
            for (const ld::Recording& r : project_->recordings)
                if (n.isEmpty() ? r.sample.isEmpty() : r.sample == n) addRec(r);
        }
    }
    if (paths.isEmpty()) {
        QMessageBox::information(this, tr("Compare"), tr("No recordings selected (glass references are left out)."));
        return;
    }
    emit compare(paths, names);
}
