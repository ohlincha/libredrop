#pragma once
// Cameras for live measurement. Frames are kept as the camera's own JPEG data (UVC cameras such as the
// goniometer's send Motion-JPEG), so recording copies them into the video without re-encoding and keeps up at
// 120 fps; each frame carries the driver's capture time.
//
// Backends: V4L2 (Linux), ffmpeg (Windows: DirectShow, macOS: AVFoundation; any ffmpeg input as "ffmpeg:<format>:<input>"),
// the simulator ("synth:<scenario>", core/simulate.h), and a camera that replays a
// Motion-JPEG recording in real time ("sim:<file>";
// the GUI offers the built-in example) for trying things out and testing without a camera.
//
// The goniometer camera's settings as Ossila's software sets them (read from a USB capture, see
// tools/goniometer_capture.py): manual exposure 3.9 ms, gain 0, brightness 0, contrast 32, hue 0, saturation 64,
// sharpness 0, gamma 72, backlight compensation 2, auto white balance. applyOssilaSettings() sets these.
#include <QByteArray>
#include <QString>
#include <QStringList>
#include <memory>
#include <vector>

namespace ld {

struct CameraInfo {
    QString id;                    // /dev/videoN, ffmpeg:<format>:<input>, synth:<scenario>, sim:<file>
    QString name;
};

struct CameraMode {
    int w = 0, h = 0;
    double fps = 0;                // the fastest the camera offers at this size
};

struct CameraFrame {
    QByteArray jpeg;
    double t = 0;                  // seconds, capture time on a monotonic clock
    int sequence = 0;              // the driver's frame counter (gaps: frames the camera dropped)
};

struct CameraControl {
    QString name;                  // V4L2 style: exposure_time_absolute, gain, gamma, ...
    int min = 0, max = 0, value = 0, def = 0;
};

class Camera {
public:
    virtual ~Camera() = default;
    // Cameras that deliver Motion-JPEG, then the simulated example (when withDemo).
    static std::vector<CameraInfo> list(bool withDemo = true);
    static std::unique_ptr<Camera> open(const QString& id, QString* error);
    static QString demoId();       // the built-in example recording, replayed

    virtual QString name() const = 0;
    virtual std::vector<CameraMode> modes() const = 0;
    virtual bool start(const CameraMode& m, QString* error) = 0;
    // The next frame; waits for it (up to about 2 s). False on error or timeout (error says which).
    virtual bool grab(CameraFrame* out, QString* error) = 0;
    virtual void stop() = 0;
    virtual CameraMode mode() const = 0;   // as started (the size and rate the camera accepted)

    virtual std::vector<CameraControl> controls() const { return {}; }
    virtual bool setControl(const QString& name, int value) { (void)name, (void)value; return false; }
    // The arguments for ffmpeg that open the camera driver's own settings dialog (Windows), or empty. The camera must
    // not be in use meanwhile; the driver keeps what is set there.
    virtual QStringList settingsDialogArgs() const { return {}; }
    // Ossila's settings with this exposure (in 100 µs units, as the camera counts it); false if not a UVC camera.
    bool applyOssilaSettings(int exposure100us);
    // The settings to store with a recording (name → value).
    std::vector<std::pair<QString, int>> settings() const;
};

}  // namespace ld
