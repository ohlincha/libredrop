// Real goniometer frames (2026-11-25, Ossila camera at 1920x1080, beryllium): the cases that went wrong once, as
// regression tests. The whole recordings are in testdata/real (not in git; see testdata/README.md).
//   glass_*            water on a glass slide (reflection). 16.09 s: a speck inside the drop's columns once split it
//                      in two and the drop was rejected as "at the image edge" (gaps in the curves).
//   postit_A_* / _B_*  water on a 3M post-it: opaque, no reflection, curled sheet with a fibrous ridge, a lifted
//                      sheet edge on the left and a step on the right. "The hardest sample you will get" (user).
#include <QImage>
#include <QtTest>

#include "core/image.h"
#include "core/scene.h"

using namespace ld;

namespace {
Scene run(const QString& file) {
    const QImage img(QStringLiteral(TEST_DATA "/") + file);
    if (img.isNull()) return {};
    SceneOptions o;
    o.drop.pxPerMm = 94.23;
    return analyzeScene(fromQImage(img), o);
}
double angle(const Scene& s, const char* method) {
    if (s.drops.size() != 1) return std::nan("");
    const AngleResult* r = s.drops.front().a.method(QString::fromLatin1(method));
    return r && r->ok ? r->mean() : std::nan("");
}
bool hasCheck(const Scene& s, const char* id) {
    for (const SceneDrop& d : s.drops)
        if (d.a.hasCheck(QString::fromLatin1(id))) return true;
    return false;
}
}  // namespace

class TestReal : public QObject {
    Q_OBJECT
private slots:
    void glassSplitBlob() {
        for (const char* f : {"glass_16s.jpg", "glass_split_blob_16s.jpg"}) {
            const Scene s = run(QString::fromLatin1(f));
            QVERIFY2(s.ok && s.drops.size() == 1, f);
            QVERIFY2(!hasCheck(s, "border"), f);
            QVERIFY2(std::abs(angle(s, "young-laplace") - 38.3) < 1.5, qPrintable(QString("%1: %2").arg(f).arg(angle(s, "young-laplace"))));
        }
    }

    void postitNoDrop_data() {
        QTest::addColumn<QString>("file");
        QTest::newRow("before") << "postit_A_03s_nodrop.jpg";
        QTest::newRow("hanging drop") << "postit_A_11s_hanging.jpg";   // a pendant drop on the needle: not on the sample
        QTest::newRow("wavy sheet") << "postit_B_10s_nodrop_wavy.jpg"; // the sheet's bulges and ridge are not drops
    }
    void postitNoDrop() {
        QFETCH(QString, file);
        const Scene s = run(file);
        QVERIFY2(s.ok, qPrintable(s.error));
        QVERIFY(!s.surface.reflection);
        QCOMPARE(int(s.drops.size()), 0);
    }

    void postitDrop_data() {
        QTest::addColumn<QString>("file");
        QTest::addColumn<QString>("method");
        QTest::addColumn<double>("lo");
        QTest::addColumn<double>("hi");
        QTest::addColumn<bool>("known");   // the method is known to fail here (expected failure, to be fixed)
        // by eye: ~90° just after landing, ~48° at 60 s, ~25° at 88 s (the paper under it wet and raised)
        QTest::newRow("B 30 s YL") << "postit_B_30s_90deg.jpg" << "young-laplace" << 84.0 << 96.0 << false;
        QTest::newRow("B 30 s circle") << "postit_B_30s_90deg.jpg" << "circle" << 80.0 << 92.0 << false;
        QTest::newRow("B 60 s YL") << "postit_B_60s_48deg.jpg" << "young-laplace" << 44.0 << 53.0 << false;
        QTest::newRow("B 60 s circle") << "postit_B_60s_48deg.jpg" << "circle" << 43.0 << 52.0 << false;
        QTest::newRow("B 88 s circle") << "postit_B_88s_wet.jpg" << "circle" << 20.0 << 34.0 << false;
        QTest::newRow("B 88 s YL") << "postit_B_88s_wet.jpg" << "young-laplace" << 20.0 << 34.0 << true;    // 18°
        // first post-it drop: ~90-100° at 18 s (walls vertical, slight overhang), ~55-60° at 33 s
        QTest::newRow("A 18 s circle") << "postit_A_18s_drop.jpg" << "circle" << 80.0 << 100.0 << false;
        QTest::newRow("A 18 s YL") << "postit_A_18s_drop.jpg" << "young-laplace" << 80.0 << 100.0 << true; // 54°
        QTest::newRow("A 33 s circle") << "postit_A_33s_spread.jpg" << "circle" << 50.0 << 65.0 << false;
        QTest::newRow("A 33 s YL") << "postit_A_33s_spread.jpg" << "young-laplace" << 50.0 << 65.0 << true; // no fit
    }
    void postitDrop() {
        QFETCH(QString, file);
        QFETCH(QString, method);
        QFETCH(double, lo);
        QFETCH(double, hi);
        QFETCH(bool, known);
        const Scene s = run(file);
        QVERIFY2(s.ok, qPrintable(s.error));
        QVERIFY(!s.surface.reflection);
        QCOMPARE(int(s.drops.size()), 1);
        QVERIFY(!hasCheck(s, "border"));
        const double a = angle(s, method.toLatin1().constData());
        if (known) QEXPECT_FAIL("", "known weakness on this frame (see testdata/README.md)", Continue);
        QVERIFY2(a >= lo && a <= hi, qPrintable(QString("%1 %2: %3°").arg(file, method).arg(a)));
    }
};

QTEST_GUILESS_MAIN(TestReal)
#include "test_real.moc"
