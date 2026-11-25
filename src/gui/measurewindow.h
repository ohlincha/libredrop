#pragma once
// Measure: the live camera with the drops found in it (surface, outline, angle), and recording. Fill in the sample,
// press Record (or Ctrl+R) when the drop touches (the video starts 2 s earlier), Stop when it has settled. The
// video and its .json go into the project's recordings/ (or a chosen folder), are added to the project, and the
// main window opens and analyses them.
#include <QFutureWatcher>
#include <QImage>
#include <QDateTime>
#include <QJsonObject>
#include <QThread>
#include <QWidget>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>

#include "core/camera.h"
#include "core/recorder.h"
#include "core/scene.h"

class FrameView;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;

// Grabs frames on its own thread, writes them while recording, and keeps the newest for the live view.
class CameraThread : public QThread {
    Q_OBJECT
public:
    CameraThread(std::unique_ptr<ld::Camera> cam, const ld::CameraMode& mode, int exposure100us, double preSeconds);
    ~CameraThread() override;
    void finish();                                        // stop and wait
    bool takeLatest(ld::CameraFrame* f);                  // the newest frame since the last call
    void startRecording(const QString& path, const QJsonObject& meta, double maxSeconds);
    void stopRecording();
    void setExposure(int exposure100us);
    ld::CameraMode mode() const { return mode_; }
    QString cameraName() const { return name_; }
    QJsonObject cameraSettings() const;                   // after start (the camera's controls)
    double fps() const { return fps_.load(); }            // measured over the last second

signals:
    void frameReady();                                    // a new frame is waiting (sent once until taken)
    void failed(const QString& error);
    void recordingStarted();
    void recordingProgress(double seconds, int frames, int dropped);
    void recordingStopped(const QString& path, int frames, double seconds, double fps, int dropped, const QString& error);

protected:
    void run() override;

private:
    std::unique_ptr<ld::Camera> cam_;
    ld::CameraMode mode_;
    QString name_;
    std::atomic<bool> quit_{false};
    std::atomic<int> exposure_;
    std::atomic<bool> exposureChanged_{false};
    std::atomic<double> fps_{0};
    mutable std::mutex m_;
    ld::CameraFrame latest_;
    bool haveLatest_ = false;
    QJsonObject settings_;
    // recording requests (under m_)
    bool wantStart_ = false, wantStop_ = false;
    QString path_;
    QJsonObject meta_;
    double maxSeconds_ = 0;
    ld::Recorder rec_;
};

struct MeasureContext {
    std::function<ld::SceneOptions(int frameWidth)> sceneOptions;   // scale and liquid from the main window
    std::function<QString()> recordingsDir;                          // the project's recordings/ ("" without a project)
    std::function<QStringList()> samples;                            // sample names in the project
    QStringList liquids;                                             // names for the liquid list
    std::function<void(const QString& path, bool open)> recorded;    // add to the project; open and analyse
    std::function<void(int frameWidth, double pxPerMm, double needleMm)> scaleSet;   // from the needle
    std::function<bool(double* t, double* h, QString* sensor)> conditions;          // room °C, %RH (USB sensor)
};

class MeasureWindow : public QWidget {
    Q_OBJECT
public:
    MeasureWindow(MeasureContext ctx, QWidget* parent = nullptr);
    ~MeasureWindow() override;
    void refreshSamples();
    void listCameras();                                   // and start the first (or last used)

protected:
    void closeEvent(QCloseEvent* e) override;

private:
    void cameraChosen();
    void startCamera();
    void stopCamera();
    void frameReady();
    void previewDone();
    void record();
    void recordingStopped(const QString& path, int frames, double seconds, double fps, int dropped, const QString& error);
    QString outputDir() const;
    QString baseName(const QDateTime& when) const;   // the file name without .mkv
    void updateName();
    void saveSettings() const;

    MeasureContext ctx_;
    FrameView* view_ = nullptr;
    QComboBox* camera_ = nullptr;
    QComboBox* mode_ = nullptr;
    QDoubleSpinBox* exposure_ = nullptr;
    QCheckBox* live_ = nullptr;
    QComboBox* sample_ = nullptr;
    QComboBox* side_ = nullptr;
    QComboBox* direction_ = nullptr;
    QComboBox* liquid_ = nullptr;
    QDoubleSpinBox* volume_ = nullptr;
    QCheckBox* control_ = nullptr;
    QLineEdit* note_ = nullptr;
    QSpinBox* maxSeconds_ = nullptr;
    QLabel* folder_ = nullptr;
    QLineEdit* name_ = nullptr;                           // own file name (empty: from the sample fields)
    QLabel* nameShown_ = nullptr;
    QPushButton* folderButton_ = nullptr;
    QPushButton* record_ = nullptr;
    QCheckBox* analyse_ = nullptr;
    QLabel* status_ = nullptr;
    QLabel* readout_ = nullptr;
    QLabel* scaleLabel_ = nullptr;
    QImage lastImage_;                                    // the newest live frame shown (grey)
    void scaleFromNeedle();
    void showScale();

    std::unique_ptr<ld::Camera> pending_;                 // opened, modes listed, not yet streaming
    CameraThread* thread_ = nullptr;
    bool recording_ = false;
    struct Preview {
        QImage image;
        std::optional<ld::Scene> scene;
        double mean = 0;
    };
    QFutureWatcher<Preview> preview_;
    bool previewWanted_ = false;
    QString chosenDir_;
};
