#include "mjpeg.h"

#include <QBuffer>
#include <QImageReader>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>

namespace ld {

namespace {

// ---- Matroska (EBML) -------------------------------------------------------------------------------------------

constexpr quint32 kEbml = 0x1A45DFA3, kSegment = 0x18538067, kInfo = 0x1549A966, kTimecodeScale = 0x2AD7B1,
                  kTracks = 0x1654AE6B, kTrackEntry = 0xAE, kTrackNumber = 0xD7, kTrackType = 0x83, kCodecId = 0x86,
                  kVideo = 0xE0, kPixelWidth = 0xB0, kPixelHeight = 0xBA, kCluster = 0x1F43B675, kTimecode = 0xE7,
                  kSimpleBlock = 0xA3, kBlockGroup = 0xA0, kBlock = 0xA1, kCues = 0x1C53BB6B, kSeekHead = 0x114D9B74,
                  kTags = 0x1254C367, kChapters = 0x1043A770, kAttachments = 0x1941A469;

bool isTopLevel(quint32 id) {
    return id == kCluster || id == kCues || id == kSeekHead || id == kInfo || id == kTracks || id == kTags ||
           id == kChapters || id == kAttachments;
}

struct Ebml {
    QFile& f;
    // reads an element header at pos; size -1 = unknown
    bool header(qint64 pos, quint32* id, qint64* size, qint64* data) {
        if (!f.seek(pos)) return false;
        uchar b[12];
        const qint64 n = f.read(reinterpret_cast<char*>(b), 12);
        if (n < 2) return false;
        int il = 1;
        while (il <= 4 && !(b[0] & (0x80 >> (il - 1)))) ++il;
        if (il > 4 || il >= n) return false;
        quint32 v = 0;
        for (int i = 0; i < il; ++i) v = (v << 8) | b[i];
        int sl = 1;
        while (sl <= 8 && !(b[il] & (0x80 >> (sl - 1)))) ++sl;
        if (sl > 8 || il + sl > n) return false;
        quint64 s = b[il] & (0xFF >> sl);
        bool allOnes = s == quint64(0xFF >> sl);
        for (int i = 1; i < sl; ++i) {
            s = (s << 8) | b[il + i];
            allOnes = allOnes && b[il + i] == 0xFF;
        }
        *id = v;
        *size = allOnes ? -1 : qint64(s);
        *data = pos + il + sl;
        return true;
    }
    quint64 uint(qint64 data, qint64 size) {
        f.seek(data);
        const QByteArray b = f.read(std::min<qint64>(size, 8));
        quint64 v = 0;
        for (char c : b) v = (v << 8) | uchar(c);
        return v;
    }
    QByteArray bytes(qint64 data, qint64 size) {
        f.seek(data);
        return f.read(std::min<qint64>(size, 1024));
    }
};

// ---- writing ---------------------------------------------------------------------------------------------------

QByteArray id(quint32 v) {
    QByteArray b;
    for (int s = 24; s >= 0; s -= 8)
        if (v >> s || !b.isEmpty()) b.append(char((v >> s) & 0xFF));
    return b;
}

QByteArray vsize(quint64 n, int len = 0) {
    if (!len) {
        len = 1;
        while (len < 8 && n >= (quint64(1) << (7 * len)) - 1) ++len;
    }
    QByteArray b(len, 0);
    for (int i = len - 1; i >= 0; --i, n >>= 8) b[i] = char(n & 0xFF);
    b[0] = char(uchar(b[0]) | (0x80 >> (len - 1)));
    return b;
}

QByteArray el(quint32 i, const QByteArray& payload) { return id(i) + vsize(quint64(payload.size())) + payload; }

QByteArray uintEl(quint32 i, quint64 v) {
    QByteArray b;
    do {
        b.prepend(char(v & 0xFF));
        v >>= 8;
    } while (v);
    return el(i, b);
}

QByteArray floatEl(quint32 i, double v) {
    quint64 bits;
    std::memcpy(&bits, &v, 8);
    QByteArray b(8, 0);
    for (int k = 7; k >= 0; --k, bits >>= 8) b[k] = char(bits & 0xFF);
    return el(i, b);
}

constexpr double kTickSeconds = 1e-4;   // timestamps in 0.1 ms

}  // namespace

// ---- reader ----------------------------------------------------------------------------------------------------

bool MjpegReader::open(const QString& path, QString* error) {
    f_.setFileName(path);
    index_.clear();
    times_.clear();
    if (!f_.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("cannot read %1").arg(path);
        return false;
    }
    const QByteArray magic = f_.read(12);
    bool ok = false;
    if (magic.size() >= 4 && qFromBigEndian<quint32>(magic.constData()) == kEbml) ok = openMatroska(error);
    else if (magic.startsWith("RIFF") && magic.mid(8, 4) == "AVI ") ok = openAvi(error);
    else if (error) *error = QStringLiteral("not a Matroska or AVI file");
    if (ok && index_.empty()) {
        ok = false;
        if (error) *error = QStringLiteral("no frames");
    }
    if (ok && (w_ <= 0 || h_ <= 0)) {   // size from the first frame
        QBuffer b;
        b.setData(jpeg(0));
        QImageReader r(&b, "jpeg");
        w_ = r.size().width(), h_ = r.size().height();
        ok = w_ > 0 && h_ > 0;
        if (!ok && error) *error = QStringLiteral("the first frame is not a JPEG");
    }
    if (ok && !times_.empty()) {
        const double t0 = times_.front();
        for (double& t : times_) t -= t0;
    }
    if (!ok) f_.close();
    return ok;
}

bool MjpegReader::openMatroska(QString* error) {
    Ebml e{f_};
    const qint64 fileSize = f_.size();
    quint32 i;
    qint64 size, data;
    if (!e.header(0, &i, &size, &data) || i != kEbml || size < 0) return false;
    qint64 pos = data + size;
    if (!e.header(pos, &i, &size, &data) || i != kSegment) {
        if (error) *error = QStringLiteral("no Matroska segment");
        return false;
    }
    const qint64 segEnd = size < 0 ? fileSize : std::min(fileSize, data + size);
    double scale = 1e-6;   // TimecodeScale (ns) in seconds
    int videoTrack = -1;
    bool mjpeg = false;
    pos = data;
    while (pos < segEnd && e.header(pos, &i, &size, &data)) {
        const bool unknown = size < 0;   // (size and data are reused for the children below)
        const qint64 end = unknown ? segEnd : std::min(segEnd, data + size);
        if (i == kInfo) {
            for (qint64 p = data; p < end && e.header(p, &i, &size, &data); p = data + size)
                if (i == kTimecodeScale) scale = double(e.uint(data, size)) * 1e-9;
        } else if (i == kTracks) {
            for (qint64 p = data; p < end && e.header(p, &i, &size, &data); p = data + size) {
                if (i != kTrackEntry) continue;
                int number = 0, type = 0, w = 0, h = 0;
                QByteArray codec;
                const qint64 tEnd = data + size;
                for (qint64 q = data; q < tEnd && e.header(q, &i, &size, &data); q = data + size) {
                    if (i == kTrackNumber) number = int(e.uint(data, size));
                    else if (i == kTrackType) type = int(e.uint(data, size));
                    else if (i == kCodecId) codec = e.bytes(data, size);
                    else if (i == kVideo)
                        for (qint64 v = data, vEnd = data + size; v < vEnd && e.header(v, &i, &size, &data); v = data + size) {
                            if (i == kPixelWidth) w = int(e.uint(data, size));
                            else if (i == kPixelHeight) h = int(e.uint(data, size));
                        }
                }
                while (codec.endsWith('\0')) codec.chop(1);
                if (type == 1 && videoTrack < 0) {
                    videoTrack = number;
                    mjpeg = codec == "V_MJPEG" || codec == "V_MS/VFW/FOURCC";   // (VFW: checked on the first frame)
                    w_ = w, h_ = h;
                }
            }
        } else if (i == kCluster) {
            std::int64_t clusterTime = 0;
            qint64 p = data;
            while (p < end && e.header(p, &i, &size, &data)) {
                if (size < 0 || isTopLevel(i)) break;   // a cluster of unknown size ends at the next top-level element
                if (i == kTimecode) clusterTime = std::int64_t(e.uint(data, size));
                qint64 block = -1, blockSize = 0;
                if (i == kSimpleBlock) block = data, blockSize = size;
                else if (i == kBlockGroup) {
                    quint32 j;
                    qint64 s2, d2;
                    for (qint64 q = data; q < data + size && e.header(q, &j, &s2, &d2); q = d2 + s2)
                        if (j == kBlock) block = d2, blockSize = s2;
                }
                if (block >= 0 && blockSize > 4) {
                    f_.seek(block);
                    uchar h[11];
                    f_.read(reinterpret_cast<char*>(h), 11);
                    int tl = 1;
                    while (tl <= 8 && !(h[0] & (0x80 >> (tl - 1)))) ++tl;
                    const int track = h[0] & (0xFF >> tl);   // (track numbers above 127 are not expected)
                    const auto rel = qint16(quint16(h[tl] << 8 | h[tl + 1]));
                    const int lacing = (h[tl + 2] >> 1) & 3;
                    if (track == videoTrack && lacing == 0) {
                        const qint64 off = block + tl + 3;
                        index_.push_back({off, block + blockSize - off});
                        times_.push_back(double(clusterTime + rel) * scale);
                    }
                }
                p = data + size;
            }
            pos = unknown ? p : end;
            continue;
        }
        if (unknown) break;
        pos = end;
    }
    if (videoTrack < 0) {
        if (error) *error = QStringLiteral("no video track");
        return false;
    }
    if (!mjpeg) {
        if (error) *error = QStringLiteral("the video is not Motion-JPEG");
        index_.clear();
        return false;
    }
    format_ = QStringLiteral("Matroska Motion-JPEG");
    return true;
}

bool MjpegReader::openAvi(QString* error) {
    const qint64 fileSize = f_.size();
    auto u32 = [&](qint64 pos) {
        f_.seek(pos);
        uchar b[4] = {0, 0, 0, 0};
        f_.read(reinterpret_cast<char*>(b), 4);
        return quint32(b[0] | b[1] << 8 | b[2] << 16 | quint32(b[3]) << 24);
    };
    auto fourcc = [&](qint64 pos) {
        f_.seek(pos);
        return f_.read(4);
    };
    int stream = -1, videoStream = -1;
    double frameTime = 0;
    bool mjpeg = false;
    // walk chunks (LIST/RIFF descend), collect 'NNdc'/'NNdb' chunks of the video stream from every movi list
    std::function<void(qint64, qint64)> walk = [&](qint64 pos, qint64 end) {
        while (pos + 8 <= end) {
            const QByteArray cc = fourcc(pos);
            const qint64 size = u32(pos + 4);
            const qint64 data = pos + 8;
            if (cc == "LIST" || cc == "RIFF") {
                const QByteArray type = fourcc(data);
                if (type == "strl") ++stream;
                walk(data + 4, std::min(end, data + size));
            } else if (cc == "strh" && fourcc(data) == "vids" && videoStream < 0) {
                videoStream = stream;
                const quint32 scale = u32(data + 20), rate = u32(data + 24);
                if (rate) frameTime = double(scale) / rate;
                const QByteArray h = fourcc(data + 4).toUpper();
                mjpeg = h == "MJPG" || h == "AVRN" || h == "LJPG" || h == "JPGL";
            } else if (cc == "strf" && stream == videoStream && videoStream >= 0) {
                w_ = int(u32(data + 4)), h_ = std::abs(int(u32(data + 8)));
                const QByteArray c = fourcc(data + 16).toUpper();
                if (c == "MJPG") mjpeg = true;
            } else if (cc == "avih" && frameTime == 0) {
                frameTime = u32(data) * 1e-6;
            } else if (cc.size() == 4 && (cc.endsWith("dc") || cc.endsWith("db")) && videoStream >= 0 &&
                       cc.left(2).toInt() == videoStream && size > 0) {
                index_.push_back({data, size});
                times_.push_back(double(index_.size() - 1) * (frameTime > 0 ? frameTime : 1.0 / 30));
            }
            pos = data + size + (size & 1);
        }
    };
    walk(0, fileSize);
    if (videoStream < 0) {
        if (error) *error = QStringLiteral("no video stream");
        return false;
    }
    if (!mjpeg) {
        if (error) *error = QStringLiteral("the video is not Motion-JPEG");
        index_.clear();
        return false;
    }
    format_ = QStringLiteral("AVI Motion-JPEG");
    return true;
}

QByteArray MjpegReader::jpeg(int i) {
    if (i < 0 || i >= frames() || !f_.seek(index_[std::size_t(i)].offset)) return {};
    return f_.read(index_[std::size_t(i)].size);
}

bool MjpegReader::frame(int i, GrayImage* out) {
    const QByteArray d = jpeg(i);
    if (d.isEmpty()) return false;
    QImage im;
    if (!im.loadFromData(d, "JPG")) return false;
    *out = fromQImage(im);
    return true;
}

// ---- writer ----------------------------------------------------------------------------------------------------

bool MjpegWriter::open(const QString& path, int width, int height, QString* error) {
    f_.setFileName(path);
    if (!f_.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) *error = QStringLiteral("cannot write %1").arg(path);
        return false;
    }
    const QByteArray header = el(kEbml, uintEl(0x4286, 1) + uintEl(0x42F7, 1) + uintEl(0x42F2, 4) + uintEl(0x42F3, 8) +
                                            el(0x4282, "matroska") + uintEl(0x4287, 4) + uintEl(0x4285, 2));
    f_.write(header);
    f_.write(id(kSegment) + vsize(0, 8));   // size patched in finish()
    segmentData_ = f_.pos();
    QByteArray info = uintEl(kTimecodeScale, quint64(std::llround(kTickSeconds * 1e9))) + el(0x4D80, "LibreDrop") +
                      el(0x5741, "LibreDrop");
    f_.write(id(kInfo) + vsize(quint64(info.size() + 11)) + info);
    durationPos_ = f_.pos();
    f_.write(floatEl(0x4489, 0.0));         // Duration, patched in finish()
    const QByteArray video = uintEl(kPixelWidth, quint64(width)) + uintEl(kPixelHeight, quint64(height));
    const QByteArray track = uintEl(kTrackNumber, 1) + uintEl(0x73C5, 1) + uintEl(kTrackType, 1) + el(kCodecId, "V_MJPEG") +
                             uintEl(0x9C, 0) + el(kVideo, video);
    f_.write(el(kTracks, el(kTrackEntry, track)));
    ok_ = f_.error() == QFile::NoError;
    return ok_;
}

void MjpegWriter::closeCluster() {
    if (clusterStart_ < 0) return;
    const qint64 end = f_.pos();
    f_.seek(clusterStart_ + 4);
    f_.write(vsize(quint64(end - clusterStart_ - 12), 8));
    f_.seek(end);
    clusterStart_ = -1;
}

bool MjpegWriter::add(const QByteArray& jpeg, double t) {
    if (!ok_) return false;
    const auto ticks = std::int64_t(std::llround(t / kTickSeconds));
    if (clusterStart_ < 0 || ticks - clusterTime_ > 30000 || ticks < clusterTime_ || f_.pos() - clusterStart_ > (64 << 20)) {
        closeCluster();
        clusterStart_ = f_.pos();
        clusterTime_ = ticks;
        cues_.push_back({ticks, clusterStart_ - segmentData_});
        f_.write(id(kCluster) + vsize(0, 8) + uintEl(kTimecode, quint64(ticks)));
    }
    const auto rel = qint16(ticks - clusterTime_);
    QByteArray head = vsize(1);   // track 1
    head.append(char((rel >> 8) & 0xFF)).append(char(rel & 0xFF)).append(char(0x80));   // keyframe
    f_.write(id(kSimpleBlock) + vsize(quint64(head.size() + jpeg.size())));
    f_.write(head);
    f_.write(jpeg);
    lastTime_ = ticks;
    ok_ = f_.error() == QFile::NoError;
    return ok_;
}

bool MjpegWriter::finish() {
    if (!f_.isOpen()) return ok_;
    closeCluster();
    QByteArray cues;
    for (const Cue& c : cues_)
        cues += el(0xBB, uintEl(0xB3, quint64(c.time)) + el(0xB7, uintEl(0xF7, 1) + uintEl(0xF1, quint64(c.clusterPos))));
    if (!cues.isEmpty()) f_.write(el(kCues, cues));
    const qint64 end = f_.pos();
    f_.seek(segmentData_ - 8);
    f_.write(vsize(quint64(end - segmentData_), 8));
    f_.seek(durationPos_);
    f_.write(floatEl(0x4489, double(lastTime_ + 1)));
    f_.close();
    return ok_;
}

MjpegWriter::~MjpegWriter() {
    if (f_.isOpen()) finish();
}

QByteArray encodeJpeg(const GrayImage& im, int quality) {
    QByteArray out;
    QBuffer b(&out);
    b.open(QIODevice::WriteOnly);
    toQImage(im).save(&b, "JPG", quality);
    return out;
}

}  // namespace ld
