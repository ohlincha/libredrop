#include "video.h"

#include <QImage>
#include <QProcess>
#include <algorithm>
#include <cmath>

namespace ld {

namespace {

QString ffmpegProgram() { return qEnvironmentVariable("LIBREDROP_FFMPEG", QStringLiteral("ffmpeg")); }

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
    std::vector<Baseline> found;
    const int sampleStep = std::max(1, (last - first + 1) / std::max(1, opt.surfaceSamples));
    QString err;
    if (!readFrames(path, info, sampleStep, first, last, [&](int, const GrayImage& im) {
            SceneOptions so = opt.scene;
            so.surface.reset();
            const Scene s = analyzeScene(im, so);
            if (s.ok && !s.drops.empty()) found.push_back(s.surface);
            return true;
        }, &err)) {
        r.error = err;
        return r;
    }
    const std::optional<Baseline> surface = opt.scene.surface ? opt.scene.surface : combineSurfaces(found);
    if (!surface) {
        r.error = QStringLiteral("no drop on a surface in any sampled frame");
        return r;
    }
    // pass 2: every step-th frame with the fixed surface
    std::vector<Measured> frames;
    const int total = (last - first) / std::max(1, opt.step) + 1;
    if (!readFrames(path, info, opt.step, first, last, [&](int idx, const GrayImage& im) {
            SceneOptions so = opt.scene;
            so.surface = surface;
            Scene s = analyzeScene(im, so);
            frames.push_back({idx, info.times[std::size_t(idx)] - info.times[0], std::move(s.drops)});
            if (progress) progress(int(frames.size()), total);
            return true;
        }, &err)) {
        r.error = err;
        return r;
    }
    r = track(std::move(frames), opt);
    r.surface = *surface;
    r.surfaceFrames = int(found.size());
    return r;
}

VideoResult analyzeImages(const QStringList& files, const VideoOptions& opt, const Progress& progress) {
    std::vector<Baseline> found;
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
        const Scene s = analyzeScene(images[i], so);
        if (s.ok && !s.drops.empty()) found.push_back(s.surface);
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

}  // namespace ld
