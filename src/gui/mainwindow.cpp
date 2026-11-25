#include "mainwindow.h"

#include "comparedialog.h"
#include "library.h"
#include "measurewindow.h"
#include "projectdialogs.h"
#include "core/pendant.h"

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
#include <QScrollBar>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QThreadPool>
#include <QTimer>
#include <QDockWidget>
#include <QRegularExpression>
#include <QLineEdit>
#include <QTabWidget>
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
// 25 °C (PC: γ at 23 °C); sources in the project's liquid table (core/project.cpp, defaultLiquids)
const Liquid kLiquids[] = {
    {"Water", 997, 72.0},
    {"Dimethyl carbonate (DMC)", 1063, 28.1},
    {"Diethyl carbonate (DEC)", 969, 26.3},
    {"Propylene carbonate (PC)", 1200, 41.3},
    {"1 M LiPF6 in EC:DMC 1:1 (LP30)", 1303, 29.4},
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
    conditions_ = new QLabel;
    conditions_->setVisible(false);
    statusBar()->addPermanentWidget(conditions_);
    thermoTimer_ = new QTimer(this);
    thermoTimer_->setInterval(30000);
    connect(thermoTimer_, &QTimer::timeout, this, &MainWindow::readConditions);
    connect(&thermoWatcher_, &QFutureWatcher<std::vector<ld::ThermoReading>>::finished, this, &MainWindow::conditionsRead);
    if (!qEnvironmentVariableIsSet("LIBREDROP_NO_THERMO")) {
        thermoTimer_->start();
        QTimer::singleShot(500, this, &MainWindow::readConditions);
    }
    QSettings st;
    restoreGeometry(st.value("window/geometry").toByteArray());
    const QString last = st.value("project").toString();
    if (!last.isEmpty() && QFileInfo::exists(last + QStringLiteral("/project.json"))) openProject(last);
}

MainWindow::~MainWindow() = default;

void MainWindow::buildUi() {
    QMenu* file = menuBar()->addMenu(tr("&File"));
    file->addAction(tr("&Measure with the camera…"), QKeySequence(tr("Ctrl+M")), this, &MainWindow::openMeasure);
    file->addSeparator();
    file->addAction(tr("&Open video or image…"), QKeySequence::Open, this, &MainWindow::open);
    file->addSeparator();
    QAction* expTable = file->addAction(tr("Export &table (CSV)…"), this, &MainWindow::exportTable);
    file->addAction(tr("Export &plot (PNG)…"), this, &MainWindow::exportPlot);
    expTable->setShortcut(QKeySequence::Save);
    file->addAction(tr("Save &compact copy…"), this, &MainWindow::saveCompact)
        ->setToolTip(tr("<p>Copy of the recording with only the band around the drops, in grey, as JPEG frames: typically "
                        "5–15× smaller, opens without ffmpeg, gives the same results.</p>"));
    file->addAction(tr("Compare &samples…"), this, [this] { openCompare({}); });
    QMenu* proj = menuBar()->addMenu(tr("&Project"));
    proj->addAction(tr("&New project…"), this, &MainWindow::newProject);
    proj->addAction(tr("&Open project…"), this, [this] {
        const QString d = QFileDialog::getExistingDirectory(this, tr("Open project folder"));
        if (!d.isEmpty()) openProject(d);
    });
    proj->addAction(tr("&Liquids…"), this, [this] {
        if (!library_->hasProject()) return;
        if (editLiquids(this, library_->project())) library_->save();
    });
    proj->addAction(tr("&Calibration…"), this, [this] {
        if (!library_->hasProject()) return;
        auto needle = [this]() -> double {   // the needle's width in the frame at the slider (above the surface)
            if (path_.isEmpty() || isImage_) return ld::kNaN;
            ld::GrayImage g;
            QString err;
            if (!ld::readFrameAt(path_, info_, slider_->value(), &g, &err)) return ld::kNaN;
            const int bottom = haveResult_ ? int(result_.surface.p0.y) - 5 : g.h;
            const ld::PendantResult r = ld::analyzePendant(ld::crop(g, 0, 0, g.w, std::clamp(bottom, 20, g.h)), {});
            return r.needleWidthPx;
        };
        if (editCalibrations(this, library_->project(), info_.w, scale_->value(), needle)) {
            library_->save();
            applyProjectContext();
        }
    });
    proj->addAction(tr("&Add recordings…"), this, [this] {
        if (!library_->hasProject()) {
            QMessageBox::information(this, tr("Project"), tr("Open or make a project first (Project ▸ New project…)."));
            return;
        }
        library_->addRecordings(QFileDialog::getOpenFileNames(this, tr("Add recordings"), QString(),
                                                              tr("Videos (*.mkv *.avi *.mp4 *.mov);;All files (*)")));
    });
    file->addSeparator();
    file->addAction(tr("&Quit"), QKeySequence::Quit, this, &QWidget::close);
    QMenu* help = menuBar()->addMenu(tr("&Help"));
    help->addAction(tr("Open &example (water on glass)"), this, &MainWindow::openExample);
    help->addAction(tr("&Measuring separators…"), this, [this] {
        QMessageBox::information(this, tr("Measuring separators"), tr(
            "<p><b>1. Set up</b> (once per camera mode): the scale, and the liquid (Measurement ▸ Liquid).</p>"
            "<p><b>2. Glass first</b> (once per liquid and day): record a drop of the liquid on a clean glass slide for "
            "as long as you will record on the separators. Open it, analyse, then Soaking tab ▸ <i>Use as glass "
            "reference</i>. This is the evaporation correction; it is kept for that liquid.</p>"
            "<p>Record with <i>File ▸ Measure with the camera…</i> (Ctrl+M): fill in sample and liquid, press Record as the "
            "drop touches, Stop when it has settled or soaked in; the recording is filed in the project and analysed.</p>"
            "<p><b>3. Separators</b>: record each sample (3 drops or more; both sides and both directions, MD and CD, if "
            "they may differ). 640×480 at 120 fps for liquids that soak in within a second or two.</p>"
            "<p><b>4. Read</b> the Soaking tab: the angle at fixed times after contact (set the times under "
            "Measurement), the sorptivity (uptake per contact area, mm/√s) and when the drop was absorbed. "
            "Untick drops that are not usable (dust, a drop dragged by the needle). Export the table for each sample.</p>"
            "<p>The sorptivity is proportional to the scale: compare samples measured at the same scale.</p>"));
    });
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
    soakTimes_ = new QLineEdit(QStringLiteral("0.1, 1, 10"));
    soakTimes_->setToolTip(tr("Times after contact (s) at which the angle is reported in the Soaking tab"));
    evaporation_ = new QDoubleSpinBox;
    evaporation_->setRange(0, 10);
    evaporation_->setDecimals(4);
    evaporation_->setSingleStep(0.001);
    evaporation_->setSuffix(tr(" µL/s"));
    evaporation_->setToolTip(tr("<p>Volume lost by evaporation, measured with the same liquid on a non-absorbing surface "
                                "(glass): added back before the sorptivity is fitted.</p>"));
    analyseButton_ = new QPushButton(tr("Analyse"));
    analyseButton_->setObjectName(QStringLiteral("analyse"));
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
    auto* sr = new QHBoxLayout;
    sr->addWidget(new QLabel(tr("angle at")));
    sr->addWidget(soakTimes_, 1);
    sr->addWidget(new QLabel(tr("s; evaporation")));
    sr->addWidget(evaporation_);
    form->addRow(tr("Soaking"), sr);
    auto* ar = new QHBoxLayout;
    auto* measure = new QPushButton(tr("Measure…"));
    measure->setObjectName(QStringLiteral("measure"));
    measure->setToolTip(tr("Live camera: record a drop (Ctrl+M)"));
    connect(measure, &QPushButton::clicked, this, &MainWindow::openMeasure);
    ar->addWidget(measure);
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
    soakTable_ = new QTreeWidget;
    soakTable_->setRootIsDecorated(false);
    soakTable_->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    soakTable_->header()->setStretchLastSection(false);
    tabs_ = new QTabWidget;
    tabs_->addTab(table_, tr("Shape"));
    soakHint_ = new QLabel;
    soakHint_->setObjectName(QStringLiteral("soakHint"));
    soakHint_->setWordWrap(true);
    soakHint_->setTextFormat(Qt::RichText);
    glassRef_ = new QPushButton(tr("Use as glass reference"));
    glassRef_->setObjectName(QStringLiteral("glassRef"));
    glassRef_->setToolTip(tr("<p>Take the volume loss of the drops in this recording (a non-absorbing surface such as "
                             "glass, same liquid) as the evaporation rate for this liquid.</p>"));
    connect(glassRef_, &QPushButton::clicked, this, &MainWindow::useAsGlassReference);
    auto* soakPage = new QWidget;
    auto* sp = new QVBoxLayout(soakPage);
    sp->setContentsMargins(4, 4, 4, 0);
    sp->addWidget(soakHint_);   // full width (wraps to fewer lines); its height follows the text (event filter)
    soakHint_->installEventFilter(this);
    sp->addWidget(glassRef_, 0, Qt::AlignRight);
    soakTable_->setHeaderLabels({tr("Drop"), tr("θ at 0.1 s (°)"), tr("θ at 1 s (°)"), tr("θ at 10 s (°)"),
                                 tr("Sorptivity (mm/√s)"), tr("R²"), tr("Absorbed after (s)")});
    sp->addWidget(soakTable_, 1);
    tabs_->addTab(soakPage, tr("Soaking"));
    soakTable_->setMinimumHeight(100);   // header and a few drops, also in a short window
    table_->setMinimumHeight(100);
    tl->addWidget(tabs_, 1);
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
    rsplit->setChildrenCollapsible(false);
    rl->addWidget(rsplit, 1);

    auto* split = new QSplitter(Qt::Horizontal);
    split->addWidget(left);
    split->addWidget(right);
    split->setStretchFactor(0, 1);   // a wider window widens the image; the panel stays as wide as its table
    split->setStretchFactor(1, 0);
    connect(split, &QSplitter::splitterMoved, this, [this] { panelDragged_ = true; });
    split_ = split;
    setCentralWidget(split);
    library_ = new LibraryPanel;
    libraryDock_ = new QDockWidget(tr("Library"), this);
    libraryDock_->setObjectName(QStringLiteral("library"));
    libraryDock_->setWidget(library_);
    addDockWidget(Qt::LeftDockWidgetArea, libraryDock_);
    libraryDock_->hide();   // shown with a project
    connect(library_, &LibraryPanel::openRecording, this, &MainWindow::openFile);
    connect(library_, &LibraryPanel::compare, this, [this](const QStringList& p, const QStringList& n) { openCompare(p, n); });
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
        if (ld::Recording* rec = currentRecording()) {   // remember the unticked drops in the project
            QList<int> ex;
            for (int i = 0; i < table_->topLevelItemCount(); ++i) {
                QTreeWidgetItem* it = table_->topLevelItem(i);
                if (!it->data(0, Qt::UserRole + 1).toBool() && it->checkState(0) != Qt::Checked) ex << it->data(0, Qt::UserRole).toInt();
            }
            if (ex != rec->excluded) {
                rec->excluded = ex;
                library_->save();
            }
        }
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
    connect(soakTimes_, &QLineEdit::editingFinished, this, [this] {
        updateTable();
        saveSettings();
    });
    connect(evaporation_, &QDoubleSpinBox::editingFinished, this, [this] {
        evaporationSource_ = evaporation_->value() > 0 ? tr("entered") : QString();
        updateTable();
        saveSettings();
    });
    for (QWidget* w : std::initializer_list<QWidget*>{scale_, density_, tension_, step_})
        connect(qobject_cast<QAbstractSpinBox*>(w), &QAbstractSpinBox::editingFinished, this, &MainWindow::saveSettings);

    QSettings st;
    // by name (the list may grow); older settings kept the position
    const int byName = liquid_->findText(st.value("liquidName").toString());
    liquid_->setCurrentIndex(byName >= 0 ? byName : std::clamp(st.value("liquid", 0).toInt(), 0, liquid_->count() - 1));
    liquidChanged();
    if (liquid_->currentIndex() == liquid_->count() - 1) {
        density_->setValue(st.value("density", 1000).toDouble());
        tension_->setValue(st.value("tension", 50).toDouble());
    }
    scale_->setValue(st.value("scale", 94.23).toDouble());
    step_->setValue(st.value("step", 1).toInt());
    soakTimes_->setText(st.value("soakTimes", QStringLiteral("0.1, 1, 10")).toString());
    loadEvaporation();
    sinceContact_->setChecked(st.value("sinceContact", false).toBool());
    statusBar()->showMessage(tr("Open a recording to start."));
}

void MainWindow::openCompare(const QStringList& files, const QStringList& samples) {
    {
        if (!compare_) {
            CompareSettings cs;
            cs.options = [this](int w, const QString& path) {
                ld::VideoOptions o = options();
                const ld::VideoOptions defaults;
                o.minPoints = defaults.minPoints, o.minDuration = defaults.minDuration, o.step = step_->value();
                const QVariant saved = QSettings().value(QStringLiteral("scaleFor/%1").arg(w));
                o.scene.drop.pxPerMm = !saved.isNull() ? saved.toDouble() : defaultScale(w) > 0 ? defaultScale(w) : scale_->value();
                if (const ld::Recording* r = library_->recordingFor(path)) {   // from the library: its liquid, calibration
                    if (const ld::LiquidData* l = library_->project().liquid(r->liquid))
                        o.scene.drop.densityDiff = l->density, o.scene.drop.surfaceTension = l->tension;
                    const ld::Calibration c = library_->project().calibrationFor(w);
                    if (std::isfinite(c.pxPerMm)) o.scene.drop.pxPerMm = c.pxPerMm;
                }
                return o;
            };
            cs.soak = [this](const QString& path) {
                ld::SoakOptions so = soakOptions();
                if (const ld::Recording* r = library_->recordingFor(path)) {
                    const ld::EvaporationRef e = library_->project().evaporationFor(r->liquid, r->date);
                    so.evaporation = std::isfinite(e.rate) ? e.rate : 0;
                }
                return so;
            };
            cs.describe = [this] {
                if (library_->hasProject())
                    return tr("Project <b>%1</b>: liquid, scale and evaporation correction come from the library for each "
                              "recording (the glass reference of the same day, else the latest before it).")
                        .arg(library_->project().name.toHtmlEscaped());
                return tr("Liquid: <b>%1</b> · evaporation correction: %2 · scale: as set for each camera mode")
                    .arg(liquid_->currentText().toHtmlEscaped(),
                         evaporation_->value() > 0 ? tr("%1 µL/s").arg(evaporation_->value(), 0, 'f', 4)
                                                   : tr("<span style='color:#b07800'><b>none</b> (set it in the Soaking tab "
                                                        "from a glass recording first)</span>"));
            };
            compare_ = new CompareDialog(cs, this);
            compare_->setAttribute(Qt::WA_DeleteOnClose, false);
        }
        compare_->show();
        compare_->raise();
        if (!files.isEmpty()) compare_->addFiles(files, samples);
    }
}

void MainWindow::openMeasure() {
    if (!measure_) {
        MeasureContext c;
        c.sceneOptions = [this](int w) {
            ld::SceneOptions o;
            double sc = defaultScale(w);
            if (const QVariant v = QSettings().value(QStringLiteral("scaleFor/%1").arg(w)); !v.isNull()) sc = v.toDouble();
            if (library_->hasProject())
                if (const ld::Calibration cal = library_->project().calibrationFor(w); std::isfinite(cal.pxPerMm)) sc = cal.pxPerMm;
            o.drop.pxPerMm = sc;
            o.drop.densityDiff = density_->value();
            o.drop.surfaceTension = tension_->value();
            return o;
        };
        c.recordingsDir = [this] {
            return library_->hasProject() ? QDir(library_->project().dir).filePath(QStringLiteral("recordings")) : QString();
        };
        c.samples = [this] {
            QStringList n;
            if (library_->hasProject())
                for (const ld::Sample& s : library_->project().samples) n << s.name;
            return n;
        };
        for (int i = 0; i < liquid_->count(); ++i)
            if (liquid_->itemText(i) != tr("Custom")) c.liquids << liquid_->itemText(i);
        c.recorded = [this](const QString& path, bool open) {
            if (library_->hasProject()) {
                QString err;
                if (library_->project().addRecording(path, false, nullptr, &err)) library_->save();
                else statusBar()->showMessage(tr("Not added to the project: %1").arg(err), 10000);
                library_->refresh();
            }
            if (open) {
                openFile(path);
                raise();
                activateWindow();
            }
        };
        measure_ = new MeasureWindow(std::move(c), this);
        measure_->show();
    } else {
        measure_->refreshSamples();
        if (measure_->isHidden()) {
            measure_->show();
            measure_->listCameras();
        }
    }
    measure_->raise();
    measure_->activateWindow();
}

bool MainWindow::openProject(const QString& dir) {
    ld::Project p;
    QString err;
    if (!ld::Project::load(dir, &p, &err)) {
        QMessageBox::warning(this, tr("Open project"), err);
        return false;
    }
    library_->setProject(std::move(p));
    libraryDock_->show();
    QSettings().setValue("project", dir);
    setWindowTitle(tr("LibreDrop — %1").arg(library_->project().name));
    if (measure_) measure_->refreshSamples();
    return true;
}

void MainWindow::newProject() {
    const QString dir = QFileDialog::getSaveFileName(this, tr("New project: folder name"), QString(), QString(), nullptr,
                                                     QFileDialog::ShowDirsOnly);
    if (dir.isEmpty()) return;
    ld::Project p;
    QString err;
    if (!ld::Project::create(dir, QFileInfo(dir).fileName(), &p, &err)) {
        QMessageBox::warning(this, tr("New project"), err);
        return;
    }
    openProject(dir);
    statusBar()->showMessage(tr("Project made in %1. Add recordings with Project ▸ Add recordings… or the Add button.").arg(dir), 10000);
}

ld::Recording* MainWindow::currentRecording() {
    return library_ && library_->hasProject() && !path_.isEmpty() ? library_->recordingFor(path_) : nullptr;
}

void MainWindow::applyProjectContext() {
    ld::Recording* rec = currentRecording();
    if (!rec) return;
    if (library_->project().fillConditions(*rec)) library_->save();
    const ld::Project& pr = library_->project();
    if (!rec->liquid.isEmpty()) {   // the liquid list entry with this name (or its abbreviation in brackets)
        for (int i = 0; i < liquid_->count(); ++i) {
            const QString t = liquid_->itemText(i);
            if (t.compare(rec->liquid, Qt::CaseInsensitive) == 0 ||
                t.contains(QStringLiteral("(%1)").arg(rec->liquid), Qt::CaseInsensitive)) {
                liquid_->setCurrentIndex(i);
                break;
            }
        }
    }
    if (const ld::LiquidData* l = pr.liquid(rec->liquid); l && std::isfinite(l->density) && std::isfinite(l->tension)) {
        density_->setValue(l->density);
        tension_->setValue(l->tension);
    }
    const ld::Calibration c = pr.calibrationFor(info_.w);
    if (std::isfinite(c.pxPerMm)) scale_->setValue(c.pxPerMm);
    const ld::EvaporationRef e = pr.evaporationFor(rec->liquid, rec->date);
    evaporation_->setValue(std::isfinite(e.rate) ? e.rate : 0);
    evaporationSource_ = std::isfinite(e.rate) ? tr("project: %1 on %2").arg(QFileInfo(e.recording).fileName(), e.date) : QString();
    updateSoakHint();
}

void MainWindow::readConditions() {
    if (!thermoWatcher_.isRunning()) thermoWatcher_.setFuture(QtConcurrent::run([] { return ld::readThermometers(); }));
}

void MainWindow::conditionsRead() {
    const std::vector<ld::ThermoReading> all = thermoWatcher_.result();
    if (all.empty()) {
        conditions_->setVisible(false);
        return;
    }
    const ld::ThermoReading room = ld::roomConditions(all);
    QStringList tip;
    for (const ld::ThermoReading& r : all)
        tip << QStringLiteral("%1 (%2): %3").arg(r.name.isEmpty() ? r.model : r.name, r.location,
                                                 r.error.isEmpty() ? QStringLiteral("%1 °C%2").arg(r.temperature, 0, 'f', 2)
                                                                         .arg(std::isfinite(r.humidity) ? QStringLiteral(", %1 %RH").arg(r.humidity, 0, 'f', 1) : QString())
                                                                   : r.error.toHtmlEscaped());
    conditions_->setToolTip(tr("<p>Room conditions from USB thermometers (names and offsets from USB Thermo Logger). "
                               "Logged into the open project every minute and noted with each recording.</p>") + tip.join(QStringLiteral("<br>")));
    if (std::isfinite(room.temperature))
        conditions_->setText(QStringLiteral("🌡 %1 °C%2 ").arg(room.temperature, 0, 'f', 1)
                                 .arg(std::isfinite(room.humidity) ? QStringLiteral(" · %1 %RH").arg(room.humidity, 0, 'f', 0) : QString()));
    else
        conditions_->setText(tr("🌡 sensor: %1 ").arg(all.front().error.left(40)));
    conditions_->setVisible(true);
    if (library_->hasProject() && std::isfinite(room.temperature) &&
        (!lastConditionsLog_.isValid() || lastConditionsLog_.secsTo(QDateTime::currentDateTime()) >= 55)) {
        library_->project().logConditions(room.name.isEmpty() ? room.model : room.name, room.temperature, room.humidity);
        lastConditionsLog_ = QDateTime::currentDateTime();
    }
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
    loadEvaporation();   // evaporation is stored per liquid
    saveSettings();
    if (haveResult_) updateTable();
    else updateSoakHint();
}

void MainWindow::loadEvaporation() {
    QSettings st;
    const int i = liquid_->currentIndex();
    const QString key = liquid_->itemText(i).toHtmlEscaped().replace(QLatin1Char('/'), QLatin1Char('-'));
    evaporation_->setValue(st.value(QStringLiteral("evaporationOf/%1").arg(key), st.value(QStringLiteral("evaporation/%1").arg(i), 0.0)).toDouble());
    evaporationSource_ = st.value(QStringLiteral("evaporationSourceOf/%1").arg(key), st.value(QStringLiteral("evaporationSource/%1").arg(i))).toString();
}

void MainWindow::updateSoakHint() {
    const QString liquid = liquid_->currentText();
    QString t;
    if (evaporation_->value() <= 0) {
        t = tr("<b>No evaporation correction for %1 yet.</b> Do this first: record a drop of the same liquid on a "
               "non-absorbing surface (a clean glass slide), open and analyse it, then press <i>Use as glass "
               "reference</i>. Or enter a known rate under Measurement ▸ Soaking. Until then the sorptivity includes "
               "evaporation.").arg(liquid.toHtmlEscaped());
    } else {
        t = tr("Evaporation correction for %1: <b>%2 µL/s</b>%3. Change the liquid to use its own value.")
                .arg(liquid.toHtmlEscaped()).arg(evaporation_->value(), 0, 'f', 4)
                .arg(evaporationSource_.isEmpty() ? QString() : tr(" (%1)").arg(evaporationSource_.toHtmlEscaped()));
    }
    bool differ = false;
    if (const ld::Recording* rec = currentRecording(); rec && evaporation_->value() > 0) {   // same conditions as the reference?
        const ld::EvaporationRef e = library_->project().evaporationFor(rec->liquid, rec->date);
        auto cond = [](double T, double H) {
            return std::isfinite(T) ? QStringLiteral("%1 °C%2").arg(T, 0, 'f', 1)
                                          .arg(std::isfinite(H) ? QStringLiteral(", %1 %RH").arg(H, 0, 'f', 0) : QString())
                                    : tr("not recorded");
        };
        if (std::isfinite(rec->temperature) || std::isfinite(e.temperature)) {
            differ = std::isfinite(rec->temperature) && std::isfinite(e.temperature) &&
                     (std::abs(rec->temperature - e.temperature) > 2 ||
                      (std::isfinite(rec->humidity) && std::isfinite(e.humidity) && std::abs(rec->humidity - e.humidity) > 10));
            t += tr("<br>Conditions: this recording %1; glass reference %2.%3")
                     .arg(cond(rec->temperature, rec->humidity), cond(e.temperature, e.humidity),
                          differ ? tr(" <b>They differ: the evaporation correction may not fit; measure glass again.</b>") : QString());
        }
    }
    soakHint_->setText(t);
    soakHint_->setProperty("state", evaporation_->value() <= 0 || differ ? "todo" : "ok");
    soakHint_->style()->unpolish(soakHint_);   // (re-apply the style sheet for the new state)
    soakHint_->style()->polish(soakHint_);
    glassRef_->setEnabled(haveResult_ && !isImage_);
}

void MainWindow::useAsGlassReference() {
    if (!haveResult_ || isImage_) return;
    if (!(scale_->value() > 0)) {
        QMessageBox::information(this, tr("Glass reference"), tr("Set the scale first: the evaporation rate is a volume per second."));
        return;
    }
    const ld::SeriesSet set = ld::seriesOf(result_, resultUnits_);
    const QList<int> chosen = chosenIds();
    std::vector<double> rates;
    bool absorbed = false;
    for (const ld::DropSeries& d : set.drops) {
        if (!chosen.contains(d.id)) continue;
        const ld::TrackSummary s = ld::summarize(d);
        if (std::isfinite(s.volumeRate)) rates.push_back(-s.volumeRate);
        absorbed |= std::isfinite(ld::soak(d, soakOptions()).absorbedAfter);
    }
    if (rates.empty()) {
        QMessageBox::information(this, tr("Glass reference"), tr("No drop in this recording has a measured volume after release."));
        return;
    }
    std::sort(rates.begin(), rates.end());
    const double e = rates[rates.size() / 2];
    if (absorbed || e <= 0) {
        QMessageBox::warning(this, tr("Glass reference"),
                             absorbed ? tr("A drop disappeared during this recording: this does not look like a non-absorbing "
                                           "surface. The value was not used.")
                                      : tr("The drops did not lose volume (%1 µL/s). The value was not used.").arg(-e, 0, 'g', 3));
        return;
    }
    evaporation_->setValue(e);
    evaporationSource_ = tr("from %1").arg(QFileInfo(path_).fileName());
    if (ld::Recording* rec = currentRecording()) {   // in the project: for this liquid on this day
        rec->glassReference = true;
        ld::Project& pr = library_->project();
        const QString liquid = rec->liquid.isEmpty() ? liquid_->currentText() : rec->liquid;
        pr.evaporation.erase(std::remove_if(pr.evaporation.begin(), pr.evaporation.end(), [&](const ld::EvaporationRef& x) {
            return x.liquid.compare(liquid, Qt::CaseInsensitive) == 0 && x.date == rec->date;
        }), pr.evaporation.end());
        pr.evaporation.push_back({liquid, rec->date, e, rec->file, rec->temperature, rec->humidity});
        library_->save();
        evaporationSource_ = tr("project: %1 on %2").arg(QFileInfo(rec->file).fileName(), rec->date);
    }
    saveSettings();
    updateTable();
    statusBar()->showMessage(tr("Evaporation for %1 set to %2 µL/s.").arg(liquid_->currentText()).arg(e, 0, 'f', 4), 8000);
}

void MainWindow::saveSettings() const {
    QSettings st;
    st.setValue("liquid", liquid_->currentIndex());
    st.setValue("liquidName", liquid_->currentText());
    st.setValue("density", density_->value());
    st.setValue("tension", tension_->value());
    st.setValue("scale", scale_->value());
    st.setValue("step", step_->value());
    st.setValue("soakTimes", soakTimes_->text());
    const QString key = liquid_->currentText().toHtmlEscaped().replace(QLatin1Char('/'), QLatin1Char('-'));
    st.setValue(QStringLiteral("evaporationOf/%1").arg(key), evaporation_->value());
    st.setValue(QStringLiteral("evaporationSourceOf/%1").arg(key), evaporationSource_);
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
    applyProjectContext();
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
    if (ld::Recording* rec = currentRecording()) {   // the library's ticks and the cached results
        QSignalBlocker b(table_);
        for (int i = 0; i < table_->topLevelItemCount(); ++i) {
            QTreeWidgetItem* it = table_->topLevelItem(i);
            if (rec->excluded.contains(it->data(0, Qt::UserRole).toInt()) && !it->data(0, Qt::UserRole + 1).toBool())
                it->setCheckState(0, Qt::Unchecked);
        }
        QFile f(library_->project().resultPath(*rec));
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(ld::tracksCsv(result_, resultUnits_).toUtf8());
    }
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
    // soaking tab (videos only)
    updateSoakHint();
    soakTable_->clear();
    tabs_->setTabVisible(1, !isImage_);
    if (!isImage_) {
        const ld::SoakOptions so = soakOptions();
        QStringList sh{tr("Drop")};
        for (double t : so.times) sh << tr("θ at %1 s (°)").arg(t);
        sh << tr("Sorptivity (%1/√s)").arg(L) << tr("R²") << tr("Absorbed after (s)");
        soakTable_->setColumnCount(int(sh.size()));
        soakTable_->setHeaderLabels(sh);
        const int cs = 1 + int(so.times.size());
        soakTable_->headerItem()->setToolTip(cs, tr("<p>Uptake per contact area: absorbed volume = S · area · √t "
                                                    "(after release, evaporation added back). Glass: should be ≈ 0.</p>"));
        soakTable_->headerItem()->setToolTip(cs + 1, tr("How well volume follows S·∫A d√t (1 = exactly)"));
        soakTable_->headerItem()->setToolTip(cs + 2, tr("Time from contact until the drop disappeared; empty: still there at the end"));
        for (const ld::DropSeries& d : set.drops) {
            const ld::Soaking k = ld::soak(d, so);
            auto* it = new QTreeWidgetItem(soakTable_);
            it->setText(0, QString::number(d.id));
            QPixmap swatch(12, 12);
            swatch.fill(ld::dropColor(d.id));
            it->setIcon(0, QIcon(swatch));
            for (std::size_t i = 0; i < so.times.size(); ++i) it->setText(1 + int(i), fmt(k.angleAt[i], 1));
            it->setText(cs, std::isfinite(k.sorptivity) ? QString::number(k.sorptivity, 'g', 3) : tr("—"));
            it->setText(cs + 1, fmt(k.sorptivityR2, 3));
            it->setText(cs + 2, std::isfinite(k.absorbedAfter) ? fmt(k.absorbedAfter, 2) : QString());
        }
    }
    view_->setChosen(chosenIds());
    fitTableWidth();
}

ld::SoakOptions MainWindow::soakOptions() const {
    ld::SoakOptions so;
    std::vector<double> times;
    for (const QString& x : soakTimes_->text().split(QRegularExpression(QStringLiteral("[,;\\s]+")), Qt::SkipEmptyParts)) {
        bool ok = false;
        const double t = x.toDouble(&ok);
        if (ok && t >= 0) times.push_back(t);
    }
    if (!times.empty()) so.times = times;
    so.evaporation = evaporation_->value();
    so.videoEnd = info_.times.empty() ? ld::kNaN : info_.times.back();
    return so;
}

void MainWindow::fitTableWidth() {
    // the side panel is never narrower than the table's columns (the frame view gives way)
    if (!split_ || split_->count() < 2) return;
    QWidget* side = split_->widget(1);
    for (int c = 0; c < table_->columnCount(); ++c) table_->resizeColumnToContents(c);
    // measured from the widgets around the table, not its viewport: that is tiny while another tab is in front
    // (it used to make the panel a little wider after every analysis)
    const int table = std::max(table_->header()->length(), soakTable_->header()->length()) + 2 * table_->frameWidth() +
                      table_->verticalScrollBar()->sizeHint().width() + 8;   // + the tab page's margins
    const int around = side->width() - tabs_->width();                        // the panel's own margins
    const int need = std::max(0, table + std::max(0, around));
    side->setMinimumWidth(need);
    if (!panelDragged_ && side->width() > need + 60) {   // give the rest to the image (until the user drags the divider)
        const int total = split_->sizes().value(0) + split_->sizes().value(1);
        split_->setSizes({total - need - 20, need + 20});
    }
}

bool MainWindow::eventFilter(QObject* o, QEvent* e) {
    if (o == soakHint_ && e->type() == QEvent::Resize)   // a wrapped label is not given its height by the tab widget
        soakHint_->setMinimumHeight(soakHint_->heightForWidth(soakHint_->width()));
    return QMainWindow::eventFilter(o, e);
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
