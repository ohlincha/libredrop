#include "mainwindow.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
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
#include <QStyle>
#include <QToolButton>
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
    file->addAction(tr("Save &compact copy…"), this, &MainWindow::saveCompact)
        ->setToolTip(tr("<p>Copy of the recording with only the band around the drops, in grey, as JPEG frames: typically "
                        "5–15× smaller, opens without ffmpeg, gives the same results.</p>"));
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
    playButton_ = new QToolButton;
    playButton_->setObjectName(QStringLiteral("play"));
    playButton_->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    playButton_->setToolTip(tr("Play / pause (Space)"));
    playButton_->setShortcut(Qt::Key_Space);
    playButton_->setEnabled(false);
    speed_ = new QComboBox;
    for (double f : {0.1, 0.25, 0.5, 1.0, 2.0, 4.0}) speed_->addItem(QStringLiteral("%1×").arg(f), f);
    speed_->setCurrentIndex(3);
    speed_->setToolTip(tr("Playback speed (1× = real time)"));
    auto* timeRow = new QHBoxLayout;
    timeRow->addWidget(playButton_);
    timeRow->addWidget(speed_);
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
    table_->setHeaderLabels({tr("Drop")});
    table_->setToolTip(tr("<p>Tick the drops that count: only those are plotted and exported.</p>"));
    table_->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table_->header()->setStretchLastSection(false);
    table_->setMinimumHeight(110);
    checks_ = new QLabel;
    checks_->setWordWrap(true);
    checks_->setTextFormat(Qt::RichText);
    checks_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    plot_ = new PlotWidget;
    sinceContact_ = new QCheckBox(tr("Time from each drop's contact with the surface (t = 0 at touch)"));

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
    split_ = split;
    setCentralWidget(split);
    resize(1500, 900);

    frameTimer_ = new QTimer(this);
    frameTimer_->setSingleShot(true);
    frameTimer_->setInterval(60);
    connect(frameTimer_, &QTimer::timeout, this, [this] { showFrame(slider_->value()); });
    connect(slider_, &QSlider::valueChanged, this, [this](int v) {
        timeLabel_->setText(tr("%1 s  (frame %2)").arg(frameTime(v), 0, 'f', 2).arg(v));
        if (playTimer_->isActive()) showFrame(v);   // (the latest request wins while a frame is being read)
        else frameTimer_->start();
    });
    playTimer_ = new QTimer(this);
    playTimer_->setInterval(15);
    connect(playTimer_, &QTimer::timeout, this, [this] {
        const double t = playFrom_ + playClock_.elapsed() / 1000.0 * speed_->currentData().toDouble();
        const int last = slider_->maximum();
        const int i = frameAt(t);
        if (i >= last || t > frameTime(last)) {
            slider_->setValue(last);
            setPlaying(false);
            return;
        }
        if (i != slider_->value()) slider_->setValue(i);
    });
    connect(playButton_, &QToolButton::clicked, this, [this] { setPlaying(!playTimer_->isActive()); });
    connect(speed_, &QComboBox::currentIndexChanged, this, [this] {
        if (playTimer_->isActive()) setPlaying(true);   // restart the clock from here
    });
    connect(slider_, &QSlider::sliderPressed, this, [this] { setPlaying(false); });
    connect(analyseButton_, &QPushButton::clicked, this, [this] {
        if (watcher_.isRunning() || compactWatcher_.isRunning()) cancel();
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
            if (table_->topLevelItem(i)->data(0, Qt::UserRole).toInt() == id) {
                table_->setCurrentItem(table_->topLevelItem(i));
                break;
            }
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
    setPlaying(false);
    playButton_->setEnabled(!isImage_);
    playButton_->setVisible(!isImage_);
    speed_->setVisible(!isImage_);
    plotBox_->setVisible(!isImage_);
    timeLabel_->setVisible(!isImage_);
    timeLabel_->setText(tr("%1 s  (frame %2)").arg(0.0, 0, 'f', 2).arg(0));
    analyseButton_->setEnabled(true);
    view_->resetView();
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
    // still at the start (often only the needle): show the first drop a second after it is free of the needle
    if (slider_->value() == 0 && !result_.tracks.empty() && !isImage_) {
        const ld::DropTrack* longest = &result_.tracks.front();
        for (const ld::DropTrack& t : result_.tracks)
            if (t.points.size() > longest->points.size()) longest = &t;
        const ld::SeriesSet set = ld::seriesOf(result_, resultUnits_);
        double t0 = longest->tContact();
        for (const ld::DropSeries& d : set.drops)
            if (d.id == longest->id && d.released() >= 0) t0 = d.t[std::size_t(d.released())];
        slider_->setValue(frameAt(std::min(t0 + 1.0, longest->points.back().t)));   // (shows the frame)
    } else {
        showFrame(slider_->value());
    }
}

void MainWindow::updateTable() {
    QSignalBlocker block(table_);
    table_->clear();
    const ld::SeriesSet set = ld::seriesOf(result_, resultUnits_);
    const QString L = set.lenUnit, V = set.volUnit == QLatin1String("uL") ? QStringLiteral("µL") : set.volUnit;
    // Same columns for an image and a video. A video has two lines per drop (at release from the needle; at the end)
    // plus contact time and frame count; units in the header, numbers in the cells.
    QStringList head{tr("Drop")};
    if (!isImage_) head << tr("Contact (s)") << QString();
    head << tr("Angle (°)") << tr("Left (°)") << tr("Right (°)") << tr("Base (%1)").arg(L) << tr("Height (%1)").arg(L)
         << tr("Volume (%1)").arg(V);
    if (!isImage_) head << tr("Frames");
    table_->setColumnCount(int(head.size()));
    table_->setHeaderLabels(head);
    const int c0 = isImage_ ? 1 : 3;   // first value column
    table_->headerItem()->setToolTip(c0, tr("Young–Laplace fit of the whole outline: the contact angle to use"));
    table_->headerItem()->setToolTip(c0 + 1, tr("Tangent at the left contact point (a check: should be close to Angle)"));
    table_->headerItem()->setToolTip(c0 + 2, tr("Tangent at the right contact point"));
    if (!isImage_) table_->headerItem()->setToolTip(1, tr("Time the drop touched the surface"));
    for (const ld::DropSeries& d : set.drops) {
        const ld::TrackSummary s = ld::summarize(d);
        auto* it = new QTreeWidgetItem(table_);
        it->setData(0, Qt::UserRole, d.id);
        it->setText(0, QString::number(d.id));
        it->setCheckState(0, Qt::Checked);
        QPixmap swatch(12, 12);
        swatch.fill(ld::dropColor(d.id));
        it->setIcon(0, QIcon(swatch));
        auto put = [&](QTreeWidgetItem* row, double ang, double l, double r, double b, double h, double v) {
            row->setText(c0, fmt(ang, 1));
            row->setText(c0 + 1, fmt(l, 1));
            row->setText(c0 + 2, fmt(r, 1));
            row->setText(c0 + 3, fmt(b, 3));
            row->setText(c0 + 4, fmt(h, 3));
            row->setText(c0 + 5, fmt(v, 3));
        };
        if (isImage_) {
            put(it, d.angle[0], d.left[0], d.right[0], d.base[0], d.height[0], d.volume[0]);
            continue;
        }
        it->setText(1, QString::number(s.tContact, 'f', 2));
        it->setText(9, s.failed ? tr("%1 (%2 not measured)").arg(s.points).arg(s.failed) : QString::number(s.points));
        if (s.tRelease < 0) {
            it->setText(2, tr("on needle"));
            it->setToolTip(2, tr("The needle stays in this drop: no values after release"));
            continue;
        }
        it->setText(2, tr("release"));
        it->setToolTip(2, tr("Free of the needle from %1 s (median of the first 5 frames after)").arg(s.tRelease, 0, 'f', 2));
        put(it, s.angleRelease, s.leftRelease, s.rightRelease, s.baseRelease, s.heightRelease, s.volumeRelease);
        auto* end = new QTreeWidgetItem(table_);
        end->setData(0, Qt::UserRole, d.id);
        end->setData(0, Qt::UserRole + 1, true);   // second line: no tick of its own
        end->setText(2, tr("end"));
        end->setToolTip(2, tr("At %1 s (median of the last 5 frames)").arg(s.tEnd, 0, 'f', 2));
        put(end, s.angleEnd, s.leftEnd, s.rightEnd, s.baseEnd, s.heightEnd, s.volumeEnd);
        end->setToolTip(c0, tr("Angle change after release: %1°/s").arg(fmt(s.angleRate, 3)));
        end->setToolTip(c0 + 3, tr("Largest base after release: %1 %2; change: %3 %2/s").arg(fmt(s.baseMax, 3), L, fmt(s.baseRate, 4)));
        end->setToolTip(c0 + 5, tr("Volume change after release: %1 %2/s").arg(fmt(s.volumeRate, 4), V));
    }
    view_->setChosen(chosenIds());
    fitTableWidth();
}

void MainWindow::fitTableWidth() {
    // the side panel is never narrower than the table's columns (the frame view gives way)
    if (!split_ || split_->count() < 2) return;
    QWidget* side = split_->widget(1);
    for (int c = 0; c < table_->columnCount(); ++c) table_->resizeColumnToContents(c);
    const int margins = side->width() - table_->viewport()->width();
    side->setMinimumWidth(std::max(0, table_->header()->length() + 4 + margins));
}

QList<int> MainWindow::chosenIds() const {
    QList<int> ids;
    for (int i = 0; i < table_->topLevelItemCount(); ++i)
        if (!table_->topLevelItem(i)->data(0, Qt::UserRole + 1).toBool() && table_->topLevelItem(i)->checkState(0) == Qt::Checked) ids << table_->topLevelItem(i)->data(0, Qt::UserRole).toInt();
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

void MainWindow::setPlaying(bool on) {
    if (on && slider_->value() >= slider_->maximum()) slider_->setValue(0);   // at the end: from the start
    if (on) {
        playFrom_ = frameTime(slider_->value());
        playClock_.restart();
        playTimer_->start();
    } else {
        playTimer_->stop();
    }
    playButton_->setIcon(style()->standardIcon(on ? QStyle::SP_MediaPause : QStyle::SP_MediaPlay));
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

void MainWindow::saveCompact() {
    if (path_.isEmpty() || isImage_ || compactWatcher_.isRunning()) return;
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Save compact copy"));
    auto* form = new QFormLayout(&dlg);
    auto* intro = new QLabel(tr("Keeps the band around the surface and the drops, in grey, with each frame as a JPEG at "
                                "the chosen quality and the original frame times. Quality 60–85 gives the same angles "
                                "and volumes as the original within about 1%."));
    intro->setWordWrap(true);
    form->addRow(intro);
    auto* quality = new QSpinBox;
    quality->setRange(30, 100);
    quality->setValue(QSettings().value("compactQuality", 85).toInt());
    form->addRow(tr("JPEG quality"), quality);
    auto* y0 = new QSpinBox, *y1 = new QSpinBox;
    y0->setRange(0, std::max(0, info_.h - 1));
    y1->setRange(0, std::max(0, info_.h - 1));
    int s0 = 0, s1 = info_.h - 1;
    if (haveResult_) ld::suggestCrop(result_, &s0, &s1);
    y0->setValue(s0);
    y1->setValue(s1);
    auto* rows = new QHBoxLayout;
    rows->addWidget(y0);
    rows->addWidget(new QLabel(tr("to")));
    rows->addWidget(y1);
    form->addRow(tr("Rows kept"), rows);
    if (!haveResult_) form->addRow(new QLabel(tr("(Analyse first to have the rows set to the drops.)")));
    auto* stepBox = new QSpinBox;
    stepBox->setRange(1, 1000);
    stepBox->setPrefix(tr("1 in "));
    stepBox->setSpecialValueText(tr("all"));
    form->addRow(tr("Frames"), stepBox);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted) return;
    QSettings().setValue("compactQuality", quality->value());
    const QFileInfo fi(path_);
    const QString out = QFileDialog::getSaveFileName(this, tr("Save compact copy"),
                                                     fi.dir().filePath(fi.completeBaseName() + tr("_compact.mkv")),
                                                     tr("Matroska video (*.mkv)"));
    if (out.isEmpty()) return;
    if (QFileInfo(out).canonicalFilePath() == fi.canonicalFilePath()) {
        QMessageBox::warning(this, tr("Save compact copy"), tr("Choose another file name than the recording's own."));
        return;
    }
    ld::CompactOptions co;
    co.quality = quality->value();
    co.y0 = std::min(y0->value(), y1->value()), co.y1 = std::max(y0->value(), y1->value());
    co.step = stepBox->value();
    cancel_ = std::make_shared<std::atomic<bool>>(false);
    co.cancel = cancel_.get();
    const auto cancelFlag = cancel_;
    const QString in = path_;
    QPointer<MainWindow> self(this);
    auto progress = [self](int done, int total) {
        QMetaObject::invokeMethod(qApp, [self, done, total] {
            if (!self) return;
            self->progress_->setMaximum(total);
            self->progress_->setValue(done);
        }, Qt::QueuedConnection);
    };
    setBusy(true);
    analyseButton_->setText(tr("Cancel"));
    statusBar()->showMessage(tr("Writing %1…").arg(QFileInfo(out).fileName()));
    disconnect(&compactWatcher_, nullptr, this, nullptr);
    connect(&compactWatcher_, &QFutureWatcher<QString>::finished, this, [this, out, in] {
        setBusy(false);
        const QString err = compactWatcher_.result();
        if (!err.isEmpty()) {
            statusBar()->showMessage(err == QLatin1String("cancelled") ? tr("Cancelled.") : tr("Not saved: %1").arg(err));
            return;
        }
        statusBar()->showMessage(tr("Saved %1: %2 MB (the recording: %3 MB).")
                                     .arg(QFileInfo(out).fileName())
                                     .arg(QFileInfo(out).size() / 1e6, 0, 'f', 1)
                                     .arg(QFileInfo(in).size() / 1e6, 0, 'f', 1));
    });
    compactWatcher_.setFuture(QtConcurrent::run([in, out, co, cancelFlag, progress] {
        QString err;
        return ld::writeCompact(in, out, co, progress, &err) ? QString() : err;
    }));
}

void MainWindow::closeEvent(QCloseEvent* e) {
    cancel();
    watcher_.waitForFinished();
    compactWatcher_.waitForFinished();
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
