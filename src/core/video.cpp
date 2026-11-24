#include "video.h"

#include "mjpeg.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QProcess>
#include <algorithm>
#include <future>
#include <thread>
#include <cmath>

namespace ld {

namespace {

// ffmpeg: LIBREDROP_FFMPEG, else next to the program (Windows installer, macOS bundle), else Homebrew's (apps
// started from the Finder don't have it on PATH), else on PATH.
QString ffmpegProgram() {
    if (qEnvironmentVariableIsSet("LIBREDROP_FFMPEG")) return qEnvironmentVariable("LIBREDROP_FFMPEG");
#ifdef Q_OS_WIN
    const QString exe = QStringLiteral("ffmpeg.exe");
#else
    const QString exe = QStringLiteral("ffmpeg");
#endif
    QStringList dirs;
    if (QCoreApplication::instance()) dirs << QCoreApplication::applicationDirPath();
#ifdef Q_OS_MACOS
    dirs << QStringLiteral("/opt/homebrew/bin") << QStringLiteral("/usr/local/bin");
#endif
    for (const QString& d : dirs)
        if (QFileInfo::exists(QDir(d).filePath(exe))) return QDir(d).filePath(exe);
    return QStringLiteral("ffmpeg");
}

QString ffprobeProgram() {
    QString p = ffmpegProgram();
    const int i = p.lastIndexOf(QLatin1String("ffmpeg"));
    return i >= 0 ? p.replace(i, 6, QStringLiteral("ffprobe")) : QStringLiteral("ffprobe");
}

bool run(const QString& program, const QStringList& args, QByteArray* out, QString* err) {
    QProcess p;
    p.start(program, args);
    if (!p.waitForStarted(10000)) {
        *err = QStringLiteral("cannot start %1 (install ffmpeg, or set LIBREDROP_FFMPEG)").arg(program);
        return false;
    }
    p.closeWriteChannel();
    if (!p.waitForFinished(600000) || p.exitCode() != 0) {
        *err = QStringLiteral("%1 failed: %2").arg(program, QString::fromLocal8Bit(p.readAllStandardError()).trimmed());
        return false;
    }
    *out = p.readAllStandardOutput();
    return true;
}

int batchSize() { return std::clamp(int(std::thread::hardware_concurrency()), 1, 16); }

// f applied to every element, each in its own thread (batches are small: one element per core).
template <class T, class F>
auto parallelMap(const std::vector<T>& in, F f) -> std::vector<decltype(f(in[0]))> {
    std::vector<std::future<decltype(f(in[0]))>> fut;
    for (const T& x : in) fut.push_back(std::async(std::launch::async, [&f, &x] { return f(x); }));
    std::vector<decltype(f(in[0]))> out;
    for (auto& r : fut) out.push_back(r.get());
    return out;
}

// Strip what the tables and plots do not need (outline points), image coordinates for the cut-out.
TrackPoint point(int frame, double t, SceneDrop d) {
    d.a.outline.clear();
    d.a.outline.shrink_to_fit();
    return {frame, t, std::move(d)};
}

struct Measured {
    int frame;
    double t;
    std::vector<SceneDrop> drops;
};

// Drops are followed from frame to frame by position. When the slide is pushed, all drops move together: the
// shift that brings most drops onto a recent drop of similar width is found first, then each drop is matched to
// the nearest shifted track.
VideoResult track(std::vector<Measured> frames, const VideoOptions& opt) {
    VideoResult r;
    r.frames = int(frames.size());
    struct Active {
        std::size_t track;
        double x, width, t;
    };
    std::vector<DropTrack> tracks;
    std::vector<Active> active;
    const double keep = 1.0;   // s: a drop not seen for this long is gone
    auto similar = [](double w1, double w2) { return std::abs(w1 - w2) < 0.35 * std::max(w1, w2) + 6; };
    auto tol = [](double w) { return std::max(8.0, 0.15 * w); };
    for (Measured& m : frames) {
        active.erase(std::remove_if(active.begin(), active.end(), [&](const Active& a) { return m.t - a.t > keep; }),
                     active.end());
        // common shift
        std::vector<double> shifts{0.0};
        for (const SceneDrop& d : m.drops)
            for (const Active& a : active)
                if (similar(d.width, a.width)) shifts.push_back(d.xc - a.x);
        double shift = 0;
        int bestCount = -1;
        for (double sh : shifts) {
            int count = 0;
            for (const SceneDrop& d : m.drops)
                for (const Active& a : active)
                    if (similar(d.width, a.width) && std::abs(d.xc - sh - a.x) < tol(d.width)) {
                        ++count;
                        break;
                    }
            if (count > bestCount || (count == bestCount && std::abs(sh) < std::abs(shift))) bestCount = count, shift = sh;
        }
        // nearest shifted match, closest pairs first
        struct Pair {
            double dist;
            std::size_t d, a;
        };
        std::vector<Pair> pairs;
        for (std::size_t i = 0; i < m.drops.size(); ++i)
            for (std::size_t k = 0; k < active.size(); ++k) {
                const double dist = std::abs(m.drops[i].xc - shift - active[k].x);
                if (dist < tol(m.drops[i].width) && similar(m.drops[i].width, active[k].width)) pairs.push_back({dist, i, k});
            }
        std::sort(pairs.begin(), pairs.end(), [](const Pair& p, const Pair& q) { return p.dist < q.dist; });
        std::vector<int> match(m.drops.size(), -1);
        std::vector<bool> taken(active.size(), false);
        for (const Pair& p : pairs)
            if (match[p.d] < 0 && !taken[p.a]) match[p.d] = int(p.a), taken[p.a] = true;
        for (std::size_t i = 0; i < m.drops.size(); ++i) {
            SceneDrop& d = m.drops[i];
            std::size_t ti;
            if (match[i] >= 0) {
                Active& a = active[std::size_t(match[i])];
                ti = a.track;
                a.x = d.xc, a.width = d.width, a.t = m.t;
            } else {
                ti = tracks.size();
                tracks.push_back({0, {}});
                active.push_back({ti, d.xc, d.width, m.t});
            }
            tracks[ti].points.push_back(point(m.frame, m.t, std::move(d)));
        }
    }
    int id = 0;
    for (DropTrack& t : tracks) {
        int good = 0;
        for (const TrackPoint& p : t.points)
            good += p.drop.a.ok && std::none_of(p.drop.a.checks.begin(), p.drop.a.checks.end(),
                                                [](const Check& c) { return c.level == Check::Error; });
        if (int(t.points.size()) >= opt.minPoints && good > 0 && t.points.back().t - t.points.front().t >= opt.minDuration) {
            t.id = ++id;
            r.tracks.push_back(std::move(t));
        }
    }
    r.ok = true;
    return r;
}

}  // namespace

VideoInfo probeVideo(const QString& path) {
    VideoInfo v;
    if (!qEnvironmentVariableIsSet("LIBREDROP_FFMPEG_ONLY")) {   // Motion-JPEG (camera format, LibreDrop copies): no ffmpeg
        MjpegReader r;
        if (r.open(path)) {
            v.w = r.width(), v.h = r.height();
            v.times = r.times();
            v.native = true;
            v.format = r.format();
            return v;
        }
    }
    QByteArray out;
    if (!run(ffprobeProgram(),
             {"-v", "error", "-select_streams", "v:0", "-show_entries", "stream=width,height", "-of", "csv=p=0", path}, &out,
             &v.error))
        return v;
    const QList<QByteArray> wh = out.trimmed().split(',');
    if (wh.size() < 2) {
        v.error = QStringLiteral("no video stream in %1").arg(path);
        return v;
    }
    v.w = wh[0].toInt(), v.h = wh[1].toInt();
    if (!run(ffprobeProgram(),
             {"-v", "error", "-select_streams", "v:0", "-show_entries", "packet=pts_time", "-of", "csv=p=0", path}, &out,
             &v.error))
        return v;
    for (const QByteArray& line : out.split('\n')) {
        bool ok = false;
        const double t = line.trimmed().toDouble(&ok);
        if (ok) v.times.push_back(t);
    }
    std::sort(v.times.begin(), v.times.end());
    if (v.times.empty()) v.error = QStringLiteral("no frames in %1").arg(path);
    return v;
}

bool readFrames(const QString& path, const VideoInfo& info, int step, int first, int last,
                const std::function<bool(int, const GrayImage&)>& f, QString* error) {
    step = std::max(1, step);
    if (info.native) {   // read a batch of JPEGs, decode them in parallel, hand them on in order
        MjpegReader r;
        if (!r.open(path, error)) return false;
        const int batch = batchSize();
        const int end = std::min(last, r.frames() - 1);
        for (int i0 = std::max(0, first); i0 <= end; i0 += batch * step) {
            std::vector<int> idx;
            std::vector<QByteArray> data;
            for (int i = i0; i <= end && int(idx.size()) < batch; i += step) idx.push_back(i), data.push_back(r.jpeg(i));
            std::vector<GrayImage> ims = parallelMap(data, [](const QByteArray& d) {
                QImage q;
                return q.loadFromData(d, "JPG") ? fromQImage(q) : GrayImage{};
            });
            for (std::size_t k = 0; k < idx.size(); ++k) {
                if (ims[k].empty()) {
                    *error = QStringLiteral("frame %1 cannot be decoded").arg(idx[k]);
                    return false;
                }
                if (!f(idx[k], ims[k])) return true;
            }
        }
        return true;
    }
    const QString sel = QStringLiteral("select='between(n\\,%1\\,%2)*not(mod(n-%1\\,%3))'").arg(first).arg(last).arg(step);
    QProcess p;
    p.start(ffmpegProgram(), {"-v", "error", "-i", path, "-vf", sel, "-fps_mode", "passthrough", "-f", "rawvideo",
                              "-pix_fmt", "gray", "-"});
    if (!p.waitForStarted(10000)) {
        *error = QStringLiteral("cannot start ffmpeg (install it, or set LIBREDROP_FFMPEG)");
        return false;
    }
    const qint64 frameBytes = qint64(info.w) * info.h;
    QByteArray buf;
    int k = 0;
    bool stopped = false;
    while (!stopped) {
        if (!p.waitForReadyRead(60000) && p.state() != QProcess::Running && p.bytesAvailable() == 0) break;
        buf += p.readAllStandardOutput();
        while (buf.size() >= frameBytes) {
            GrayImage im(info.w, info.h);
            const auto* src = reinterpret_cast<const unsigned char*>(buf.constData());
            for (qint64 i = 0; i < frameBytes; ++i) im.px[std::size_t(i)] = src[i];
            buf.remove(0, frameBytes);
            if (!f(first + k * step, im)) {
                stopped = true;
                break;
            }
            ++k;
        }
    }
    if (stopped) {
        p.kill();
        p.waitForFinished(5000);
        return true;
    }
    p.waitForFinished(-1);
    if (p.exitCode() != 0) {
        *error = QStringLiteral("ffmpeg failed: %1").arg(QString::fromLocal8Bit(p.readAllStandardError()).trimmed());
        return false;
    }
    return true;
}

bool readFrameAt(const QString& path, const VideoInfo& info, int index, GrayImage* out, QString* error) {
    if (index < 0 || index >= int(info.times.size())) {
        *error = QStringLiteral("no frame %1").arg(index);
        return false;
    }
    if (info.native) {
        MjpegReader r;
        if (!r.open(path, error)) return false;
        if (!r.frame(index, out)) {
            *error = QStringLiteral("frame %1 cannot be decoded").arg(index);
            return false;
        }
        return true;
    }
    // seek a little before the frame's time, then take the first frame at or after it
    const double t = info.times[std::size_t(index)] - info.times[0];
    QByteArray raw;
    if (!run(ffmpegProgram(),
             {"-v", "error", "-ss", QString::number(std::max(0.0, t - 0.002), 'f', 4), "-i", path, "-frames:v", "1", "-f",
              "rawvideo", "-pix_fmt", "gray", "-"},
             &raw, error))
        return false;
    if (raw.size() < qint64(info.w) * info.h) {
        *error = QStringLiteral("frame %1 not decoded").arg(index);
        return false;
    }
    GrayImage im(info.w, info.h);
    const auto* src = reinterpret_cast<const unsigned char*>(raw.constData());
    for (std::size_t i = 0; i < im.px.size(); ++i) im.px[i] = src[i];
    *out = std::move(im);
    return true;
}

VideoResult analyzeVideo(const QString& path, const VideoOptions& opt, const Progress& progress) {
    VideoResult r;
    const VideoInfo info = probeVideo(path);
    if (!info.error.isEmpty()) {
        r.error = info.error;
        return r;
    }
    const int n = int(info.times.size());
    int first = 0, last = n - 1;
    while (first < n && info.times[std::size_t(first)] - info.times[0] < opt.start) ++first;
    while (last > first && info.times[std::size_t(last)] - info.times[0] > opt.end) --last;
    if (first > last) {
        r.error = QStringLiteral("no frames between %1 and %2 s").arg(opt.start).arg(opt.end);
        return r;
    }
    // pass 1: surface
    std::vector<Scene> found;
    const int sampleStep = std::max(1, (last - first + 1) / std::max(1, opt.surfaceSamples));
    QString err;
    {
        SceneOptions so1 = opt.scene;
        so1.surface.reset();
        std::vector<GrayImage> batch;
        auto flush1 = [&] {
            for (Scene& s : parallelMap(batch, [&](const GrayImage& im) { return analyzeScene(im, so1); }))
                if (s.ok && !s.drops.empty()) found.push_back(std::move(s));
            batch.clear();
        };
        if (!readFrames(path, info, sampleStep, first, last, [&](int, const GrayImage& im) {
                batch.push_back(im);
                if (int(batch.size()) >= batchSize()) flush1();
                return !(opt.cancel && *opt.cancel);
            }, &err)) {
            r.error = err;
            return r;
        }
        flush1();
    }
    if (opt.cancel && *opt.cancel) {
        r.error = QStringLiteral("cancelled");
        return r;
    }
    const std::optional<Baseline> surface = opt.scene.surface ? opt.scene.surface : combineSurfaces(found);
    if (!surface) {
        r.error = QStringLiteral("no drop on a surface in any sampled frame");
        return r;
    }
    // pass 2: every step-th frame with the fixed surface; frames are measured in parallel batches
    std::vector<Measured> frames;
    const int total = (last - first) / std::max(1, opt.step) + 1;
    SceneOptions so = opt.scene;
    so.surface = surface;
    std::vector<std::pair<int, GrayImage>> pending;
    auto flush = [&] {
        std::vector<Scene> scenes =
            parallelMap(pending, [&](const std::pair<int, GrayImage>& p) { return analyzeScene(p.second, so); });
        for (std::size_t k = 0; k < pending.size(); ++k) {
            const int idx = pending[k].first;
            frames.push_back({idx, info.times[std::size_t(idx)] - info.times[0], std::move(scenes[k].drops)});
        }
        pending.clear();
        if (progress) progress(int(frames.size()), total);
    };
    if (!readFrames(path, info, opt.step, first, last, [&](int idx, const GrayImage& im) {
            pending.emplace_back(idx, im);
            if (int(pending.size()) >= batchSize()) flush();
            return !(opt.cancel && *opt.cancel);
        }, &err)) {
        r.error = err;
        return r;
    }
    flush();
    if (opt.cancel && *opt.cancel) {
        r.error = QStringLiteral("cancelled");
        return r;
    }
    r = track(std::move(frames), opt);
    r.info = info;
    r.surface = *surface;
    r.surfaceFrames = int(found.size());
    return r;
}

VideoResult analyzeImages(const QStringList& files, const VideoOptions& opt, const Progress& progress) {
    std::vector<Scene> found;
    std::vector<GrayImage> images;
    for (const QString& f : files) {
        const QImage img(f);
        if (img.isNull()) {
            VideoResult r;
            r.error = QStringLiteral("cannot read %1").arg(f);
            return r;
        }
        images.push_back(fromQImage(img));
    }
    const int sampleStep = std::max(1, int(images.size()) / std::max(1, opt.surfaceSamples));
    for (std::size_t i = 0; i < images.size(); i += std::size_t(sampleStep)) {
        SceneOptions so = opt.scene;
        so.surface.reset();
        Scene s = analyzeScene(images[i], so);
        if (s.ok && !s.drops.empty()) found.push_back(std::move(s));
    }
    const std::optional<Baseline> surface = opt.scene.surface ? opt.scene.surface : combineSurfaces(found);
    if (!surface) {
        VideoResult r;
        r.error = QStringLiteral("no drop on a surface in any image");
        return r;
    }
    std::vector<Measured> frames;
    const double fps = opt.fps > 0 ? opt.fps : 1;
    for (std::size_t i = 0; i < images.size(); i += std::size_t(std::max(1, opt.step))) {
        SceneOptions so = opt.scene;
        so.surface = surface;
        Scene s = analyzeScene(images[i], so);
        frames.push_back({int(i), double(i) / fps, std::move(s.drops)});
        if (progress) progress(int(frames.size()), int(images.size()));
    }
    VideoResult r = track(std::move(frames), opt);
    r.surface = *surface;
    r.surfaceFrames = int(found.size());
    return r;
}

bool writeCompact(const QString& in, const QString& out, const CompactOptions& opt, const Progress& progress, QString* error) {
    const VideoInfo info = probeVideo(in);
    if (!info.error.isEmpty()) {
        *error = info.error;
        return false;
    }
    const int n = int(info.times.size());
    int first = 0, last = n - 1;
    while (first < n && info.times[std::size_t(first)] - info.times[0] < opt.start) ++first;
    while (last > first && info.times[std::size_t(last)] - info.times[0] > opt.end) --last;
    const int y0 = std::clamp(opt.y0, 0, info.h - 1), y1 = opt.y1 < 0 ? info.h - 1 : std::clamp(opt.y1, y0, info.h - 1);
    MjpegWriter w;
    if (!w.open(out, info.w, y1 - y0 + 1, error)) return false;
    const int total = (last - first) / std::max(1, opt.step) + 1;
    int done = 0;
    std::vector<std::pair<int, GrayImage>> pending;
    bool ok = true;
    auto flush = [&] {
        const std::vector<QByteArray> jpegs = parallelMap(pending, [&](const std::pair<int, GrayImage>& p) {
            return encodeJpeg(crop(p.second, 0, y0, info.w, y1 - y0 + 1), opt.quality);
        });
        for (std::size_t k = 0; k < pending.size(); ++k)
            ok = ok && w.add(jpegs[k], info.times[std::size_t(pending[k].first)] - info.times[std::size_t(first)]);
        done += int(pending.size());
        pending.clear();
        if (progress) progress(done, total);
    };
    if (!readFrames(in, info, opt.step, first, last, [&](int idx, const GrayImage& im) {
            pending.emplace_back(idx, im);
            if (int(pending.size()) >= batchSize()) flush();
            return ok && !(opt.cancel && *opt.cancel);
        }, error))
        return false;
    flush();
    ok = w.finish() && ok;
    if (opt.cancel && *opt.cancel) {
        QFile::remove(out);
        *error = QStringLiteral("cancelled");
        return false;
    }
    if (!ok) *error = QStringLiteral("cannot write %1").arg(out);
    return ok;
}

void suggestCrop(const VideoResult& r, int* y0, int* y1) {
    double hMax = 0;
    for (const DropTrack& t : r.tracks)
        for (const TrackPoint& p : t.points)
            if (p.drop.a.ok && !p.drop.onNeedle &&
                std::none_of(p.drop.a.checks.begin(), p.drop.a.checks.end(), [](const Check& c) { return c.level == Check::Error; }))
                hMax = std::max(hMax, p.drop.a.heightPx);
    const double ys = r.surface.p0.y, tilt = std::abs(std::tan(r.surface.angle)) * r.info.w;
    *y0 = std::max(0, int(std::floor(ys - tilt - std::max(2.5 * hMax + 60, 200.0))));
    *y1 = std::min(r.info.h > 0 ? r.info.h - 1 : 1 << 20, int(std::ceil(ys + tilt + std::max(1.2 * hMax + 40, 80.0))));
    // whole 16-row blocks (JPEG works in 8- or 16-pixel blocks)
    *y0 -= *y0 % 16;
    *y1 = *y0 + ((*y1 - *y0 + 16) / 16) * 16 - 1;
    if (r.info.h > 0) *y1 = std::min(*y1, r.info.h - 1);
}

}  // namespace ld
