// Surface tension from a synthetic pendant drop of known Bond number and apex radius.
#include <QtTest>
#include <cmath>
#include <random>

#include "core/image.h"
#include "core/pendant.h"
#include "core/younglaplace.h"

using namespace ld;

namespace {

// A backlit pendant drop (β < 0, apex radius b px) hanging from a needle whose tip sits where the outline has
// narrowed to `neck` of its widest; 4×4 supersampled, a little noise.
GrayImage pendant(double beta, double b, double neck, double* needleW, double* tipY, double* apexY) {
    const Profile pr = integrate(beta, M_PI - 1e-4, 0.001);
    std::size_t iMax = 0;   // the widest point: where the outline turns inwards (φ = 90°)
    while (iMax + 1 < pr.X.size() && pr.phi[iMax] < M_PI / 2) ++iMax;
    std::size_t iTip = iMax;
    while (iTip + 1 < pr.X.size() && pr.X[iTip] > neck * pr.X[iMax]) ++iTip;
    const double zTip = pr.Z[iTip], rNeedle = pr.X[iTip] * b;
    const int w = int(4 * pr.X[iMax] * b) + 60, h = int(zTip * b) + 160;
    const double x0 = w / 2.0 + 0.3, yA = h - 60.4;   // apex
    *needleW = 2 * rNeedle, *tipY = yA - zTip * b, *apexY = yA;
    GrayImage im(w, h, 220);
    std::mt19937 rng(7);
    std::normal_distribution<float> noise(0, 1.5f);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            int dark = 0;
            for (int sy = 0; sy < 4; ++sy)
                for (int sx = 0; sx < 4; ++sx) {
                    const double px = x - 0.5 + (sx + 0.5) / 4, py = y - 0.5 + (sy + 0.5) / 4;
                    const double z = (yA - py) / b;
                    double half = 0;
                    if (py < *tipY) half = rNeedle;
                    else if (z >= 0) {
                        double X;
                        if (pr.atDepth(z, &X)) half = X * b;
                    }
                    dark += std::abs(px - x0) <= half;
                }
            im.at(x, y) = float(220 - (220 - 25) * dark / 16.0) + noise(rng);
        }
    return im;
}

}  // namespace

class TestPendant : public QObject {
    Q_OBJECT
private slots:
    void surfaceTension() {
        // water-like: b = 1.5 mm at 100 px/mm, β = −0.3 → γ = 997·9.81·(1.5e-3)²/0.3 = 73.35 mN/m
        for (double beta : {-0.2, -0.3, -0.45}) {
            double nw, tipY, apexY;
            const GrayImage im = pendant(beta, 150, 0.8, &nw, &tipY, &apexY);
            PendantOptions o;
            o.pxPerMm = 100, o.densityDiff = 997, o.needleMm = nw / 100;
            const PendantResult r = analyzePendant(im, o);
            QVERIFY2(r.ok, qPrintable(r.error));
            const double want = 997 * 9.81 * 1.5e-3 * 1.5e-3 / -beta * 1e3;
            qInfo("beta %.2f: gamma %.2f (want %.2f), b %.1f, beta %.3f, needle %.1f/%.1f px, Wo %.2f, rms %.2f px",
                  beta, r.gamma, want, r.apexRadiusPx, r.bond, r.needleWidthPx, nw, r.worthington, r.rmsPx);
            QVERIFY(std::abs(r.gamma / want - 1) < 0.01);
            QVERIFY(std::abs(r.needleWidthPx - nw) < 1.0);
            QVERIFY(std::abs(r.apexY - apexY) < 0.5);
        }
    }
    void noDrop() {
        GrayImage im(200, 200, 220);
        QVERIFY(!analyzePendant(im, {}).ok);
    }
};

QTEST_GUILESS_MAIN(TestPendant)
#include "test_pendant.moc"
