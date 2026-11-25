#pragma once
// Recording from a camera into a Motion-JPEG Matroska file (the camera's JPEG frames, not re-encoded) with each
// frame's capture time, plus <name>.json next to it in the capture tool's format (sample fields, camera settings,
// frames, measured frame rate), so the project library files it like any other recording.
//
// Frames from before Record was pressed are kept for `preSeconds` and written first: press Record when the drop
// touches and the contact is still in the video.
#include <QDateTime>
#include <QJsonObject>
#include <deque>

#include "camera.h"
#include "mjpeg.h"

namespace ld {

class Recorder {
public:
    double preSeconds = 2;

    // Call for every frame from the camera.
    bool push(const CameraFrame& f, QString* error);
    // meta: written to the .json (started, frames, duration, fps are added)
    bool start(const QString& path, int width, int height, const QJsonObject& meta, QString* error);
    bool stop(QString* error);                     // closes the video and writes the .json
    bool recording() const { return recording_; }

    QString path() const { return path_; }
    int frames() const { return frames_; }
    int dropped() const { return dropped_; }       // frames the camera skipped (gaps in its frame counter)
    double duration() const { return frames_ ? last_ - first_ : 0; }
    double fps() const { return frames_ > 1 && duration() > 0 ? (frames_ - 1) / duration() : 0; }

private:
    bool write(const CameraFrame& f, QString* error);
    std::deque<CameraFrame> pre_;
    MjpegWriter writer_;
    bool recording_ = false;
    QString path_;
    QJsonObject meta_;
    QDateTime startedWall_;
    double first_ = 0, last_ = 0;
    int frames_ = 0, dropped_ = 0, lastSeq_ = -1;
};

// File name for a recording: liquid_sample_side_direction_yyyyMMdd_HHmmss (as the capture tool names them).
QString recordingName(const QString& liquid, const QString& sample, const QString& side, const QString& direction,
                      const QDateTime& when);

}  // namespace ld
