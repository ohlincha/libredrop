#include "measurewindow.h"

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QStyle>
#include <QSettings>
#include <QShortcut>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardPaths>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <cmath>

#include "core/image.h"
#include "frameview.h"

// ---------------------------------------------------------------------------------------------------------------
CameraThread::CameraThread(std::unique_ptr<ld::Camera> cam, const ld::CameraMode& mode, int exposure100us, double preSeconds)
    : cam_(std::move(cam)), mode_(mode), name_(cam_->name()), exposure_(exposure100us) {
    rec_.preSeconds = preSeconds;
}

CameraThread::~CameraThread() { finish(); }

void CameraThread::finish() {
    quit_ = true;
    wait();
}

bool CameraThread::takeLatest(ld::CameraFrame* f) {
    std::lock_guard<std::mutex> l(m_);
    if (!haveLatest_) return false;
    *f = std::move(latest_);
    haveLatest_ = false;
    return true;
}

void CameraThread::startRecording(const QString& path, const QJsonObject& meta, double maxSeconds) {
    std::lock_guard<std::mutex> l(m_);
    wantStart_ = true, wantStop_ = false;
    path_ = path, meta_ = meta, maxSeconds_ = maxSeconds;
}

void CameraThread::stopRecording() {
    std::lock_guard<std::mutex> l(m_);
    wantStop_ = true;
}

void CameraThread::setExposure(int e) {
    exposure_ = e;
    exposureChanged_ = true;
}

QJsonObject CameraThread::cameraSettings() const {
    std::lock_guard<std::mutex> l(m_);
    return settings_;
}

void CameraThread::run() {
    QString err;
    if (!cam_->start(mode_, &err)) {
        emit failed(err);
        return;
    }
    cam_->applyOssilaSettings(exposure_);   // after starting: opening the stream can reset the controls
    {
        std::lock_guard<std::mutex> l(m_);
        mode_ = cam_->mode();
        for (const auto& [k, v] : cam_->settings()) settings_[k] = v;
    }
    std::vector<double> recent;   // frame times in the last second
    double lastProgress = -1;
    ld::CameraFrame f;
    while (!quit_) {
        if (exposureChanged_.exchange(false)) {
            cam_->setControl(QStringLiteral("exposure_time_absolute"), exposure_);
            std::lock_guard<std::mutex> l(m_);
            settings_[QStringLiteral("exposure_time_absolute")] = exposure_.load();
        }
        if (!cam_->grab(&f, &err)) {
            if (rec_.recording()) {
                QString e2;
                rec_.stop(&e2);
                emit recordingStopped(rec_.path(), rec_.frames(), rec_.duration(), rec_.fps(), rec_.dropped(), err);
            }
            emit failed(err);
            cam_->stop();
            return;
        }
        recent.push_back(f.t);
        while (recent.size() > 2 && recent.back() - recent.front() > 1.0) recent.erase(recent.begin());
        if (recent.size() > 1) fps_ = (recent.size() - 1) / std::max(1e-6, recent.back() - recent.front());

        bool start = false, stop = false;
        QString path;
        QJsonObject meta;
        double maxS = 0;
        {
            std::lock_guard<std::mutex> l(m_);
            std::swap(start, wantStart_);
            std::swap(stop, wantStop_);
            path = path_, meta = meta_, maxS = maxSeconds_;
        }
        if (start) {
            QString e;
            if (rec_.start(path, mode_.w, mode_.h, meta, &e)) {
                lastProgress = -1;
                emit recordingStarted();
            } else {
                emit recordingStopped(path, 0, 0, 0, 0, e);
            }
        }
        QString e;
        const bool ok = rec_.push(f, &e);
        if (rec_.recording()) {
            const double d = rec_.duration();
            if (d - lastProgress >= 0.2) {
                lastProgress = d;
                emit recordingProgress(d, rec_.frames(), rec_.dropped());
            }
            if (!ok || stop || (maxS > 0 && d >= maxS)) {
                QString e2;
                rec_.stop(&e2);
                emit recordingStopped(rec_.path(), rec_.frames(), rec_.duration(), rec_.fps(), rec_.dropped(), ok ? e2 : e);
            }
        }
        bool notify;
        {
            std::lock_guard<std::mutex> l(m_);
            notify = !haveLatest_;
            latest_ = f;
            haveLatest_ = true;
        }
        if (notify) emit frameReady();
    }
    if (rec_.recording()) {
        QString e2;
        rec_.stop(&e2);
        emit recordingStopped(rec_.path(), rec_.frames(), rec_.duration(), rec_.fps(), rec_.dropped(), e2);
    }
    cam_->stop();
}

// ---------------------------------------------------------------------------------------------------------------
namespace {

QString shortLiquid(const QString& name) {   // "Dimethyl carbonate (DMC)" → DMC
    const int a = name.lastIndexOf(QLatin1Char('(')), b = name.lastIndexOf(QLatin1Char(')'));
    return a >= 0 && b > a + 1 ? name.mid(a + 1, b - a - 1) : name;
}

QComboBox* editableCombo(const QStringList& items) {
    auto* c = new QComboBox;
    c->setEditable(true);
    c->addItems(items);
    c->setInsertPolicy(QComboBox::NoInsert);
    return c;
}

}  // namespace

MeasureWindow::MeasureWindow(MeasureContext ctx, QWidget* parent) : QWidget(parent, Qt::Window), ctx_(std::move(ctx)) {
    setWindowTitle(tr("Measure – LibreDrop"));
    QSettings st;

    view_ = new FrameView;
    view_->setMessage(tr("Choose a camera"));

    camera_ = new QComboBox;
    auto* refresh = new QPushButton(tr("Look again"));
    refresh->setToolTip(tr("List the cameras again (after plugging one in)"));
    mode_ = new QComboBox;
    exposure_ = new QDoubleSpinBox;
    exposure_->setRange(0.1, 500);
    exposure_->setDecimals(1);
    exposure_->setSingleStep(0.1);
    exposure_->setSuffix(tr(" ms"));
    exposure_->setValue(st.value(QStringLiteral("measure/exposureMs"), 3.9).toDouble());
    exposure_->setToolTip(tr("<p>Exposure time. 3.9 ms is Ossila's setting for the goniometer with its backlight on. "
                             "Raise it if the image is dark; it must be shorter than one frame (8 ms at 120 fps).</p>"
                             "<p>The other camera settings are Ossila's (gain 0, gamma 72, contrast 32, …).</p>"));
    live_ = new QCheckBox(tr("Find drops in the live image"));
    live_->setChecked(st.value(QStringLiteral("measure/live"), true).toBool());
    live_->setToolTip(tr("Show the surface, the drop outline and the angle while you set up (slows the live view a little)."));

    auto* camBox = new QGroupBox(tr("Camera"));
    auto* camForm = new QFormLayout(camBox);
    auto* camRow = new QHBoxLayout;
    camera_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    camera_->setMinimumContentsLength(16);
    camRow->addWidget(camera_, 1);
    camRow->addWidget(refresh);
    camForm->addRow(tr("Camera"), camRow);
    camForm->addRow(tr("Size and rate"), mode_);
    camForm->addRow(tr("Exposure"), exposure_);
    camForm->addRow(live_);

    sample_ = editableCombo({});
    sample_->setToolTip(tr("Sample name, e.g. kraft-A. Recordings are filed under it in the project."));
    side_ = editableCombo({QString(), QStringLiteral("front"), QStringLiteral("back")});
    direction_ = editableCombo({QString(), QStringLiteral("MD"), QStringLiteral("CD")});
    direction_->setToolTip(tr("Machine direction (MD) or cross direction (CD) of a paper or film, if it matters"));
    liquid_ = editableCombo(ctx_.liquids);
    volume_ = new QDoubleSpinBox;
    volume_->setRange(0, 100);
    volume_->setDecimals(1);
    volume_->setSuffix(tr(" µL"));
    volume_->setSpecialValueText(tr("not noted"));
    control_ = new QCheckBox(tr("Control or reference sample (glass, PTFE, …)"));
    note_ = new QLineEdit;
    note_->setPlaceholderText(tr("anything worth keeping with the recording"));
    side_->setCurrentText(st.value(QStringLiteral("measure/side")).toString());
    direction_->setCurrentText(st.value(QStringLiteral("measure/direction")).toString());
    liquid_->setCurrentText(st.value(QStringLiteral("measure/liquid"), ctx_.liquids.value(0)).toString());
    volume_->setValue(st.value(QStringLiteral("measure/volume"), 0).toDouble());

    auto* sampleBox = new QGroupBox(tr("Sample"));
    auto* sForm = new QFormLayout(sampleBox);
    sForm->addRow(tr("Sample"), sample_);
    sForm->addRow(tr("Side"), side_);
    sForm->addRow(tr("Direction"), direction_);
    sForm->addRow(tr("Liquid"), liquid_);
    sForm->addRow(tr("Volume"), volume_);
    sForm->addRow(control_);
    sForm->addRow(tr("Note"), note_);

    pre_ = new QDoubleSpinBox;
    pre_->setRange(0, 10);
    pre_->setDecimals(1);
    pre_->setSuffix(tr(" s"));
    pre_->setValue(st.value(QStringLiteral("measure/preSeconds"), 2.0).toDouble());
    pre_->setToolTip(tr("Frames from before Record was pressed that go into the video: press Record as the drop touches "
                        "and the contact is still recorded."));
    maxSeconds_ = new QSpinBox;
    maxSeconds_->setRange(0, 3600);
    maxSeconds_->setSuffix(tr(" s"));
    maxSeconds_->setSpecialValueText(tr("when I press Stop"));
    maxSeconds_->setValue(st.value(QStringLiteral("measure/maxSeconds"), 0).toInt());
    folder_ = new QLabel;
    folder_->setMinimumWidth(120);
    folderButton_ = new QPushButton(tr("Folder…"));
    chosenDir_ = st.value(QStringLiteral("measure/folder"),
                          QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).filePath(QStringLiteral("LibreDrop")))
                     .toString();
    analyse_ = new QCheckBox(tr("Open and analyse when stopped"));
    analyse_->setChecked(st.value(QStringLiteral("measure/analyse"), true).toBool());
    record_ = new QPushButton(tr("● Record"));
    record_->setObjectName(QStringLiteral("record"));
    record_->setToolTip(tr("Start or stop recording (Ctrl+R)"));
    record_->setMinimumHeight(40);
    record_->setEnabled(false);

    auto* recBox = new QGroupBox(tr("Recording"));
    auto* rForm = new QFormLayout(recBox);
    rForm->addRow(tr("Keep from before"), pre_);
    rForm->addRow(tr("Stop"), maxSeconds_);
    auto* fRow = new QHBoxLayout;
    fRow->addWidget(folder_, 1);
    fRow->addWidget(folderButton_);
    rForm->addRow(tr("Save in"), fRow);
    rForm->addRow(analyse_);
    rForm->addRow(record_);

    readout_ = new QLabel;
    readout_->setObjectName(QStringLiteral("readout"));
    readout_->setTextFormat(Qt::RichText);
    status_ = new QLabel;
    status_->setObjectName(QStringLiteral("soakHint"));   // the amber/green hint style
    status_->setWordWrap(true);
    auto* help = new QLabel(tr("<p><b>How to measure</b><br>1. Backlight on, lens cap off; sample on the stage.<br>"
                               "2. Fill in the sample and liquid.<br>3. Bring the drop down; press <b>Record</b> as it "
                               "touches (the last %1 s before are kept).<br>4. Press <b>Stop</b> when the drop has "
                               "settled or soaked in.</p>")
                                .arg(QStringLiteral("2")));
    help->setWordWrap(true);
    connect(pre_, &QDoubleSpinBox::valueChanged, help, [help, this] {
        help->setText(help->text().replace(QRegularExpression(QStringLiteral("last [0-9.]+ s")),
                                           QStringLiteral("last %1 s").arg(pre_->value())));
    });

    auto* side = new QWidget;
    auto* sl = new QVBoxLayout(side);
    sl->addWidget(camBox);
    sl->addWidget(sampleBox);
    sl->addWidget(recBox);
    sl->addWidget(status_);
    sl->addWidget(readout_);
    sl->addWidget(help);
    sl->addStretch(1);
    side->setMinimumWidth(340);

    auto* split = new QSplitter;
    split->addWidget(view_);
    split->addWidget(side);
    split->setStretchFactor(0, 1);
    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);
    lay->addWidget(split);
    resize(1300, 760);
    restoreGeometry(st.value(QStringLiteral("measure/geometry")).toByteArray());

    connect(refresh, &QPushButton::clicked, this, &MeasureWindow::listCameras);
    connect(camera_, &QComboBox::activated, this, &MeasureWindow::cameraChosen);
    connect(mode_, &QComboBox::activated, this, &MeasureWindow::startCamera);
    connect(exposure_, &QDoubleSpinBox::valueChanged, this, [this](double ms) {
        if (thread_) thread_->setExposure(std::max(1, int(std::lround(ms * 10))));
    });
    connect(folderButton_, &QPushButton::clicked, this, [this] {
        const QString d = QFileDialog::getExistingDirectory(this, tr("Save recordings in"), chosenDir_);
        if (!d.isEmpty()) chosenDir_ = d;
        refreshSamples();
    });
    connect(record_, &QPushButton::clicked, this, &MeasureWindow::record);
    auto* sc = new QShortcut(QKeySequence(tr("Ctrl+R")), this);
    connect(sc, &QShortcut::activated, this, &MeasureWindow::record);
    connect(&preview_, &QFutureWatcher<Preview>::finished, this, &MeasureWindow::previewDone);

    refreshSamples();
    listCameras();
}

MeasureWindow::~MeasureWindow() {
    stopCamera();
    preview_.waitForFinished();
}

void MeasureWindow::refreshSamples() {
    const QString cur = sample_->currentText();
    sample_->clear();
    if (ctx_.samples) sample_->addItems(ctx_.samples());
    sample_->setCurrentText(cur.isEmpty() ? QSettings().value(QStringLiteral("measure/sample")).toString() : cur);
    const bool project = ctx_.recordingsDir && !ctx_.recordingsDir().isEmpty();
    folderButton_->setVisible(!project);
    const QString full = QDir::toNativeSeparators(outputDir());
    folder_->setToolTip(full);
    folder_->setText(project ? tr("the project's recordings folder") : folder_->fontMetrics().elidedText(full, Qt::ElideMiddle, 220));
}

QString MeasureWindow::outputDir() const {
    const QString p = ctx_.recordingsDir ? ctx_.recordingsDir() : QString();
    return p.isEmpty() ? chosenDir_ : p;
}

void MeasureWindow::listCameras() {
    if (recording_) return;
    stopCamera();
    camera_->clear();
    for (const ld::CameraInfo& c : ld::Camera::list()) camera_->addItem(c.name, c.id);
    QString last = QSettings().value(QStringLiteral("measure/camera")).toString();
    if (qEnvironmentVariableIsSet("LIBREDROP_CAMERA")) last = qEnvironmentVariable("LIBREDROP_CAMERA");   // testing
    int i = camera_->findData(last);
    if (i < 0) i = 0;   // the first real camera (the demo is last)
    camera_->setCurrentIndex(i);
    if (camera_->count() == 0) {
        view_->setMessage(tr("No camera found"));
        return;
    }
    cameraChosen();
}

void MeasureWindow::cameraChosen() {
    if (recording_) return;
    stopCamera();
    mode_->clear();
    QString err;
    pending_ = ld::Camera::open(camera_->currentData().toString(), &err);
    if (!pending_) {
        view_->setFrame({}, std::nullopt, {});
        view_->setMessage(err);
        status_->setText(err);
        status_->setProperty("state", "todo");
        status_->style()->polish(status_);
        return;
    }
    for (const ld::CameraMode& m : pending_->modes())
        mode_->addItem(tr("%1 × %2 at %3 fps").arg(m.w).arg(m.h).arg(m.fps, 0, 'f', m.fps == std::round(m.fps) ? 0 : 1),
                       QVariantList{m.w, m.h, m.fps});
    const QString lastMode = QSettings().value(QStringLiteral("measure/mode/") + camera_->currentData().toString()).toString();
    for (int i = 0; i < mode_->count(); ++i)
        if (mode_->itemText(i) == lastMode) mode_->setCurrentIndex(i);
    startCamera();
}

void MeasureWindow::startCamera() {
    if (recording_ || mode_->count() == 0) return;
    stopCamera();
    if (!pending_) {   // a new mode on the same camera: open it again
        QString err;
        pending_ = ld::Camera::open(camera_->currentData().toString(), &err);
        if (!pending_) {
            view_->setMessage(err);
            return;
        }
    }
    const QVariantList v = mode_->currentData().toList();
    const ld::CameraMode m{v.value(0).toInt(), v.value(1).toInt(), v.value(2).toDouble()};
    thread_ = new CameraThread(std::move(pending_), m, std::max(1, int(std::lround(exposure_->value() * 10))), pre_->value());
    connect(thread_, &CameraThread::frameReady, this, &MeasureWindow::frameReady, Qt::QueuedConnection);
    connect(thread_, &CameraThread::failed, this, [this](const QString& e) {
        status_->setText(tr("Camera stopped: %1").arg(e));
        status_->setProperty("state", "todo");
        status_->style()->polish(status_);
        record_->setEnabled(false);
    });
    connect(thread_, &CameraThread::recordingStarted, this, [this] {
        record_->setText(tr("■ Stop"));
        record_->setEnabled(true);
    });
    connect(thread_, &CameraThread::recordingProgress, this, [this](double s, int frames, int dropped) {
        status_->setText(tr("Recording: %1 s, %2 frames%3").arg(s, 0, 'f', 1).arg(frames).arg(dropped ? tr(", %1 dropped").arg(dropped) : QString()));
        status_->setProperty("state", "ok");
        status_->style()->polish(status_);
    });
    connect(thread_, &CameraThread::recordingStopped, this, &MeasureWindow::recordingStopped);
    thread_->start();
    record_->setEnabled(true);
    status_->setText(tr("Live: %1").arg(mode_->currentText()));
    status_->setProperty("state", "");
    status_->style()->polish(status_);
    saveSettings();
}

void MeasureWindow::stopCamera() {
    if (!thread_) return;
    thread_->finish();
    delete thread_;
    thread_ = nullptr;
    record_->setEnabled(false);
}

void MeasureWindow::frameReady() {
    if (!thread_) return;
    if (preview_.isRunning()) {
        previewWanted_ = true;
        return;
    }
    previewWanted_ = false;
    ld::CameraFrame f;
    if (!thread_->takeLatest(&f)) return;
    const bool analyse = live_->isChecked();
    std::function<ld::SceneOptions(int)> opts = ctx_.sceneOptions;
    preview_.setFuture(QtConcurrent::run([f = std::move(f), analyse, opts]() {
        Preview p;
        p.image = QImage::fromData(f.jpeg, "JPEG").convertToFormat(QImage::Format_Grayscale8);
        if (p.image.isNull()) return p;
        const ld::GrayImage g = ld::fromQImage(p.image);
        double s = 0;
        int n = 0;
        for (int y = 0; y < g.h; y += 4)
            for (int x = 0; x < g.w; x += 4) s += g.at(x, y), ++n;
        p.mean = n ? s / n : 0;
        if (analyse && p.mean > 15) {
            ld::SceneOptions o = opts ? opts(g.w) : ld::SceneOptions{};
            o.drop.staticMeasurement = false;
            p.scene = ld::analyzeScene(g, o);
        }
        return p;
    }));
}

void MeasureWindow::previewDone() {
    const Preview p = preview_.result();
    if (!p.image.isNull()) {
        QList<int> ids;
        if (p.scene)
            for (std::size_t i = 0; i < p.scene->drops.size(); ++i) ids << int(i + 1);
        view_->setFrame(p.image, p.scene, ids);
        QStringList lines;
        if (thread_) lines << tr("%1 fps").arg(thread_->fps(), 0, 'f', 1);
        if (p.scene) {
            for (std::size_t i = 0; i < p.scene->drops.size(); ++i) {
                const ld::DropAnalysis& a = p.scene->drops[i].a;
                const ld::AngleResult* m = a.method(QStringLiteral("young-laplace"));
                if (!m || !m->ok) m = a.method(QStringLiteral("tangent"));
                if (m && m->ok)
                    lines << tr("Drop %1: θ %2° left, %3° right").arg(i + 1).arg(m->left, 0, 'f', 1).arg(m->right, 0, 'f', 1);
            }
            if (p.scene->ok && p.scene->drops.empty()) lines << tr("Surface found; no drop yet");
            else if (!p.scene->ok) lines << tr("No surface found");
        }
        readout_->setText(lines.join(QStringLiteral("<br>")));
        if (!recording_) {
            QString warn;
            if (p.mean < 15) warn = tr("The image is black: is the backlight on and the lens cap off? Otherwise raise the exposure.");
            else if (p.mean > 245) warn = tr("The image is white: lower the exposure.");
            const double frameMs = thread_ && thread_->mode().fps > 0 ? 1000 / thread_->mode().fps : 1e9;
            if (warn.isEmpty() && exposure_->value() > frameMs)
                warn = tr("The exposure (%1 ms) is longer than a frame (%2 ms): the camera will slow down.").arg(exposure_->value()).arg(frameMs, 0, 'f', 1);
            status_->setText(warn.isEmpty() ? tr("Live: %1").arg(mode_->currentText()) : warn);
            status_->setProperty("state", warn.isEmpty() ? "" : "todo");
            status_->style()->polish(status_);
        }
    }
    if (previewWanted_) frameReady();
}

void MeasureWindow::record() {
    if (!thread_) return;
    if (recording_) {
        record_->setEnabled(false);
        record_->setText(tr("Stopping…"));
        thread_->stopRecording();
        return;
    }
    const QString dir = outputDir();
    if (!QDir().mkpath(dir)) {
        QMessageBox::warning(this, tr("Record"), tr("Cannot make the folder %1.").arg(dir));
        return;
    }
    const QString base = ld::recordingName(shortLiquid(liquid_->currentText()), sample_->currentText(), side_->currentText(),
                                           direction_->currentText(), QDateTime::currentDateTime());
    QString path = QDir(dir).filePath(base + QStringLiteral(".mkv"));
    for (int k = 2; QFileInfo::exists(path); ++k) path = QDir(dir).filePath(QStringLiteral("%1_%2.mkv").arg(base).arg(k));

    QJsonObject meta, sample;
    const ld::CameraMode m = thread_->mode();
    meta[QStringLiteral("device")] = thread_->cameraName();
    meta[QStringLiteral("size")] = QStringLiteral("%1x%2").arg(m.w).arg(m.h);
    meta[QStringLiteral("fps_requested")] = m.fps;
    meta[QStringLiteral("exposure_100us")] = int(std::lround(exposure_->value() * 10));
    meta[QStringLiteral("controls")] = thread_->cameraSettings();
    meta[QStringLiteral("note")] = note_->text();
    meta[QStringLiteral("pre_seconds")] = pre_->value();
    auto put = [&sample](const char* k, const QString& v) {
        if (!v.trimmed().isEmpty()) sample[QLatin1String(k)] = v.trimmed();
    };
    put("sample", sample_->currentText());
    put("side", side_->currentText());
    put("direction", direction_->currentText());
    put("liquid", shortLiquid(liquid_->currentText()));
    if (volume_->value() > 0) sample[QStringLiteral("volume_ul")] = volume_->value();
    if (control_->isChecked()) sample[QStringLiteral("control")] = true;
    meta[QStringLiteral("sample")] = sample;

    recording_ = true;
    for (QWidget* w : std::initializer_list<QWidget*>{camera_, mode_, sample_, side_, direction_, liquid_, volume_, control_, note_, pre_, maxSeconds_, folderButton_})
        w->setEnabled(false);
    record_->setText(tr("Starting…"));
    record_->setEnabled(false);
    record_->setProperty("recording", true);
    record_->style()->polish(record_);
    saveSettings();
    thread_->startRecording(path, meta, maxSeconds_->value());
}

void MeasureWindow::recordingStopped(const QString& path, int frames, double seconds, double fps, int dropped, const QString& error) {
    recording_ = false;
    for (QWidget* w : std::initializer_list<QWidget*>{camera_, mode_, sample_, side_, direction_, liquid_, volume_, control_, note_, pre_, maxSeconds_, folderButton_})
        w->setEnabled(true);
    record_->setText(tr("● Record"));
    record_->setEnabled(thread_ != nullptr);
    record_->setProperty("recording", false);
    record_->style()->polish(record_);
    if (!error.isEmpty()) {
        status_->setText(tr("Recording stopped: %1").arg(error));
        status_->setProperty("state", "todo");
        status_->style()->polish(status_);
        if (frames == 0) return;
    } else {
        status_->setText(tr("Saved %1: %2 frames in %3 s (%4 fps%5).")
                             .arg(QFileInfo(path).fileName()).arg(frames).arg(seconds, 0, 'f', 1).arg(fps, 0, 'f', 1)
                             .arg(dropped ? tr(", %1 dropped by the camera").arg(dropped) : QString()));
        status_->setProperty("state", "ok");
        status_->style()->polish(status_);
    }
    note_->clear();
    if (ctx_.recorded) ctx_.recorded(path, analyse_->isChecked());
    refreshSamples();
}

void MeasureWindow::saveSettings() const {
    QSettings st;
    st.setValue(QStringLiteral("measure/camera"), camera_->currentData());
    if (mode_->count()) st.setValue(QStringLiteral("measure/mode/") + camera_->currentData().toString(), mode_->currentText());
    st.setValue(QStringLiteral("measure/exposureMs"), exposure_->value());
    st.setValue(QStringLiteral("measure/live"), live_->isChecked());
    st.setValue(QStringLiteral("measure/sample"), sample_->currentText());
    st.setValue(QStringLiteral("measure/side"), side_->currentText());
    st.setValue(QStringLiteral("measure/direction"), direction_->currentText());
    st.setValue(QStringLiteral("measure/liquid"), liquid_->currentText());
    st.setValue(QStringLiteral("measure/volume"), volume_->value());
    st.setValue(QStringLiteral("measure/preSeconds"), pre_->value());
    st.setValue(QStringLiteral("measure/maxSeconds"), maxSeconds_->value());
    st.setValue(QStringLiteral("measure/folder"), chosenDir_);
    st.setValue(QStringLiteral("measure/analyse"), analyse_->isChecked());
    st.setValue(QStringLiteral("measure/geometry"), saveGeometry());
}

void MeasureWindow::closeEvent(QCloseEvent* e) {
    if (recording_) {
        if (QMessageBox::question(this, tr("Measure"), tr("Stop the recording and close?")) != QMessageBox::Yes) {
            e->ignore();
            return;
        }
        thread_->stopRecording();
        // the thread finishes the file when it stops
    }
    saveSettings();
    stopCamera();   // frees the camera for other programs
    e->accept();
}
