// LibreDropCLI: contact angle of drop images, and synthetic test drops.
//   LibreDropCLI analyze IMAGE... [--scale PX_PER_MM --density KG_M3 --tension MN_M]
//   LibreDropCLI scene IMAGE... [same options]: every drop on the surface of a whole frame
//   LibreDropCLI video FILE|IMAGES... [same options] [--step N] [--start S] [--end S] [--fps F] [-o CSV] [--plot PNG]
//     --drops 2,3: only these drops in the summary, table and plot (ids from a first run)
//   LibreDropCLI report TABLE.csv [--drops 2,3] [--plot PNG]: summary and plot again from a saved table
//   LibreDropCLI synth --theta DEG [--bond B] [--radius PX] [--tilt DEG] [--needle GAP_PX] -o FILE.png
#include <QCoreApplication>
#include <QGuiApplication>
#include <QImage>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstdio>

#include "core/camera.h"
#include "core/drop.h"
#include "core/recorder.h"
#include "core/simulate.h"
#include "core/mjpeg.h"
#include "core/thermo.h"
#include "core/image.h"
#include "core/pendant.h"
#include "core/report.h"
#include "core/scene.h"
#include "core/video.h"

namespace {
QString arg(const QStringList& a, const QString& name, const QString& def = {}) {
    const auto i = a.indexOf(name);
    return i >= 0 && i + 1 < a.size() ? a[i + 1] : def;
}
void out(const QString& s) { std::fputs(qPrintable(s + QLatin1Char('\n')), stdout); }

QList<int> dropIds(const QStringList& a) {
    QList<int> ids;
    for (const QString& x : arg(a, "--drops").split(QLatin1Char(','), Qt::SkipEmptyParts)) ids << x.toInt();
    return ids;
}

void printSummaries(const ld::SeriesSet& set, const ld::SoakOptions& so = {}) {
    const QString L = set.lenUnit, V = set.volUnit;
    for (const ld::DropSeries& d : set.drops) {
        const ld::TrackSummary s = ld::summarize(d);
        out(QStringLiteral("drop %1 at x %2 %3: touched at %4 s, %5 frames (%6 not measurable)")
                .arg(s.id).arg(s.x, 0, 'f', 2).arg(L).arg(s.tContact, 0, 'f', 2).arg(s.points).arg(s.failed));
        if (s.tRelease < 0) {
            out(QStringLiteral("  the needle stays in the drop"));
            continue;
        }
        out(QStringLiteral("  free from %1 s (%2 s after contact) to %3 s")
                .arg(s.tRelease, 0, 'f', 2).arg(s.tRelease - s.tContact, 0, 'f', 2).arg(s.tEnd, 0, 'f', 2));
        out(QStringLiteral("  angle %1° -> %2° (%3°/s), base %4 -> %5 %6, volume %7 -> %8 %9 (%10 %9/s)")
                .arg(s.angleRelease, 0, 'f', 1).arg(s.angleEnd, 0, 'f', 1).arg(s.angleRate, 0, 'f', 3)
                .arg(s.baseRelease, 0, 'f', 3).arg(s.baseEnd, 0, 'f', 3).arg(L)
                .arg(s.volumeRelease, 0, 'f', 3).arg(s.volumeEnd, 0, 'f', 3).arg(V).arg(s.volumeRate, 0, 'g', 3));
        const ld::Soaking k = ld::soak(d, so);
        QStringList at;
        for (std::size_t i = 0; i < so.times.size(); ++i)
            at << QStringLiteral("%1 s: %2°").arg(so.times[i]).arg(k.angleAt[i], 0, 'f', 1);
        out(QStringLiteral("  angle after contact %1; sorptivity %2 %3/s^0.5 (R² %4)%5")
                .arg(at.join(QStringLiteral(", "))).arg(k.sorptivity, 0, 'g', 3).arg(L).arg(k.sorptivityR2, 0, 'f', 3)
                .arg(std::isfinite(k.absorbedAfter) ? QStringLiteral("; absorbed after %1 s").arg(k.absorbedAfter, 0, 'f', 2)
                                                    : QStringLiteral("; still there at the end")));
    }
}

ld::SoakOptions soakOptions(const QStringList& a, double videoEnd) {
    ld::SoakOptions so;
    const QString times = arg(a, "--at");
    if (!times.isEmpty()) {
        so.times.clear();
        for (const QString& x : times.split(QLatin1Char(','), Qt::SkipEmptyParts)) so.times.push_back(x.toDouble());
    }
    so.evaporation = arg(a, "--evaporation", "0").toDouble();
    so.videoEnd = videoEnd;
    return so;
}

bool savePlot(const QStringList& a, const ld::SeriesSet& set) {
    const QString png = arg(a, "--plot");
    if (png.isEmpty()) return true;
    if (!ld::plotSeries(set).save(png)) {
        out(QStringLiteral("cannot write ") + png);
        return false;
    }
    out(QStringLiteral("plot: ") + png);
    return true;
}
}  // namespace

int main(int argc, char** argv) {
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");   // plots need fonts
    QGuiApplication app(argc, argv);
    const QStringList a = app.arguments().mid(1);
    if (a.isEmpty() || a[0] == QLatin1String("--help")) {
        out(QStringLiteral("LibreDropCLI analyze IMAGE... [--scale PX_PER_MM --density KG_M3 --tension MN_M]\n"
                           "LibreDropCLI scene IMAGE... [same options]\n"
                           "LibreDropCLI video FILE|IMAGES... [same options] [--step N] [--start S] [--end S] [--fps F] "
                           "[-o CSV] [--plot PNG] [--drops 2,3]\n"
                           "LibreDropCLI report TABLE.csv [--drops 2,3] [--plot PNG]\n"
                           "LibreDropCLI synth --theta DEG [--bond B] [--radius PX] [--tilt DEG] [--needle GAP_PX] -o FILE\n"
                           "LibreDropCLI cameras | record OUT.mkv [--camera DEV] [--size WxH] [--seconds S] ...\n"
                           "LibreDropCLI simulate PRESET -o OUT.mkv (simulated drops with the true values; no PRESET: list)"));
        return a.isEmpty() ? 1 : 0;
    }
    if (a[0] == QLatin1String("cameras")) {   // cameras, their modes and settings
        for (const ld::CameraInfo& c : ld::Camera::list(false)) {
            out(c.id + QStringLiteral("  ") + c.name);
            QString err;
            auto cam = ld::Camera::open(c.id, &err);
            if (!cam) { out(QStringLiteral("  ") + err); continue; }
            for (const ld::CameraMode& m : cam->modes()) out(QStringLiteral("  %1x%2 @ %3 fps").arg(m.w).arg(m.h).arg(m.fps, 0, 'f', 1));
            for (const ld::CameraControl& k : cam->controls())
                out(QStringLiteral("  %1 = %2 (%3..%4, default %5)").arg(k.name).arg(k.value).arg(k.min).arg(k.max).arg(k.def));
        }
        return 0;
    }
    if (a[0] == QLatin1String("record")) {   // record from a camera
        if (a.size() < 2 || a[1].startsWith(QLatin1String("--"))) {
            out(QStringLiteral("LibreDropCLI record OUT.mkv [--camera /dev/videoN|sim:FILE] [--size 640x480] [--fps F] [--seconds S]\n"
                               "  [--exposure 39 (x100 us, Ossila's settings)] [--sample NAME --side S --direction D --liquid L --volume UL --note TEXT]"));
            return 1;
        }
        QString id = arg(a, "--camera");
        if (id.isEmpty()) {
            const auto cams = ld::Camera::list(false);
            if (cams.empty()) { out(QStringLiteral("no camera found")); return 1; }
            id = cams.front().id;
        }
        QString err;
        auto cam = ld::Camera::open(id, &err);
        if (!cam) { out(err); return 1; }
        const QString size = arg(a, "--size", "1920x1080");
        ld::CameraMode want{size.section(QLatin1Char('x'), 0, 0).toInt(), size.section(QLatin1Char('x'), 1, 1).toInt(), 0};
        for (const ld::CameraMode& m : cam->modes()) if (m.w == want.w && m.h == want.h) want.fps = m.fps;
        if (a.contains(QStringLiteral("--fps"))) want.fps = arg(a, "--fps").toDouble();
        const int exposure = arg(a, "--exposure", "39").toInt();
        if (!cam->start(want, &err)) { out(err); return 1; }
        cam->applyOssilaSettings(exposure);   // after starting: opening the stream can reset controls
        QJsonObject meta, sample, ctl;
        meta[QStringLiteral("device")] = cam->name();
        const ld::CameraMode m = cam->mode();
        meta[QStringLiteral("size")] = QStringLiteral("%1x%2").arg(m.w).arg(m.h);
        meta[QStringLiteral("fps_requested")] = m.fps;
        meta[QStringLiteral("exposure_100us")] = exposure;
        meta[QStringLiteral("note")] = arg(a, "--note");
        for (const auto& [k, v] : cam->settings()) ctl[k] = v;
        meta[QStringLiteral("controls")] = ctl;
        for (const char* k : {"sample", "batch", "side", "direction", "liquid"})
            if (const QString v = arg(a, QStringLiteral("--") + QLatin1String(k)); !v.isEmpty()) sample[QLatin1String(k)] = v;
        if (a.contains(QStringLiteral("--volume"))) sample[QStringLiteral("volume_ul")] = arg(a, "--volume").toDouble();
        meta[QStringLiteral("sample")] = sample;
        if (const ld::ThermoReading room = ld::roomConditions(ld::readThermometers()); std::isfinite(room.temperature)) {
            meta[QStringLiteral("temperature_c")] = std::round(room.temperature * 100) / 100;
            if (std::isfinite(room.humidity)) meta[QStringLiteral("humidity_pct")] = std::round(room.humidity * 10) / 10;
            meta[QStringLiteral("sensor")] = room.name.isEmpty() ? room.model : room.name;
        }
        ld::Recorder rec;
        rec.preSeconds = 0;
        if (!rec.start(a[1], m.w, m.h, meta, &err)) { out(err); return 1; }
        const double seconds = arg(a, "--seconds", "5").toDouble();
        ld::CameraFrame f;
        double t0 = -1;
        long long bytes = 0;
        double darkest = 255, brightest = 0;
        while (cam->grab(&f, &err)) {
            if (t0 < 0) t0 = f.t;
            bytes += f.jpeg.size();
            if (rec.frames() % 30 == 0) {   // mean grey level of some frames: a black image means no light
                QImage im = QImage::fromData(f.jpeg, "JPEG").convertToFormat(QImage::Format_Grayscale8).scaled(64, 36);
                double s = 0;
                for (int y = 0; y < im.height(); ++y) for (int x = 0; x < im.width(); ++x) s += im.constScanLine(y)[x];
                s /= std::max(1, im.width() * im.height());
                darkest = std::min(darkest, s), brightest = std::max(brightest, s);
            }
            if (!rec.push(f, &err)) break;
            if (f.t - t0 >= seconds) break;
        }
        cam->stop();
        QString err2;
        rec.stop(&err2);
        if (!err.isEmpty()) out(err);
        if (!err2.isEmpty()) out(err2);
        out(QStringLiteral("%1: %2 frames in %3 s = %4 fps, %5 dropped, %6 kB/frame, mean grey %7..%8 of 255, %9x%10")
                .arg(a[1]).arg(rec.frames()).arg(rec.duration(), 0, 'f', 2).arg(rec.fps(), 0, 'f', 1).arg(rec.dropped())
                .arg(rec.frames() ? bytes / 1024.0 / rec.frames() : 0, 0, 'f', 1).arg(darkest, 0, 'f', 0).arg(brightest, 0, 'f', 0)
                .arg(m.w).arg(m.h));
        return rec.frames() > 0 && err2.isEmpty() ? 0 : 1;
    }
    if (a[0] == QLatin1String("simulate")) {   // a simulated recording with the true values
        ld::SimSpec sp;
        if (a.size() < 2 || a[1].startsWith(QLatin1String("--")) || !ld::simPreset(a[1], &sp)) {
            out(QStringLiteral("LibreDropCLI simulate PRESET -o OUT.mkv [--seconds S] [--fps F] [--seed N] [--half] [--truth TRUTH.csv]\npresets:"));
            for (const ld::SimSpec& p : ld::simPresets()) out(QStringLiteral("  %1  %2").arg(p.name, -14).arg(p.title));
            return 1;
        }
        if (a.contains(QStringLiteral("--seconds"))) sp.seconds = arg(a, "--seconds").toDouble();
        if (a.contains(QStringLiteral("--fps"))) sp.fps = arg(a, "--fps").toDouble();
        if (a.contains(QStringLiteral("--seed"))) sp.seed = arg(a, "--seed").toUInt();
        if (a.contains(QStringLiteral("--half"))) sp = ld::simScaled(sp, 0.5);
        const QString file = arg(a, "-o", sp.name + QStringLiteral(".mkv"));
        QString truthFile = arg(a, "--truth");
        if (truthFile.isEmpty()) truthFile = file.left(file.lastIndexOf(QLatin1Char('.'))) + QStringLiteral("_truth.csv");
        ld::MjpegWriter w;
        QString err;
        if (!w.open(file, sp.width, sp.height, &err)) { out(err); return 1; }
        QFile tf(truthFile);
        if (!tf.open(QIODevice::WriteOnly | QIODevice::Text)) { out(QStringLiteral("cannot write ") + truthFile); return 1; }
        tf.write("time_s,on_surface,theta_deg,base_mm,height_mm,volume_ul,contact_left_px,contact_right_px,baseline_y_px\n");
        const int n = int(std::lround(sp.seconds * sp.fps));
        for (int i = 0; i < n; ++i) {
            const double t = i / sp.fps;
            ld::SimTruth tr;
            const ld::GrayImage im = ld::simFrame(sp, t, &tr);
            w.add(ld::encodeJpeg(im, 92), t);
            tf.write(QStringLiteral("%1,%2,%3,%4,%5,%6,%7,%8,%9\n").arg(t, 0, 'f', 4).arg(int(tr.onSurface)).arg(tr.thetaDeg, 0, 'f', 3)
                         .arg(tr.baseMm, 0, 'f', 4).arg(tr.heightMm, 0, 'f', 4).arg(tr.volumeUl, 0, 'f', 4)
                         .arg(tr.contactLeftX, 0, 'f', 2).arg(tr.contactRightX, 0, 'f', 2).arg(tr.baselineY, 0, 'f', 2).toUtf8());
            if (i % 30 == 0) std::fprintf(stderr, "\r%d/%d frames", i, n);
        }
        std::fprintf(stderr, "\r%d/%d frames\n", n, n);
        w.finish();
        QJsonObject meta, sample;
        meta[QStringLiteral("device")] = QStringLiteral("Simulator: ") + sp.title;
        meta[QStringLiteral("size")] = QStringLiteral("%1x%2").arg(sp.width).arg(sp.height);
        meta[QStringLiteral("fps_requested")] = sp.fps;
        meta[QStringLiteral("frames")] = n;
        meta[QStringLiteral("started")] = QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
        meta[QStringLiteral("note")] = QStringLiteral("simulated (%1, seed %2); true values in %3").arg(sp.name).arg(sp.seed).arg(QFileInfo(truthFile).fileName());
        sample[QStringLiteral("sample")] = QStringLiteral("sim ") + sp.name;
        sample[QStringLiteral("liquid")] = QStringLiteral("Water");
        sample[QStringLiteral("volume_ul")] = sp.volumeUl;
        meta[QStringLiteral("sample")] = sample;
        QFile jf(file.left(file.lastIndexOf(QLatin1Char('.'))) + QStringLiteral(".json"));
        if (jf.open(QIODevice::WriteOnly)) jf.write(QJsonDocument(meta).toJson());
        out(file + QStringLiteral("  (true values: ") + truthFile + QStringLiteral(")"));
        return 0;
    }
    if (a[0] == QLatin1String("synth")) {
        ld::SynthDrop d;
        d.thetaDeg = arg(a, "--theta", "90").toDouble();
        d.bond = arg(a, "--bond", "0.1").toDouble();
        d.apexRadiusPx = arg(a, "--radius", "200").toDouble();
        d.tiltDeg = arg(a, "--tilt", "0").toDouble();
        if (a.contains("--needle")) {
            d.needle = true;
            d.needleGapPx = arg(a, "--needle").toDouble();
        }
        const QString file = arg(a, "-o", "drop.png");
        if (!ld::toQImage(ld::renderDrop(d)).save(file)) {
            out(QStringLiteral("cannot write ") + file);
            return 1;
        }
        out(file);
        return 0;
    }
    if (a[0] == QLatin1String("compact")) {   // compact copy: band around the surface, grey, JPEG quality
        if (a.size() < 3) {
            out(QStringLiteral("LibreDropCLI compact IN OUT.mkv [--quality Q] [--rows Y0,Y1] [--step N]"));
            return 1;
        }
        ld::CompactOptions co;
        co.quality = arg(a, "--quality", "85").toInt();
        co.step = arg(a, "--step", "1").toInt();
        const QString rows = arg(a, "--rows");
        if (!rows.isEmpty()) {
            co.y0 = rows.section(QLatin1Char(','), 0, 0).toInt(), co.y1 = rows.section(QLatin1Char(','), 1, 1).toInt();
        } else {   // from a quick analysis (every 10th frame)
            ld::VideoOptions vo;
            vo.step = 10;
            const ld::VideoResult r = ld::analyzeVideo(a[1], vo);
            if (!r.ok) {
                out(QStringLiteral("no drops found to crop to (%1): give --rows Y0,Y1").arg(r.error));
                return 1;
            }
            ld::suggestCrop(r, &co.y0, &co.y1);
        }
        out(QStringLiteral("rows %1..%2, quality %3").arg(co.y0).arg(co.y1).arg(co.quality));
        QString err;
        if (!ld::writeCompact(a[1], a[2], co, {}, &err)) {
            out(err);
            return 1;
        }
        out(QStringLiteral("%1: %2 MB (from %3 MB)")
                .arg(a[2]).arg(QFileInfo(a[2]).size() / 1e6, 0, 'f', 1).arg(QFileInfo(a[1]).size() / 1e6, 0, 'f', 1));
        return 0;
    }
    if (a[0] == QLatin1String("report")) {   // summary and plot of chosen drops from a saved table
        QFile f(a.value(1));
        if (a.size() < 2 || !f.open(QIODevice::ReadOnly)) {
            out(QStringLiteral("cannot read ") + a.value(1));
            return 1;
        }
        ld::SeriesSet set;
        QString err;
        if (!ld::readTracksCsv(QString::fromUtf8(f.readAll()), &set, &err)) {
            out(err);
            return 1;
        }
        double tEnd = 0;   // (the table doesn't keep the recording's end: the last measured frame)
        for (const ld::DropSeries& d : set.drops)
            if (!d.t.empty()) tEnd = std::max(tEnd, d.t.back());
        ld::selectDrops(&set, dropIds(a));
        printSummaries(set, soakOptions(a, tEnd));
        return savePlot(a, set) ? 0 : 1;
    }
    if (a[0] == QLatin1String("video")) {
        ld::VideoOptions vo;
        vo.scene.drop.pxPerMm = arg(a, "--scale", "0").toDouble();
        vo.scene.drop.densityDiff = arg(a, "--density", "0").toDouble();
        vo.scene.drop.surfaceTension = arg(a, "--tension", "0").toDouble();
        vo.scene.drop.staticMeasurement = false;
        vo.step = arg(a, "--step", "1").toInt();
        vo.start = arg(a, "--start", "0").toDouble();
        if (a.contains("--end")) vo.end = arg(a, "--end").toDouble();
        vo.fps = arg(a, "--fps", "0").toDouble();
        QStringList inputs;
        for (int i = 1; i < a.size(); ++i) {
            if (a[i].startsWith(QLatin1String("-"))) ++i;
            else inputs << a[i];
        }
        if (inputs.isEmpty()) {
            out(QStringLiteral("no input"));
            return 1;
        }
        const bool images = inputs.size() > 1 || !QImage(inputs[0]).isNull();
        auto progress = [](int done, int total) {
            if (done % 100 == 0 || done == total) std::fprintf(stderr, "\r%d/%d frames", done, total);
            if (done == total) std::fputc('\n', stderr);
        };
        const ld::VideoResult r = images ? ld::analyzeImages(inputs, vo, progress) : ld::analyzeVideo(inputs[0], vo, progress);
        if (!r.ok) {
            out(r.error);
            return 1;
        }
        const ld::Units u{vo.scene.drop.pxPerMm};
        out(QStringLiteral("%1 frames measured; surface y %2 px (tilt %3°, from %4 frames); %5 drops")
                .arg(r.frames).arg(r.surface.p0.y, 0, 'f', 1).arg(r.surface.angle * 180 / M_PI, 0, 'f', 2)
                .arg(r.surfaceFrames).arg(r.tracks.size()));
        ld::SeriesSet set = ld::seriesOf(r, u);
        double tEnd = r.info.times.empty() ? 0 : r.info.times.back();
        for (const ld::DropSeries& d : set.drops)
            if (!d.t.empty()) tEnd = std::max(tEnd, d.t.back());
        ld::selectDrops(&set, dropIds(a));
        printSummaries(set, soakOptions(a, tEnd));
        const QString csv = arg(a, "-o");
        if (!csv.isEmpty()) {
            QFile f(csv);
            if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                out(QStringLiteral("cannot write ") + csv);
                return 1;
            }
            f.write(ld::tracksCsv(r, u, dropIds(a)).toUtf8());
            out(QStringLiteral("table: ") + csv);
        }
        if (!savePlot(a, set)) return 1;
        return 0;
    }
    if (a[0] == QLatin1String("pendant")) {   // surface tension from a hanging drop (image, or video frames)
        ld::PendantOptions po;
        po.pxPerMm = arg(a, "--scale", "0").toDouble();
        po.densityDiff = arg(a, "--density", "997").toDouble();
        po.needleMm = arg(a, "--needle", "0").toDouble();
        const int bottom = arg(a, "--bottom", "0").toInt();   // use rows above this one only (the surface)
        auto run = [&](const ld::GrayImage& full, const QString& what) {
            const ld::GrayImage im = bottom > 0 ? ld::crop(full, 0, 0, full.w, std::min(bottom, full.h)) : full;
            const ld::PendantResult r = ld::analyzePendant(im, po);
            if (!r.ok) {
                out(what + QStringLiteral(": ") + r.error);
                return;
            }
            out(QStringLiteral("%1: gamma %2 mN/m, beta %3, apex radius %4 px, needle %5 px, volume %6 uL, Wo %7, rms %8 px")
                    .arg(what).arg(r.gamma, 0, 'f', 2).arg(r.bond, 0, 'f', 4).arg(r.apexRadiusPx, 0, 'f', 1)
                    .arg(r.needleWidthPx, 0, 'f', 1).arg(r.volumeUl, 0, 'f', 3).arg(r.worthington, 0, 'f', 2).arg(r.rmsPx, 0, 'f', 2));
            for (const ld::Check& c : r.checks) out(QStringLiteral("  ") + c.message);
        };
        const QString path = a.value(1);
        const QImage img(path);
        if (!img.isNull()) {
            run(ld::fromQImage(img), path);
            return 0;
        }
        const ld::VideoInfo info = ld::probeVideo(path);
        if (!info.error.isEmpty()) {
            out(info.error);
            return 1;
        }
        const double t0 = arg(a, "--from", "0").toDouble(), t1 = arg(a, "--to", QString::number(t0)).toDouble();
        const double every = arg(a, "--every", "0").toDouble();
        double last = -1e9;
        for (int i = 0; i < int(info.times.size()); ++i) {
            const double t = info.times[std::size_t(i)];
            if (t < t0 - 1e-6 || t > t1 + 1e-6 || t - last < every - 1e-6) continue;
            last = t;
            ld::GrayImage f;
            QString err;
            if (!ld::readFrameAt(path, info, i, &f, &err)) {
                out(err);
                return 1;
            }
            run(f, QStringLiteral("%1 s").arg(t, 0, 'f', 3));
        }
        return 0;
    }
    if (a[0] == QLatin1String("scene")) {
        ld::SceneOptions so;
        so.drop.pxPerMm = arg(a, "--scale", "0").toDouble();
        so.drop.densityDiff = arg(a, "--density", "0").toDouble();
        so.drop.surfaceTension = arg(a, "--tension", "0").toDouble();
        int rc = 0;
        for (int i = 1; i < a.size(); ++i) {
            if (a[i].startsWith(QLatin1String("--"))) {
                ++i;
                continue;
            }
            const QImage img(a[i]);
            const ld::Scene sc = ld::analyzeScene(ld::fromQImage(img), so);
            if (!sc.ok) {
                out(a[i] + QStringLiteral(": ") + sc.error);
                rc = 1;
                continue;
            }
            out(QStringLiteral("%1: surface y %2 at x %3, tilt %4°, %5 corners on it, %6 drops")
                    .arg(a[i]).arg(sc.surface.p0.y, 0, 'f', 1).arg(sc.surface.p0.x, 0, 'f', 0)
                    .arg(sc.surface.angle * 180 / M_PI, 0, 'f', 2).arg(sc.surfaceVotes).arg(sc.drops.size()));
            for (const ld::SceneDrop& d : sc.drops) {
                const ld::AngleResult* yl = d.a.method(QStringLiteral("young-laplace"));
                const ld::AngleResult* hw = d.a.method(QStringLiteral("height-width"));
                out(QStringLiteral("  drop x %1..%2%3: %4 YL %5/%6° hw %7° base %8 px height %9 px")
                        .arg(d.x0).arg(d.x1).arg(d.onNeedle ? " (needle)" : "")
                        .arg(d.a.ok ? QStringLiteral("ok") : d.a.error)
                        .arg(yl && yl->ok ? yl->left : ld::kNaN, 0, 'f', 1).arg(yl && yl->ok ? yl->right : ld::kNaN, 0, 'f', 1)
                        .arg(hw && hw->ok ? hw->left : ld::kNaN, 0, 'f', 1).arg(d.a.baseWidthPx, 0, 'f', 0).arg(d.a.heightPx, 0, 'f', 0));
                QStringList ms;
                for (const ld::AngleResult& m : d.a.methods)
                    ms << QStringLiteral("%1 %2/%3%4").arg(m.method).arg(m.left, 0, 'f', 1).arg(m.right, 0, 'f', 1).arg(m.ok ? "" : "(x)");
                out(QStringLiteral("    methods: ") + ms.join(QStringLiteral(", ")) +
                    QStringLiteral("; baseline y %1 tilt %2°").arg(d.a.baseline.p0.y, 0, 'f', 1).arg(d.a.baseline.angle * 180 / M_PI, 0, 'f', 2));
                for (const ld::Check& c : d.a.checks)
                    if (c.level != ld::Check::Info) out(QStringLiteral("    [%1] %2").arg(c.id, c.message));
            }
        }
        return rc;
    }
    if (a[0] != QLatin1String("analyze")) {
        out(QStringLiteral("unknown command ") + a[0]);
        return 1;
    }
    ld::DropOptions opt;
    opt.pxPerMm = arg(a, "--scale", "0").toDouble();
    opt.densityDiff = arg(a, "--density", "0").toDouble();
    opt.surfaceTension = arg(a, "--tension", "0").toDouble();
    int rc = 0;
    for (int i = 1; i < a.size(); ++i) {
        if (a[i].startsWith(QLatin1String("--"))) {
            ++i;
            continue;
        }
        const QImage img(a[i]);
        if (img.isNull()) {
            out(a[i] + QStringLiteral(": cannot read"));
            rc = 1;
            continue;
        }
        const ld::DropAnalysis r = ld::analyzeDrop(ld::fromQImage(img), opt);
        out(QStringLiteral("%1: %2").arg(a[i], r.ok ? QStringLiteral("ok") : r.error));
        out(QStringLiteral("  baseline tilt %1°, height %2 px, base %3 px, apex radius %4 px, Bond %5, edge %6 px")
                .arg(r.baseline.angle * 180 / M_PI, 0, 'f', 2).arg(r.heightPx, 0, 'f', 1).arg(r.baseWidthPx, 0, 'f', 1)
                .arg(r.apexRadiusPx, 0, 'f', 1).arg(r.bond, 0, 'f', 3).arg(r.edgeWidthPx, 0, 'f', 1));
        for (const ld::AngleResult& m : r.methods)
            out(m.ok ? QStringLiteral("  %1 left %2° right %3° (rms %4 px)")
                           .arg(m.method, -14).arg(m.left, 6, 'f', 2).arg(m.right, 6, 'f', 2).arg(m.rmsPx, 0, 'f', 2)
                     : QStringLiteral("  %1 —  %2").arg(m.method, -14).arg(m.note));
        for (const ld::Check& c : r.checks)
            out(QStringLiteral("  [%1] %2").arg(c.level == ld::Check::Error ? "error" : c.level == ld::Check::Warning ? "warning" : "info", c.message));
        if (!r.ok) rc = 1;
    }
    return rc;
}
