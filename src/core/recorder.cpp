#include "recorder.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>

#include <cmath>

namespace ld {

bool Recorder::push(const CameraFrame& f, QString* error) {
    if (recording_) return write(f, error);
    pre_.push_back(f);
    while (pre_.size() > 1 && pre_.back().t - pre_.front().t > preSeconds) pre_.pop_front();
    if (preSeconds <= 0) pre_.clear();
    return true;
}

bool Recorder::start(const QString& path, int width, int height, const QJsonObject& meta, QString* error) {
    if (recording_) stop(nullptr);
    if (!writer_.open(path, width, height, error)) return false;
    path_ = path;
    meta_ = meta;
    frames_ = dropped_ = 0;
    lastSeq_ = -1;
    recording_ = true;
    const double newest = pre_.empty() ? 0 : pre_.back().t;
    startedWall_ = QDateTime::currentDateTime().addMSecs(pre_.empty() ? 0 : -qint64((newest - pre_.front().t) * 1000));
    std::deque<CameraFrame> pre;
    pre.swap(pre_);
    for (const CameraFrame& f : pre)
        if (!write(f, error)) return false;
    return true;
}

bool Recorder::write(const CameraFrame& f, QString* error) {
    if (frames_ == 0) first_ = f.t;
    else if (f.t <= last_) return true;   // out of order or repeated: keep times increasing
    if (lastSeq_ >= 0 && f.sequence > lastSeq_ + 1) dropped_ += f.sequence - lastSeq_ - 1;
    lastSeq_ = f.sequence;
    if (!writer_.add(f.jpeg, f.t - first_)) {
        if (error) *error = QStringLiteral("cannot write to %1 (disk full?)").arg(path_);
        return false;
    }
    last_ = f.t;
    ++frames_;
    return true;
}

bool Recorder::stop(QString* error) {
    if (!recording_) return true;
    recording_ = false;
    if (!writer_.finish()) {
        if (error) *error = QStringLiteral("cannot finish %1").arg(path_);
        return false;
    }
    QJsonObject m = meta_;
    m[QStringLiteral("started")] = startedWall_.toString(Qt::ISODateWithMs);
    m[QStringLiteral("frames")] = frames_;
    m[QStringLiteral("duration_s")] = duration();
    m[QStringLiteral("fps_measured")] = std::round(fps() * 100) / 100;
    m[QStringLiteral("dropped_frames")] = dropped_;
    m[QStringLiteral("recorded_by")] = QStringLiteral("LibreDrop " LIBREDROP_VERSION);
    const QFileInfo fi(path_);
    QFile f(fi.dir().filePath(fi.completeBaseName() + QStringLiteral(".json")));
    if (!f.open(QIODevice::WriteOnly) || f.write(QJsonDocument(m).toJson()) < 0) {
        if (error) *error = QStringLiteral("cannot write %1").arg(f.fileName());
        return false;
    }
    return true;
}

QString recordingName(const QString& liquid, const QString& sample, const QString& side, const QString& direction,
                      const QDateTime& when) {
    QStringList parts;
    for (QString p : {liquid, sample, side, direction}) {
        p = p.trimmed();
        if (p.isEmpty()) continue;
        for (QChar& c : p)
            if (!c.isLetterOrNumber() && c != QLatin1Char('-') && c != QLatin1Char('.')) c = QLatin1Char('-');
        parts << p;
    }
    if (parts.isEmpty()) parts << QStringLiteral("drop");
    return parts.join(QLatin1Char('_')) + when.toString(QStringLiteral("_yyyyMMdd_HHmmss"));
}

}  // namespace ld
