#pragma once
// Motion-JPEG videos without ffmpeg: every frame is a JPEG (what UVC cameras such as the goniometer's send), so
// reading a recording only needs the container (Matroska .mkv/.webm, or AVI incl. OpenDML > 1 GB) and Qt's JPEG
// decoder. Writing: Matroska with each frame's own timestamp (cameras do not keep a perfectly even frame rate).
#include <QFile>
#include <QImage>
#include <QString>
#include <cstdint>
#include <vector>

#include "image.h"

namespace ld {

class MjpegReader {
public:
    // False if the file is not a Motion-JPEG Matroska or AVI file (then use ffmpeg).
    bool open(const QString& path, QString* error = nullptr);
    int frames() const { return int(index_.size()); }
    int width() const { return w_; }
    int height() const { return h_; }
    const std::vector<double>& times() const { return times_; }   // seconds, from the first frame
    QByteArray jpeg(int i);              // the frame's JPEG data
    bool frame(int i, GrayImage* out);   // decoded to grey levels
    QString format() const { return format_; }

private:
    struct Entry {
        qint64 offset;
        qint64 size;
    };
    bool openMatroska(QString* error);
    bool openAvi(QString* error);
    QFile f_;
    std::vector<Entry> index_;
    std::vector<double> times_;
    int w_ = 0, h_ = 0;
    QString format_;
};

// Matroska Motion-JPEG writer. Call add() for each frame (JPEG data and time in seconds), then finish().
class MjpegWriter {
public:
    bool open(const QString& path, int width, int height, QString* error = nullptr);
    bool add(const QByteArray& jpeg, double t);
    bool finish();
    ~MjpegWriter();

private:
    void closeCluster();
    QFile f_;
    qint64 segmentData_ = 0, durationPos_ = 0, clusterStart_ = -1, clusterData_ = 0;
    std::int64_t clusterTime_ = 0, lastTime_ = 0;
    struct Cue {
        std::int64_t time;
        qint64 clusterPos;
    };
    std::vector<Cue> cues_;
    bool ok_ = false;
};

// JPEG of a grey image at the given quality (0–100).
QByteArray encodeJpeg(const GrayImage& im, int quality);

}  // namespace ld
