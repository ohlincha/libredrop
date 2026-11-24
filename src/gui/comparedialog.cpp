#include "comparedialog.h"

#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QSplitter>
#include <QTextStream>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <cmath>
#include <map>

namespace {
QString num(double v, int prec) { return std::isfinite(v) ? QString::number(v, 'f', prec) : QString(); }
QString sig(double v) { return std::isfinite(v) ? QString::number(v, 'g', 3) : QString(); }
struct Stat {
    double mean = ld::kNaN, sd = ld::kNaN;
    int n = 0;
};
Stat stat(const std::vector<double>& v) {
    Stat s;
    double sum = 0;
    for (double x : v)
        if (std::isfinite(x)) sum += x, ++s.n;
    if (s.n == 0) return s;
    s.mean = sum / s.n;
    if (s.n > 1) {
        double q = 0;
        for (double x : v)
            if (std::isfinite(x)) q += (x - s.mean) * (x - s.mean);
        s.sd = std::sqrt(q / (s.n - 1));
    }
    return s;
}
QString pm(const Stat& s, int prec, bool significant = false) {
    if (s.n == 0) return QStringLiteral("—");
    auto f = [&](double v) { return significant ? sig(v) : num(v, prec); };
    return s.n > 1 ? QStringLiteral("%1 ± %2").arg(f(s.mean), f(s.sd)) : f(s.mean);
}
}  // namespace

CompareDialog::CompareDialog(CompareSettings settings, QWidget* parent) : QDialog(parent), settings_(std::move(settings)) {
    setWindowTitle(tr("Compare samples"));
    resize(1100, 700);
    info_ = new QLabel;
    info_->setWordWrap(true);
    tree_ = new QTreeWidget;
    tree_->setRootIsDecorated(true);
    tree_->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    tree_->setToolTip(tr("<p>Double-click a sample name to change it: recordings with the same name are one sample. "
                         "Untick drops that are not usable.</p>"));
    summary_ = new QTreeWidget;
    summary_->setRootIsDecorated(false);
    summary_->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    auto* add = new QPushButton(tr("Add recordings…"));
    auto* remove = new QPushButton(tr("Remove"));
    analyseButton_ = new QPushButton(tr("Analyse"));
    analyseButton_->setObjectName(QStringLiteral("analyse"));
    auto* exp = new QPushButton(tr("Export CSV…"));
    auto* buttons = new QHBoxLayout;
    buttons->addWidget(add);
    buttons->addWidget(remove);
    buttons->addStretch(1);
    buttons->addWidget(analyseButton_);
    buttons->addWidget(exp);
    auto* split = new QSplitter(Qt::Vertical);
    split->addWidget(tree_);
    auto* sumBox = new QWidget;
    auto* sl = new QVBoxLayout(sumBox);
    sl->setContentsMargins(0, 0, 0, 0);
    auto* sumTitle = new QLabel(tr("<b>Per sample</b> (mean ± standard deviation of the ticked drops)"));
    sl->addWidget(sumTitle);
    sl->addWidget(summary_, 1);
    split->addWidget(sumBox);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);
    auto* l = new QVBoxLayout(this);
    l->addWidget(info_);
    l->addLayout(buttons);
    l->addWidget(split, 1);

    connect(add, &QPushButton::clicked, this, [this] {
        const QStringList paths = QFileDialog::getOpenFileNames(this, tr("Add recordings"), QString(),
                                                                tr("Videos (*.mkv *.avi *.mp4 *.mov);;All files (*)"));
        addFiles(paths);
    });
    connect(remove, &QPushButton::clicked, this, [this] {
        if (watcher_.isRunning()) return;
        QTreeWidgetItem* it = tree_->currentItem();
        if (!it) return;
        if (it->parent()) it = it->parent();
        files_.erase(files_.begin() + it->data(0, Qt::UserRole).toInt());
        rebuild();
    });
    connect(analyseButton_, &QPushButton::clicked, this, &CompareDialog::analyseAll);
    connect(exp, &QPushButton::clicked, this, &CompareDialog::exportCsv);
    connect(tree_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* it, int col) {
        if (!it->parent() && col == 0) tree_->editItem(it, 0);
    });
    connect(tree_, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* it, int col) {
        if (!it->parent() && col == 0) {
            const int i = it->data(0, Qt::UserRole).toInt();
            if (i >= 0 && i < int(files_.size())) files_[std::size_t(i)].sample = it->text(0).trimmed();
        }
        updateSummary();
    });
    connect(&watcher_, &QFutureWatcher<void>::finished, this, [this] {
        analyseButton_->setEnabled(true);
        rebuild();
    });
    rebuild();
}

QString CompareDialog::defaultSample(const QString& path) const {
    QString b = QFileInfo(path).completeBaseName();
    b.remove(QRegularExpression(QStringLiteral("_\\d{8}_\\d{6}$")));   // the capture tool's date and time
    b.remove(QRegularExpression(QStringLiteral("[_ -]\\d{1,3}$")));     // repeat number
    return b.isEmpty() ? QFileInfo(path).completeBaseName() : b;
}

void CompareDialog::addFiles(const QStringList& paths, const QStringList& samples) {
    for (int i = 0; i < paths.size(); ++i) {
        const QString& p = paths[i];
        bool have = false;
        for (const File& f : files_) have |= f.path == p;
        if (have) continue;
        File f;
        f.path = p;
        f.sample = i < samples.size() && !samples[i].isEmpty() ? samples[i] : defaultSample(p);
        files_.push_back(f);
    }
    rebuild();
}

void CompareDialog::analyseAll() {
    if (watcher_.isRunning()) return;
    times_ = settings_.soak(QString()).times;
    std::vector<int> todo;
    for (int i = 0; i < int(files_.size()); ++i)
        if (!files_[std::size_t(i)].done) todo.push_back(i);
    if (todo.empty()) {   // everything analysed: again (e.g. after changing the settings)
        for (int i = 0; i < int(files_.size()); ++i) todo.push_back(i), files_[std::size_t(i)].done = false;
    }
    analyseButton_->setEnabled(false);
    QPointer<CompareDialog> self(this);
    const auto optionsFor = settings_.options;
    struct Job {
        int index;
        QString path;
        ld::SoakOptions soak;
    };
    std::vector<Job> jobs;
    for (int i : todo) jobs.push_back({i, files_[std::size_t(i)].path, settings_.soak(files_[std::size_t(i)].path)});
    for (int i : todo) files_[std::size_t(i)].error = tr("waiting…");
    rebuild();
    watcher_.setFuture(QtConcurrent::run([self, jobs, optionsFor] {
        for (const Job& job : jobs) {
            const int index = job.index;
            const QString& path = job.path;
            File f;
            f.path = path;
            const ld::VideoInfo info = ld::probeVideo(path);
            if (!info.error.isEmpty()) {
                f.error = info.error;
            } else {
                const ld::VideoOptions o = optionsFor(info.w, path);
                const ld::VideoResult r = ld::analyzeVideo(path, o, {});
                if (!r.ok) {
                    f.error = r.error;
                } else {
                    const ld::Units u{o.scene.drop.pxPerMm};
                    const ld::SeriesSet set = ld::seriesOf(r, u);
                    f.lenUnit = set.lenUnit;
                    ld::SoakOptions so = job.soak;
                    so.videoEnd = r.info.times.empty() ? ld::kNaN : r.info.times.back();
                    for (const ld::DropSeries& d : set.drops) f.drops.push_back({d.id, ld::soak(d, so), ld::summarize(d)});
                }
            }
            f.done = true;
            QMetaObject::invokeMethod(qApp, [self, index, f] {
                if (!self || index >= int(self->files_.size()) || self->files_[std::size_t(index)].path != f.path) return;
                File& dst = self->files_[std::size_t(index)];
                dst.done = true, dst.error = f.error, dst.drops = f.drops, dst.lenUnit = f.lenUnit;
                self->rebuild();
            }, Qt::QueuedConnection);
        }
    }));
}

void CompareDialog::rebuild() {
    info_->setText(settings_.describe() +
                   tr("<br>Add the recordings of all samples (same liquid), then Analyse. Recordings with the same sample "
                      "name are averaged."));
    if (times_.empty()) times_ = settings_.soak(QString()).times;
    QString L = QStringLiteral("mm");
    for (const File& f : files_)
        if (!f.lenUnit.isEmpty()) L = f.lenUnit;
    QStringList head{tr("Sample"), tr("File"), tr("Drop")};
    for (double t : times_) head << tr("θ at %1 s (°)").arg(t);
    head << tr("Sorptivity (%1/√s)").arg(L) << tr("R²") << tr("Absorbed after (s)") << tr("Contact (s)");
    QSignalBlocker block(tree_);
    // keep the ticks
    std::map<std::pair<QString, int>, bool> ticked;
    for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
        QTreeWidgetItem* top = tree_->topLevelItem(i);
        for (int j = 0; j < top->childCount(); ++j)
            ticked[{top->data(1, Qt::UserRole).toString(), top->child(j)->data(2, Qt::UserRole).toInt()}] =
                top->child(j)->checkState(2) == Qt::Checked;
    }
    tree_->clear();
    tree_->setColumnCount(int(head.size()));
    tree_->setHeaderLabels(head);
    const int c0 = 3, cs = c0 + int(times_.size());
    for (int i = 0; i < int(files_.size()); ++i) {
        const File& f = files_[std::size_t(i)];
        auto* top = new QTreeWidgetItem(tree_);
        top->setData(0, Qt::UserRole, i);
        top->setData(1, Qt::UserRole, f.path);
        top->setText(0, f.sample);
        top->setFlags(top->flags() | Qt::ItemIsEditable);
        top->setText(1, QFileInfo(f.path).fileName());
        top->setToolTip(1, f.path);
        top->setText(2, !f.error.isEmpty() ? f.error : f.done ? tr("%n drop(s)", nullptr, int(f.drops.size())) : tr("not analysed"));
        for (const Drop& d : f.drops) {
            auto* it = new QTreeWidgetItem(top);
            it->setData(2, Qt::UserRole, d.id);
            it->setText(2, tr("drop %1").arg(d.id));
            const auto k = ticked.find({f.path, d.id});
            it->setCheckState(2, k == ticked.end() || k->second ? Qt::Checked : Qt::Unchecked);
            for (std::size_t t = 0; t < d.k.angleAt.size(); ++t) it->setText(c0 + int(t), num(d.k.angleAt[t], 1));
            it->setText(cs, sig(d.k.sorptivity));
            it->setText(cs + 1, num(d.k.sorptivityR2, 3));
            it->setText(cs + 2, num(d.k.absorbedAfter, 2));
            it->setText(cs + 3, num(d.s.tContact, 2));
        }
        top->setExpanded(true);
    }
    updateSummary();
}

void CompareDialog::updateSummary() {
    struct Acc {
        std::vector<std::vector<double>> at;
        std::vector<double> S, abs;
        int drops = 0, recordings = 0;
    };
    std::map<QString, Acc> by;
    std::vector<QString> order;
    for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
        QTreeWidgetItem* top = tree_->topLevelItem(i);
        const File& f = files_[std::size_t(top->data(0, Qt::UserRole).toInt())];
        if (!by.count(f.sample)) order.push_back(f.sample);
        Acc& a = by[f.sample];
        a.at.resize(times_.size());
        ++a.recordings;
        for (int j = 0; j < top->childCount(); ++j) {
            if (top->child(j)->checkState(2) != Qt::Checked) continue;
            const int id = top->child(j)->data(2, Qt::UserRole).toInt();
            for (const Drop& d : f.drops) {
                if (d.id != id) continue;
                ++a.drops;
                for (std::size_t t = 0; t < times_.size() && t < d.k.angleAt.size(); ++t) a.at[t].push_back(d.k.angleAt[t]);
                a.S.push_back(d.k.sorptivity);
                a.abs.push_back(d.k.absorbedAfter);
            }
        }
    }
    QStringList head{tr("Sample"), tr("Recordings"), tr("Drops")};
    for (double t : times_) head << tr("θ at %1 s (°)").arg(t);
    QString L = QStringLiteral("mm");
    for (const File& f : files_)
        if (!f.lenUnit.isEmpty()) L = f.lenUnit;
    head << tr("Sorptivity (%1/√s)").arg(L) << tr("Absorbed after (s)");
    summary_->clear();
    summary_->setColumnCount(int(head.size()));
    summary_->setHeaderLabels(head);
    rows_.clear();
    for (const QString& name : order) {
        const Acc& a = by[name];
        SampleRow row{name, a.recordings, a.drops, stat(a.abs).n, {}, {}};
        for (const auto& v : a.at) row.mean.push_back(stat(v).mean), row.sd.push_back(stat(v).sd);
        for (const auto* v : {&a.S, &a.abs}) row.mean.push_back(stat(*v).mean), row.sd.push_back(stat(*v).sd);
        rows_.push_back(row);
        auto* it = new QTreeWidgetItem(summary_);
        it->setText(0, name);
        it->setText(1, QString::number(a.recordings));
        it->setText(2, QString::number(a.drops));
        for (std::size_t t = 0; t < times_.size(); ++t) it->setText(3 + int(t), pm(stat(a.at[t]), 1));
        it->setText(3 + int(times_.size()), pm(stat(a.S), 0, true));
        const Stat ab = stat(a.abs);
        it->setText(4 + int(times_.size()), ab.n ? tr("%1 (%2 of %3)").arg(pm(ab, 2)).arg(ab.n).arg(a.drops) : tr("—"));
    }
}

void CompareDialog::exportCsv() {
    const QString path = QFileDialog::getSaveFileName(this, tr("Export CSV"), QStringLiteral("comparison.csv"), tr("CSV (*.csv)"));
    if (path.isEmpty()) return;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, tr("Export"), tr("Cannot write %1").arg(path));
        return;
    }
    QTextStream o(&file);
    QStringList head{QStringLiteral("row"), QStringLiteral("sample"), QStringLiteral("file"), QStringLiteral("drop")};
    for (double t : times_) head << QStringLiteral("angle_at_%1s_deg").arg(t);
    head << QStringLiteral("sorptivity") << QStringLiteral("r2") << QStringLiteral("absorbed_after_s") << QStringLiteral("contact_s");
    o << head.join(QLatin1Char(',')) << '\n';
    auto q = [](QString s) { return s.contains(QLatin1Char(',')) || s.contains(QLatin1Char('"')) ? QLatin1Char('"') + s.replace(QLatin1Char('"'), QStringLiteral("\"\"")) + QLatin1Char('"') : s; };
    for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
        QTreeWidgetItem* top = tree_->topLevelItem(i);
        const File& f = files_[std::size_t(top->data(0, Qt::UserRole).toInt())];
        for (int j = 0; j < top->childCount(); ++j) {
            if (top->child(j)->checkState(2) != Qt::Checked) continue;
            const int id = top->child(j)->data(2, Qt::UserRole).toInt();
            for (const Drop& d : f.drops) {
                if (d.id != id) continue;
                QStringList r{QStringLiteral("drop"), q(f.sample), q(QFileInfo(f.path).fileName()), QString::number(d.id)};
                for (double v : d.k.angleAt) r << num(v, 2);
                r << sig(d.k.sorptivity) << num(d.k.sorptivityR2, 4) << num(d.k.absorbedAfter, 3) << num(d.s.tContact, 3);
                o << r.join(QLatin1Char(',')) << '\n';
            }
        }
    }
    // per sample: a row of means and a row of standard deviations (file: recordings, drop: drops measured)
    for (const SampleRow& row : rows_)
        for (int k = 0; k < 2; ++k) {
            const std::vector<double>& v = k == 0 ? row.mean : row.sd;
            QStringList r{k == 0 ? QStringLiteral("sample_mean") : QStringLiteral("sample_sd"), q(row.name),
                          QString::number(row.recordings), QString::number(row.drops)};
            for (std::size_t c = 0; c < times_.size(); ++c) r << num(v[c], 2);
            r << sig(v[times_.size()]) << QString() << num(v[times_.size() + 1], 3) << QString();
            o << r.join(QLatin1Char(',')) << '\n';
        }
}
