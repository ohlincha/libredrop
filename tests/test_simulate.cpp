// The simulator (core/simulate) against the analysis: every scenario rendered at several moments of the drop's
// life; the measured angle and base must match the true ones. Also checks the simulator's own physics (volume and
// angle over time).
#include <QtTest>

#include "core/scene.h"
#include "core/simulate.h"

using namespace ld;

class TestSimulate : public QObject {
    Q_OBJECT
private slots:
    void physics() {
        SimSpec s;
        QVERIFY(simPreset(QStringLiteral("separator"), &s));
        QVERIFY(!simTruth(s, s.tContact - 0.1).onSurface);
        const SimTruth a = simTruth(s, s.tContact + 0.01), b = simTruth(s, s.tContact + 5), c = simTruth(s, s.tContact + 10);
        QVERIFY(a.onSurface && b.onSurface);
        QVERIFY(std::abs(a.volumeUl - s.volumeUl) < 0.01);
        QVERIFY(b.volumeUl < a.volumeUl && b.thetaDeg < a.thetaDeg);
        QVERIFY(!c.onSurface || c.volumeUl < b.volumeUl);
        // pinned while soaking: the base stays (it does not shrink)
        const SimTruth p1 = simTruth(s, s.tContact + s.soakDelay + 0.5), p2 = simTruth(s, s.tContact + s.soakDelay + 3);
        QVERIFY(p2.baseMm >= p1.baseMm - 1e-3);
    }

    void analysis_data() {
        QTest::addColumn<QString>("preset");
        QTest::addColumn<double>("t");
        QTest::addColumn<double>("tolDeg");
        QTest::newRow("glass 4 s") << "glass" << 4.0 << 1.0;
        QTest::newRow("glass 15 s") << "glass" << 15.0 << 1.0;
        QTest::newRow("separator 4 s") << "separator" << 4.0 << 1.0;
        QTest::newRow("separator 10 s") << "separator" << 10.0 << 1.0;
        QTest::newRow("wavy 6 s") << "wavy-paper" << 6.0 << 1.0;
        QTest::newRow("wavy 25 s") << "wavy-paper" << 25.0 << 1.0;
        QTest::newRow("curled 10 s") << "curled-postit" << 10.0 << 1.5;
        QTest::newRow("curled 40 s") << "curled-postit" << 40.0 << 1.5;
        QTest::newRow("hydrophobic 8 s") << "hydrophobic" << 8.0 << 1.0;
    }
    void analysis() {
        QFETCH(QString, preset);
        QFETCH(double, t);
        QFETCH(double, tolDeg);
        SimSpec s;
        QVERIFY(simPreset(preset, &s));
        SimTruth tr;
        const GrayImage im = simFrame(s, t, &tr);
        QVERIFY(tr.onSurface);
        SceneOptions o;
        o.drop.pxPerMm = s.pxPerMm;
        const Scene sc = analyzeScene(im, o);
        QVERIFY2(sc.ok, qPrintable(sc.error));
        QCOMPARE(int(sc.drops.size()), 1);
        const DropAnalysis& a = sc.drops.front().a;
        const AngleResult* yl = a.method(QStringLiteral("young-laplace"));
        QVERIFY2(yl && yl->ok && std::abs(yl->mean() - tr.thetaDeg) < tolDeg,
                 qPrintable(QString("%1 at %2 s: %3° (true %4°)").arg(preset).arg(t).arg(yl ? yl->mean() : -1).arg(tr.thetaDeg)));
        QVERIFY2(std::abs(a.baseWidthPx / s.pxPerMm - tr.baseMm) < 0.05,
                 qPrintable(QString("base %1 mm (true %2)").arg(a.baseWidthPx / s.pxPerMm).arg(tr.baseMm)));
    }
};

QTEST_GUILESS_MAIN(TestSimulate)
#include "test_simulate.moc"
