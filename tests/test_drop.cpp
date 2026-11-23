// Contact angles of synthetic drops of known angle and shape, and the checks.
#include <QtTest>
#include <cmath>

#include "core/drop.h"
#include "core/image.h"
#include "core/younglaplace.h"

using namespace ld;

class TestDrop : public QObject {
    Q_OBJECT
    static DropAnalysis run(SynthDrop d) { return analyzeDrop(renderDrop(d)); }
    static double err(const DropAnalysis& a, const char* m, double theta) {
        const AngleResult* r = a.method(QString::fromLatin1(m));
        return r && r->ok ? std::max(std::abs(r->left - theta), std::abs(r->right - theta)) : 1e9;
    }
private slots:
    void sphereProfile() {   // Bond 0: a circle of radius 1, hemisphere volume 2π/3
        const Profile p = sessile(0.0, M_PI / 2);
        for (std::size_t i = 0; i < p.X.size(); i += 50)
            QVERIFY(std::abs(std::hypot(p.X[i], p.Z[i] - 1) - 1) < 1e-6);
        QVERIFY(std::abs(p.Z.back() - 1) < 1e-6);
        QVERIFY(std::abs(p.V.back() - 2 * M_PI / 3) < 1e-5);
    }
    void gravityFlattens() {  // with gravity the drop of the same angle is lower than a sphere
        QVERIFY(sessile(1.0, M_PI / 2).Z.back() < 0.8);
    }

    void angles_data() {
        QTest::addColumn<double>("theta");
        QTest::addColumn<double>("bond");
        QTest::addColumn<double>("radius");
        QTest::addColumn<double>("tilt");
        QTest::newRow("5 flat") << 5.0 << 0.5 << 3500.0 << 0.0;
        QTest::newRow("15") << 15.0 << 0.05 << 800.0 << 0.0;
        QTest::newRow("30") << 30.0 << 0.1 << 400.0 << 0.0;
        QTest::newRow("45 heavy") << 45.0 << 0.3 << 200.0 << 0.0;
        QTest::newRow("90 heavy") << 90.0 << 0.5 << 200.0 << 0.0;
        QTest::newRow("120") << 120.0 << 0.3 << 150.0 << 0.0;
        QTest::newRow("150") << 150.0 << 0.1 << 200.0 << 0.0;
        QTest::newRow("170") << 170.0 << 0.05 << 300.0 << 0.0;
        QTest::newRow("70 tilted") << 70.0 << 0.2 << 200.0 << 1.0;
        QTest::newRow("70 tilted back") << 70.0 << 0.2 << 200.0 << -2.0;
    }
    void angles() {
        QFETCH(double, theta);
        QFETCH(double, bond);
        QFETCH(double, radius);
        QFETCH(double, tilt);
        SynthDrop d;
        d.thetaDeg = theta, d.bond = bond, d.apexRadiusPx = radius, d.tiltDeg = tilt;
        const DropAnalysis a = run(d);
        QVERIFY2(a.ok, qPrintable(a.error));
        QVERIFY2(err(a, "young-laplace", theta) < 0.3, qPrintable(QString::number(err(a, "young-laplace", theta))));
        QVERIFY2(err(a, "tangent", theta) < 1.0, qPrintable(QString::number(err(a, "tangent", theta))));
        QVERIFY(std::abs(a.baseline.angle * 180 / M_PI - tilt) < 0.05);
        QCOMPARE(a.hasCheck("tilt"), std::abs(tilt) > 0.5);
    }
    void sphericalCapMethods() {   // small drops (no gravity): circle and height–width are exact
        for (double theta : {10.0, 40.0, 90.0, 130.0}) {
            SynthDrop d;
            d.thetaDeg = theta, d.bond = 0.001, d.apexRadiusPx = theta < 20 ? 1200 : 200;
            const DropAnalysis a = run(d);
            QVERIFY(err(a, "circle", theta) < 0.5);
            QVERIFY(err(a, "height-width", theta) < 0.6);
            QVERIFY(!a.hasCheck("gravity"));
        }
    }
    void gravityCheck() {
        SynthDrop d;
        d.thetaDeg = 120, d.bond = 0.5;
        const DropAnalysis a = run(d);
        QVERIFY(a.hasCheck("gravity"));
        QVERIFY(err(a, "young-laplace", 120) < 0.3);
        QVERIFY(err(a, "circle", 120) > 3);
    }
    void bondRecovered() {   // the drop shape gives the Bond number, hence the surface tension
        for (double bond : {0.1, 0.4}) {
            SynthDrop d;
            d.thetaDeg = 110, d.bond = bond;
            const DropAnalysis a = run(d);
            QVERIFY2(std::abs(a.bond / bond - 1) < 0.05, qPrintable(QString::number(a.bond)));
            QVERIFY(std::abs(a.apexRadiusPx / d.apexRadiusPx - 1) < 0.005);
        }
    }
    void needle() {
        SynthDrop d;
        d.thetaDeg = 70, d.needle = true;
        d.needleGapPx = 30;                     // above the drop
        DropAnalysis a = run(d);
        QVERIFY(a.needle);
        QVERIFY(!a.needleTouches);
        QVERIFY(!a.hasCheck("needle"));
        QVERIFY(err(a, "young-laplace", 70) < 0.3);
        d.needleGapPx = -40;                    // in the drop
        a = run(d);
        QVERIFY(a.needleTouches);
        QVERIFY(a.hasCheck("needle"));
        QVERIFY(err(a, "young-laplace", 70) < 0.3);   // the needle's rows are left out
    }
    void darkImage() {   // backlight off
        SynthDrop d;
        d.background = 30, d.dark = 10;
        const DropAnalysis a = run(d);
        QVERIFY(!a.ok);
        QVERIFY(a.hasCheck("backlight"));
    }
    void blurred() {
        SynthDrop d;
        d.thetaDeg = 80, d.blurPx = 3.0;
        const DropAnalysis a = run(d);
        QVERIFY(a.hasCheck("focus"));
        QVERIFY(err(a, "young-laplace", 80) < 0.5);
    }
    void dropAtEdge() {
        SynthDrop d;
        d.thetaDeg = 60, d.centerX = 150;
        QVERIFY(run(d).hasCheck("border"));
    }
    void smallDrop() {
        SynthDrop d;
        d.thetaDeg = 60, d.apexRadiusPx = 50;
        QVERIFY(run(d).hasCheck("resolution"));
    }
    void volume() {   // Young–Laplace volume against the rendered profile
        SynthDrop d;
        d.thetaDeg = 90, d.bond = 0.3;
        double h, r, v;
        synthGeometry(d, &h, &r, &v);
        const DropAnalysis a = run(d);
        QVERIFY(std::abs(a.volumePx3 / v - 1) < 0.01);
        QVERIFY(std::abs(a.heightPx - h) < 1.0);
    }
};

QTEST_GUILESS_MAIN(TestDrop)
#include "test_drop.moc"
