// Soaking analysis on constructed series: known sorptivity, evaporation, absorption.
#include <QtTest>
#include <cmath>

#include "core/report.h"

using namespace ld;

namespace {
// A drop touching at t = 2 s, free of the needle after 0.2 s; base spreads from 2 to 3 mm in the first second;
// volume V0 − S·∫A d√t − e·t until it is gone.
DropSeries soaking(double S, double e, double V0, double fps, double tMax) {
    DropSeries d;
    d.id = 1;
    double phi = 0, prevA = 0, prevT = 0;
    for (int i = 0;; ++i) {
        const double t = i / fps, base = 2.0 + std::min(1.0, t);
        const double A = M_PI * base * base / 4;
        if (i > 0) phi += 0.5 * (A + prevA) * (std::sqrt(t) - std::sqrt(prevT));
        prevA = A, prevT = t;
        const double V = V0 - S * phi - e * t;
        if (V < 0.05 * V0 || t > tMax) break;
        d.t.push_back(2.0 + t);
        d.angle.push_back(60 * std::exp(-t / 3));
        d.base.push_back(base);
        d.volume.push_back(V);
        d.left.push_back(kNaN), d.right.push_back(kNaN), d.height.push_back(kNaN);
        d.needle.push_back(t < 0.2);
    }
    return d;
}
}  // namespace

class TestSoak : public QObject {
    Q_OBJECT
private slots:
    void sorptivity() {
        const DropSeries d = soaking(0.05, 0.0, 2.0, 30, 60);
        SoakOptions o;
        o.videoEnd = 100;
        const Soaking k = soak(d, o);
        qInfo("S %.5f (want 0.05), R2 %.4f, absorbed after %.2f s, angle at 1 s %.2f", k.sorptivity, k.sorptivityR2,
              k.absorbedAfter, k.angleAt[1]);
        QVERIFY(std::abs(k.sorptivity / 0.05 - 1) < 0.01);
        QVERIFY(std::isfinite(k.absorbedAfter) && k.absorbedAfter > 1);
        QVERIFY(std::abs(k.angleAt[1] - 60 * std::exp(-1.0 / 3)) < 0.5);
    }
    void evaporationCorrected() {
        const DropSeries d = soaking(0.02, 0.01, 2.0, 30, 20);
        SoakOptions o;
        o.evaporation = 0.01;
        o.videoEnd = 22;   // the recording ends with the drop still there
        const Soaking k = soak(d, o);
        QVERIFY(std::abs(k.sorptivity / 0.02 - 1) < 0.01);
        QVERIFY(!std::isfinite(k.absorbedAfter));
    }
};

QTEST_GUILESS_MAIN(TestSoak)
#include "test_soak.moc"
