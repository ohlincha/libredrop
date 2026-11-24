#include "mainwindow.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImageReader>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QProgressBar>
#include <QScrollArea>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QDir>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QThreadPool>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <algorithm>
#include <cmath>

#include "frameview.h"
#include "plotwidget.h"

namespace {

struct Liquid {
    const char* name;
    double density, tension;   // kg/m³, mN/m (about 20–25 °C)
};
// Literature values near room temperature; enter measured ones under Custom for accurate Young–Laplace volumes.
const Liquid kLiquids[] = {
    {"Water", 997, 72.0},
    {"Dimethyl carbonate (DMC)", 1069, 28.5},
    {"Diethyl carbonate (DEC)", 975, 26.7},
    {"Propylene carbonate (PC)", 1205, 41.1},
};

// px/mm of the Ossila camera per frame width, from the 10 mm calibration sphere (knowledge/camera-scale.md)
double defaultScale(int width) {
    if (width == 1920) return 94.23;
    if (width == 1280) return 108.0;
    if (width == 640) return 45.5;
    return 0;
}

QString fmt(double v, int prec) { return std::isfinite(v) ? QString::number(v, 'f', prec) : QStringLiteral("—"); }

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("LibreDrop"));
    setAcceptDrops(true);
    buildUi();
    connect(&watcher_, &QFutureWatcher<ld::VideoResult>::finished, this, &MainWindow::analysisDone);
    QSettings st;
    restoreGeometry(st.value("window/geometry").toByteArray());
}

MainWindow::~MainWindow() = default;

void MainWindow::buildUi() {
    QMenu* file = menuBar()->addMenu(tr("&File"));
    file->addAction(tr("&Open video or image…"), QKeySequence::Open, this, &MainWindow::open);
    file->addSeparator();
    QAction* expTable = file->addAction(tr("Export &table (CSV)…"), this, &MainWindow::exportTable);
    file->addAction(tr("Export &plot (PNG)…"), this, &MainWindow::exportPlot);
    expTable->setShortcut(QKeySequence::Save);
    file->addSeparator();
    file->addAction(tr("&Quit"), QKeySequence::Quit, this, &QWidget::close);
    QMenu* help = menuBar()->addMenu(tr("&Help"));
    help->addAction(tr("Open &example (water on glass)"), this, &MainWindow::openExample);
    help->addSeparator();
    help->addAction(tr("&About LibreDrop"), this, [this] {
        QMessageBox::about(this, tr("About LibreDrop"),
                           tr("<b>LibreDrop %1</b><p>Contact angle and drop shape from backlit side views: the surface "
                              "and every drop are found automatically, in single images and through whole videos.</p>"
                              "<p>Open source (MIT): github.com/ohlincha/libredrop</p>")
                               .arg(QStringLiteral(LIBREDROP_VERSION)));
    });

    // left: frame and time slider
    view_ = new FrameView;
    slider_ = new QSlider(Qt::Horizontal);
    slider_->setEnabled(false);
    timeLabel_ = new QLabel;
    timeLabel_->setMinimumWidth(170);
    auto* timeRow = new QHBoxLayout;
    timeRow->addWidget(slider_, 1);
    timeRow->addWidget(timeLabel_);
    auto* left = new QWidget;
    auto* ll = new QVBoxLayout(left);
    ll->setContentsMargins(0, 0, 0, 0);
    ll->addWidget(view_, 1);
    ll->addLayout(timeRow);

    // right: settings, drops, plot
    scale_ = new QDoubleSpinBox;
    scale_->setRange(0, 10000);
    scale_->setDecimals(2);
    scale_->setSuffix(tr(" px/mm"));
    scale_->setSpecialValueText(tr("unknown (px)"));
    scale_->setToolTip(tr("Image scale. Set from the calibration sphere for each camera mode; 0 gives results in pixels."));
    liquid_ = new QComboBox;
    for (const Liquid& l : kLiquids) liquid_->addItem(tr(l.name));
    liquid_->addItem(tr("Custom"));
    density_ = new QDoubleSpinBox;
    density_->setRange(0, 30000);
    density_->setDecimals(0);
    density_->setSuffix(tr(" kg/m³"));
    density_->setToolTip(tr("Density difference between liquid and air"));
    tension_ = new QDoubleSpinBox;
    tension_->setRange(0, 1000);
    tension_->setDecimals(1);
    tension_->setSuffix(tr(" mN/m"));
    step_ = new QSpinBox;
    step_->setRange(1, 1000);
    step_->setPrefix(tr("1 in "));
    step_->setSpecialValueText(tr("all"));
    step_->setToolTip(tr("Measure every n-th frame (1: all). Larger steps are faster on long videos."));
    analyseButton_ = new QPushButton(tr("Analyse"));
    analyseButton_->setEnabled(false);
    progress_ = new QProgressBar;
    progress_->setVisible(false);
    auto* settings = new QGroupBox(tr("Measurement"));
    auto* form = new QFormLayout(settings);
    form->addRow(tr("Scale"), scale_);
    form->addRow(tr("Liquid"), liquid_);
    auto* lt = new QHBoxLayout;
    lt->addWidget(density_);
    lt->addWidget(tension_);
    form->addRow(tr("ρ, γ"), lt);
    form->addRow(tr("Frames"), step_);
    auto* ar = new QHBoxLayout;
    ar->addWidget(analyseButton_);
    ar->addWidget(progress_, 1);
    form->addRow(ar);

    table_ = new QTreeWidget;
    table_->setRootIsDecorated(false);
    table_->setHeaderLabels({tr("Drop"), tr("Touched"), tr("Angle"), tr("Base"), tr("Volume"), tr("Frames")});
    table_->setToolTip(tr("Tick the drops that count: only those are plotted and exported. "
                          "Values: at release from the needle → at the end."));
    table_->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table_->header()->setStretchLastSection(false);
    table_->setMinimumHeight(110);
    checks_ = new QLabel;
    checks_->setWordWrap(true);
    checks_->setTextFormat(Qt::RichText);
    checks_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    plot_ = new PlotWidget;
    sinceContact_ = new QCheckBox(tr("Time since each drop touched the surface"));

    auto* right = new QWidget;
    auto* rl = new QVBoxLayout(right);
    rl->setContentsMargins(0, 0, 0, 0);
    rl->addWidget(settings);
    auto* rsplit = new QSplitter(Qt::Vertical);
    auto* tableBox = new QWidget;
    auto* tl = new QVBoxLayout(tableBox);
    tl->setContentsMargins(0, 0, 0, 0);
    tl->addWidget(table_, 1);
    auto* checksScroll = new QScrollArea;
    checksScroll->setWidget(checks_);
    checksScroll->setWidgetResizable(true);
    checksScroll->setFrameShape(QFrame::NoFrame);
    checksScroll->setMaximumHeight(90);
    tl->addWidget(checksScroll);
    auto* plotBox = new QWidget;
    plotBox_ = plotBox;
    auto* pl = new QVBoxLayout(plotBox);
    pl->setContentsMargins(0, 0, 0, 0);
    pl->addWidget(plot_, 1);
    pl->addWidget(sinceContact_);
    rsplit->addWidget(tableBox);
    rsplit->addWidget(plotBox);
    rsplit->setStretchFactor(1, 3);
    rl->addWidget(rsplit, 1);

    auto* split = new QSplitter(Qt::Horizontal);
    split->addWidget(left);
    split->addWidget(right);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);
    setCentralWidget(split);
    resize(1500, 900);

    frameTimer_ = new QTimer(this);
    frameTimer_->setSingleShot(true);
    frameTimer_->setInterval(60);
    connect(frameTimer_, &QTimer::timeout, this, [this] { showFrame(slider_->value()); });
    connect(slider_, &QSlider::valueChanged, this, [this](int v) {
        timeLabel_->setText(tr("%1 s  (frame %2)").arg(frameTime(v), 0, 'f', 2).arg(v));
        frameTimer_->start();
    });
    connect(analyseButton_, &QPushButton::clicked, this, [this] {
        if (watcher_.isRunning()) cancel();
        else analyse();
    });
    connect(liquid_, &QComboBox::currentIndexChanged, this, &MainWindow::liquidChanged);
    connect(table_, &QTreeWidget::itemChanged, this, [this] {
        view_->setChosen(chosenIds());
        updatePlot();
    });
    connect(table_, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* it) {
        plot_->setHighlight(it ? it->data(0, Qt::UserRole).toInt() : -1);
    });
    connect(view_, &FrameView::dropClicked, this, [this](int id) {
        for (int i = 0; i < table_->topLevelItemCount(); ++i)
            if (table_->topLevelItem(i)->data(0, Qt::UserRole).toInt() == id) table_->setCurrentItem(table_->topLevelItem(i));
    });
    connect(plot_, &PlotWidget::timeClicked, this, [this](double t) { slider_->setValue(frameAt(t)); });
    connect(sinceContact_, &QCheckBox::toggled, plot_, &PlotWidget::setSinceContact);
    for (QWidget* w : std::initializer_list<QWidget*>{scale_, density_, tension_, step_})
        connect(qobject_cast<QAbstractSpinBox*>(w), &QAbstractSpinBox::editingFinished, this, &MainWindow::saveSettings);

    QSettings st;
    liquid_->setCurrentIndex(std::clamp(st.value("liquid", 0).toInt(), 0, liquid_->count() - 1));
    liquidChanged();
    if (liquid_->currentIndex() == liquid_->count() - 1) {
        density_->setValue(st.value("density", 1000).toDouble());
        tension_->setValue(st.value("tension", 50).toDouble());
    }
    scale_->setValue(st.value("scale", 94.23).toDouble());
    step_->setValue(st.value("step", 1).toInt());
    sinceContact_->setChecked(st.value("sinceContact", false).toBool());
    statusBar()->showMessage(tr("Open a recording to start."));
}

void MainWindow::liquidChanged() {
    const int i = liquid_->currentIndex();
    const bool custom = i >= int(std::size(kLiquids));
    density_->setEnabled(custom);
    tension_->setEnabled(custom);
    if (!custom) {
        density_->setValue(kLiquids[i].density);
        tension_->setValue(kLiquids[i].tension);
    }
    saveSettings();
}

void MainWindow::saveSettings() const {
    QSettings st;
    st.setValue("liquid", liquid_->currentIndex());
    st.setValue("density", density_->value());
    st.setValue("tension", tension_->value());
    st.setValue("scale", scale_->value());
    st.setValue("step", step_->value());
    if (sinceContact_) st.setValue("sinceContact", sinceContact_->isChecked());
}

void MainWindow::openExample() {
    // the example is built in; ffmpeg needs a file, so it is copied out
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir().mkpath(dir);
    const QString f = QDir(dir).filePath(QStringLiteral("water_on_glass.mkv"));
    QFile::setPermissions(f, QFile::ReadOwner | QFile::WriteOwner);
    QFile::remove(f);
    if (!QFile::copy(QStringLiteral(":/examples/water_on_glass.mkv"), f)) {
        QMessageBox::warning(this, tr("Example"), tr("Cannot write %1").arg(f));
        return;
    }
    QFile::setPermissions(f, QFile::ReadOwner | QFile::WriteOwner);
    liquid_->setCurrentIndex(0);   // water
    scale_->setValue(94.23);
    openFile(f);
}

void MainWindow::open() {
    QSettings st;
    const QString f = QFileDialog::getOpenFileName(
        this, tr("Open video or image"), st.value("lastDir").toString(),
        tr("Recordings (*.mkv *.mp4 *.avi *.mov *.png *.jpg *.jpeg *.tif *.tiff *.bmp);;All files (*)"));
    if (f.isEmpty()) return;
    st.setValue("lastDir", QFileInfo(f).path());
    openFile(f);
}

void MainWindow::openFile(const QString& path) {
    if (watcher_.isRunning()) {
        cancel();
        watcher_.waitForFinished();
    }
    path_ = path;
    haveResult_ = false;
    result_ = {};
    table_->clear();
    plot_->setSeries({});
    checks_->clear();
    isImage_ = !QImageReader::imageFormat(path).isEmpty();
    info_ = {};
    if (isImage_) {
        const QImageReader r(path);
        info_.w = r.size().width(), info_.h = r.size().height();
        info_.times = {0.0};
    } else {
        QApplication::setOverrideCursor(Qt::WaitCursor);
        statusBar()->showMessage(tr("Reading %1…").arg(QFileInfo(path).fileName()));
        QApplication::processEvents();
        info_ = ld::probeVideo(path);
        QApplication::restoreOverrideCursor();
        if (!info_.error.isEmpty()) {
            QMessageBox::warning(this, tr("Cannot open"), info_.error);
            statusBar()->clearMessage();
            return;
        }
    }
    setWindowTitle(QStringLiteral("%1 – LibreDrop").arg(QFileInfo(path).fileName()));
    const double sc = defaultScale(info_.w);
    if (sc > 0 && QSettings().value(QStringLiteral("scaleFor/%1").arg(info_.w)).isNull()) scale_->setValue(sc);
    else if (sc > 0) scale_->setValue(QSettings().value(QStringLiteral("scaleFor/%1").arg(info_.w)).toDouble());
    slider_->blockSignals(true);
    slider_->setRange(0, std::max(0, int(info_.times.size()) - 1));
    slider_->setValue(0);
    slider_->blockSignals(false);
    slider_->setEnabled(!isImage_);
    slider_->setVisible(!isImage_);
    plotBox_->setVisible(!isImage_);
    table_->setHeaderLabels(isImage_ ? QStringList{tr("Drop"), tr("Left"), tr("Right"), tr("Base"), tr("Height"), tr("Volume")}
                                     : QStringList{tr("Drop"), tr("Touched"), tr("Angle"), tr("Base"), tr("Volume"), tr("Frames")});
    timeLabel_->setVisible(!isImage_);
    timeLabel_->setText(tr("%1 s  (frame %2)").arg(0.0, 0, 'f', 2).arg(0));
    analyseButton_->setEnabled(true);
    showFrame(0);
    analyse();
}

ld::VideoOptions MainWindow::options() const {
    ld::VideoOptions o;
    o.scene.drop.pxPerMm = scale_->value();
    o.scene.drop.densityDiff = density_->value();
    o.scene.drop.surfaceTension = tension_->value();
    o.scene.drop.staticMeasurement = false;
    o.step = isImage_ ? 1 : step_->value();
    if (isImage_) o.minPoints = 1, o.minDuration = 0;
    return o;
}

ld::Units MainWindow::units() const { return ld::Units{scale_->value()}; }

void MainWindow::analyse() {
    if (path_.isEmpty() || watcher_.isRunning()) return;
    saveSettings();
    if (info_.w > 0 && scale_->value() > 0) QSettings().setValue(QStringLiteral("scaleFor/%1").arg(info_.w), scale_->value());
    cancel_ = std::make_shared<std::atomic<bool>>(false);
    ld::VideoOptions o = options();
    o.cancel = cancel_.get();
    const QString path = path_;
    const bool image = isImage_;
    const auto cancelFlag = cancel_;   // kept alive by the task
    QPointer<MainWindow> self(this);
    auto progress = [self](int done, int total) {
        if (done % 10 && done != total) return;
        QMetaObject::invokeMethod(qApp, [self, done, total] {
            if (!self) return;
            self->progress_->setMaximum(total);
            self->progress_->setValue(done);
        }, Qt::QueuedConnection);
    };
    setBusy(true);
    statusBar()->showMessage(tr("Finding the surface and the drops…"));
    watcher_.setFuture(QtConcurrent::run([path, image, o, cancelFlag, progress] {
        return image ? ld::analyzeImages({path}, o, progress) : ld::analyzeVideo(path, o, progress);
    }));
}

void MainWindow::cancel() {
    if (cancel_) *cancel_ = true;
}

void MainWindow::setBusy(bool busy) {
    progress_->setVisible(busy);
    progress_->setValue(0);
    progress_->setMaximum(0);
    analyseButton_->setText(busy ? tr("Cancel") : tr("Analyse"));
}

void MainWindow::analysisDone() {
    setBusy(false);
    ld::VideoResult r = watcher_.result();
    if (!r.ok) {
        statusBar()->showMessage(r.error == QLatin1String("cancelled") ? tr("Cancelled.") : tr("Not analysed: %1").arg(r.error));
        return;
    }
    if (r.info.times.empty()) r.info = info_;
    result_ = std::move(r);
    resultUnits_ = units();
    haveResult_ = true;
    statusBar()->showMessage(tr("%n frame(s) measured, %1 drop(s); surface at y = %2 px, tilt %3°.", "", result_.frames)
                                 .arg(result_.tracks.size())
                                 .arg(result_.surface.p0.y, 0, 'f', 1)
                                 .arg(result_.surface.angle * 180 / M_PI, 0, 'f', 2));
    updateTable();
    updatePlot();
    showFrame(slider_->value());
}

void MainWindow::updateTable() {
    QSignalBlocker block(table_);
    table_->clear();
    const ld::SeriesSet set = ld::seriesOf(result_, resultUnits_);
    const QString L = set.lenUnit, V = set.volUnit == QLatin1String("uL") ? QStringLiteral("µL") : set.volUnit;
    for (const ld::DropSeries& d : set.drops) {
        const ld::TrackSummary s = ld::summarize(d);
        auto* it = new QTreeWidgetItem(table_);
        it->setData(0, Qt::UserRole, d.id);
        it->setText(0, QString::number(d.id));
        it->setCheckState(0, Qt::Checked);
        QPixmap swatch(12, 12);
        swatch.fill(ld::dropColor(d.id));
        it->setIcon(0, QIcon(swatch));
        if (isImage_) {   // one frame: left/right angle (Young–Laplace is symmetric: tangent sides), base, height, volume
            const ld::DropTrack& tr0 = *std::find_if(result_.tracks.begin(), result_.tracks.end(),
                                                      [&](const ld::DropTrack& t) { return t.id == d.id; });
            const ld::DropAnalysis& a = tr0.points.front().drop.a;
            const ld::AngleResult* m = a.method(QStringLiteral("tangent"));
            const bool ok = ld::usable(a);
            it->setText(1, ok && m && m->ok ? tr("%1°").arg(m->left, 0, 'f', 1) : tr("—"));
            it->setText(2, ok && m && m->ok ? tr("%1°").arg(m->right, 0, 'f', 1) : tr("—"));
            it->setText(3, tr("%1 %2").arg(fmt(d.base[0], 3), L));
            it->setText(4, tr("%1 %2").arg(fmt(ok ? resultUnits_.len(a.heightPx) : ld::kNaN, 3), L));
            it->setText(5, tr("%1 %2").arg(fmt(d.volume[0], 3), V));
            it->setToolTip(1, tr("Tangent method, left and right; Young–Laplace (symmetric): %1°").arg(fmt(d.angle[0], 1)));
            continue;
        }
        it->setText(1, tr("%1 s").arg(s.tContact, 0, 'f', 2));
        it->setText(2, tr("%1 → %2°").arg(fmt(s.angleRelease, 1), fmt(s.angleEnd, 1)));
        it->setText(3, tr("%1 → %2 %3").arg(fmt(s.baseRelease, 2), fmt(s.baseEnd, 2), L));
        it->setText(4, tr("%1 → %2 %3").arg(fmt(s.volumeRelease, 2), fmt(s.volumeEnd, 2), V));
        it->setText(5, s.failed ? tr("%1 (%2 not measured)").arg(s.points).arg(s.failed) : QString::number(s.points));
        it->setToolTip(2, tr("Angle change after release: %1°/s").arg(fmt(s.angleRate, 3)));
        it->setToolTip(4, tr("Volume change after release: %1 %2/s").arg(fmt(s.volumeRate, 4), V));
        if (s.tRelease < 0) it->setToolTip(1, tr("The needle stays in this drop"));
        else it->setToolTip(1, tr("Free of the needle from %1 s").arg(s.tRelease, 0, 'f', 2));
    }
    view_->setChosen(chosenIds());
}

QList<int> MainWindow::chosenIds() const {
    QList<int> ids;
    for (int i = 0; i < table_->topLevelItemCount(); ++i)
        if (table_->topLevelItem(i)->checkState(0) == Qt::Checked) ids << table_->topLevelItem(i)->data(0, Qt::UserRole).toInt();
    if (ids.isEmpty()) ids << -1;   // none chosen (an empty list would mean all)
    return ids;
}

void MainWindow::updatePlot() {
    if (!haveResult_ || isImage_) {
        plot_->setSeries({});
        return;
    }
    ld::SeriesSet set = ld::seriesOf(result_, resultUnits_);
    ld::selectDrops(&set, chosenIds());
    plot_->setSeries(set);
}

double MainWindow::frameTime(int index) const {
    if (index < 0 || index >= int(info_.times.size())) return 0;
    return info_.times[std::size_t(index)] - info_.times[0];
}

int MainWindow::frameAt(double t) const {
    if (info_.times.empty()) return 0;
    const double target = t + info_.times[0];
    const auto it = std::lower_bound(info_.times.begin(), info_.times.end(), target);
    int i = int(it - info_.times.begin());
    if (i > 0 && (i == int(info_.times.size()) || target - info_.times[std::size_t(i - 1)] < info_.times[std::size_t(i)] - target)) --i;
    return i;
}

void MainWindow::showFrame(int index) {
    if (path_.isEmpty()) return;
    if (frameBusy_) {   // one frame at a time; the latest request wins
        pendingFrame_ = index;
        return;
    }
    frameBusy_ = true;
    pendingFrame_ = -1;
    const int gen = ++frameGeneration_;
    const QString path = path_;
    const bool image = isImage_;
    const ld::VideoInfo info = info_;
    ld::SceneOptions so = options().scene;
    if (haveResult_) so.surface = result_.surface;
    QPointer<MainWindow> self(this);
    (void)QtConcurrent::run([=] {
        ld::GrayImage g;
        QString err;
        QImage shown;
        if (image) {
            const QImage q(path);
            g = ld::fromQImage(q);
        } else if (!ld::readFrameAt(path, info, index, &g, &err)) {
            g = {};
        }
        std::optional<ld::Scene> scene;
        if (!g.empty()) {
            shown = ld::toQImage(g);
            scene = ld::analyzeScene(g, so);
        }
        QMetaObject::invokeMethod(qApp, [self, gen, index, shown, scene] {
            if (self) self->frameReady(gen, index, shown, scene);
        }, Qt::QueuedConnection);
    });
}

void MainWindow::frameReady(int generation, int index, const QImage& image, const std::optional<ld::Scene>& scene) {
    frameBusy_ = false;
    if (pendingFrame_ >= 0) {
        showFrame(pendingFrame_);
        return;
    }
    if (generation != frameGeneration_) return;
    // track id of each drop in this frame: the track's point at this frame with the same position
    QList<int> ids;
    if (scene) {
        for (const ld::SceneDrop& d : scene->drops) {
            int id = 0;
            for (const ld::DropTrack& t : result_.tracks) {
                const auto it = std::lower_bound(t.points.begin(), t.points.end(), index,
                                                 [](const ld::TrackPoint& p, int f) { return p.frame < f; });
                if (it != t.points.end() && it->frame == index && std::abs(it->drop.xc - d.xc) < 3) {
                    id = t.id;
                    break;
                }
            }
            ids << id;
        }
    }
    view_->setFrame(image, scene, ids);
    plot_->setCursor(frameTime(index));
    // checks of the chosen drops in this frame
    QStringList lines;
    const QList<int> chosen = chosenIds();
    if (scene)
        for (int i = 0; i < int(scene->drops.size()); ++i) {
            if (!ids[i] || !chosen.contains(ids[i])) continue;
            for (const ld::Check& c : scene->drops[std::size_t(i)].a.checks)
                if (c.level != ld::Check::Info)
                    lines << QStringLiteral("<span style='color:%1'>●</span> %2: %3")
                                 .arg(ld::dropColor(ids[i]).name(), QString::number(ids[i]), c.message.toHtmlEscaped());
        }
    checks_->setText(lines.join(QStringLiteral("<br>")));
    if (image.isNull()) view_->setMessage(tr("Cannot read this frame (is ffmpeg installed?)."));
}

void MainWindow::exportTable() {
    if (!haveResult_) return;
    QSettings st;
    const QString def = QFileInfo(path_).completeBaseName() + QStringLiteral(".csv");
    const QString f = QFileDialog::getSaveFileName(this, tr("Export table"), QFileInfo(st.value("lastDir").toString(), def).filePath(),
                                                   tr("CSV (*.csv)"));
    if (f.isEmpty()) return;
    QFile out(f);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        out.write(ld::tracksCsv(result_, resultUnits_, chosenIds()).toUtf8()) < 0) {
        QMessageBox::warning(this, tr("Export"), tr("Cannot write %1").arg(f));
        return;
    }
    statusBar()->showMessage(tr("Table written to %1").arg(f), 5000);
}

void MainWindow::exportPlot() {
    if (!haveResult_ || isImage_) return;
    QSettings st;
    const QString def = QFileInfo(path_).completeBaseName() + QStringLiteral(".png");
    const QString f = QFileDialog::getSaveFileName(this, tr("Export plot"), QFileInfo(st.value("lastDir").toString(), def).filePath(),
                                                   tr("PNG image (*.png)"));
    if (f.isEmpty()) return;
    ld::SeriesSet set = ld::seriesOf(result_, resultUnits_);
    ld::selectDrops(&set, chosenIds());
    QImage img(1600, 1200, QImage::Format_RGB32);
    img.fill(Qt::white);
    QPainter p(&img);
    ld::PlotStyle ps;
    ps.sinceContact = plot_->sinceContact();
    ps.fontPx = 18;
    ld::drawSeries(p, img.rect(), set, ps);
    p.end();
    if (!img.save(f)) QMessageBox::warning(this, tr("Export"), tr("Cannot write %1").arg(f));
    else statusBar()->showMessage(tr("Plot written to %1").arg(f), 5000);
}

void MainWindow::closeEvent(QCloseEvent* e) {
    cancel();
    watcher_.waitForFinished();
    QThreadPool::globalInstance()->waitForDone();
    saveSettings();
    QSettings().setValue("window/geometry", saveGeometry());
    e->accept();
}

void MainWindow::dragEnterEvent(QDragEnterEvent* e) {
    if (e->mimeData()->hasUrls()) e->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* e) {
    const QList<QUrl> urls = e->mimeData()->urls();
    if (!urls.isEmpty() && urls[0].isLocalFile()) openFile(urls[0].toLocalFile());
}
