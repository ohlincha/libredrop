// LibreDropCLI: contact angle of drop images, and synthetic test drops.
//   LibreDropCLI analyze IMAGE... [--scale PX_PER_MM --density KG_M3 --tension MN_M]
//   LibreDropCLI synth --theta DEG [--bond B] [--radius PX] [--tilt DEG] [--needle GAP_PX] -o FILE.png
#include <QCoreApplication>
#include <QGuiApplication>
#include <QImage>
#include <QStringList>
#include <cstdio>

#include "core/drop.h"
#include "core/image.h"

namespace {
QString arg(const QStringList& a, const QString& name, const QString& def = {}) {
    const auto i = a.indexOf(name);
    return i >= 0 && i + 1 < a.size() ? a[i + 1] : def;
}
void out(const QString& s) { std::fputs(qPrintable(s + QLatin1Char('\n')), stdout); }
}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const QStringList a = app.arguments().mid(1);
    if (a.isEmpty() || a[0] == QLatin1String("--help")) {
        out(QStringLiteral("LibreDropCLI analyze IMAGE... [--scale PX_PER_MM --density KG_M3 --tension MN_M]\n"
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
