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
#include <QStringList>
#include <cstdio>

#include "core/drop.h"
#include "core/image.h"
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

void printSummaries(const ld::SeriesSet& set) {
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
    }
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
                           "LibreDropCLI synth --theta DEG [--bond B] [--radius PX] [--tilt DEG] [--needle GAP_PX] -o FILE"));
        return a.isEmpty() ? 1 : 0;
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
        ld::selectDrops(&set, dropIds(a));
        printSummaries(set);
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
        ld::selectDrops(&set, dropIds(a));
        printSummaries(set);
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
