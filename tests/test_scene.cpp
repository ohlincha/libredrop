// Whole frames (surface, several drops, needle) and drops followed through a sequence of frames.
#include <QImage>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>

#include "core/image.h"
#include "core/report.h"
#include "core/scene.h"
#include "core/video.h"

using namespace ld;

namespace {

// A synthetic drop on a reflecting surface: the drop above the baseline, its mirror image below, bright elsewhere.
GrayImage reflected(double theta, double radius, int width) {
    SynthDrop d;
    d.thetaDeg = theta, d.apexRadiusPx = radius, d.bond = 0.1;
    d.width = width, d.height = 500, d.centerX = width / 2.0, d.baselineY = 300;
    GrayImage im = renderDrop(d);
    for (int y = 300; y < im.h; ++y)
        for (int x = 0; x < im.w; ++x) {
            const int ym = 2 * 300 - y;   // the baseline row is y = 300 - 0.5 in pixel centres; close enough
            im.at(x, y) = ym >= 0 ? im.at(x, ym) : float(d.background);
        }
    return im;
}

GrayImage sideBySide(const GrayImage& a, const GrayImage& b, int shift) {
    GrayImage out(a.w + b.w, a.h, float(percentile(a, 0.98)));
    for (int y = 0; y < a.h; ++y)
        for (int x = 0; x < out.w; ++x) {
            const int xs = x - shift;
            if (xs >= 0 && xs < a.w) out.at(x, y) = a.at(xs, y);
            else if (xs >= a.w && xs < a.w + b.w) out.at(x, y) = b.at(xs - a.w, y);
        }
    return out;
}

const AngleResult* yl(const SceneDrop& d) { return d.a.method(QStringLiteral("young-laplace")); }

}  // namespace

class TestScene : public QObject {
    Q_OBJECT
private slots:
    void realFrames_data() {
        QTest::addColumn<QString>("file");
        QTest::addColumn<QList<double>>("angles");   // measurable drops, left to right
        // by hand (circle fits on the full frames, see knowledge/first-recordings.md)
        QTest::newRow("tape") << "scene_tape_90_61.png" << QList<double>{90.0, 61.0};
        QTest::newRow("glass") << "scene_glass_29_21.png" << QList<double>{28.5, 20.7};
        QTest::newRow("needle above") << "scene_glass_needle_18.png" << QList<double>{17.5};
    }
    void realFrames() {
        QFETCH(QString, file);
        QFETCH(QList<double>, angles);
        const QImage img(QStringLiteral(TEST_DATA "/") + file);
        QVERIFY(!img.isNull());
        SceneOptions o;
        o.drop.pxPerMm = 94.23, o.drop.densityDiff = 997, o.drop.surfaceTension = 72;
        const Scene s = analyzeScene(fromQImage(img), o);
        QVERIFY2(s.ok, qPrintable(s.error));
        QVERIFY(std::abs(s.surface.angle) < 0.01);
        // the drops of interest: wide ones, not cut off, not specks or stains (base > 100 px, height > 15 px)
        QList<double> got;
        for (const SceneDrop& d : s.drops)
            if (usable(d.a) && d.a.baseWidthPx > 100 && d.a.heightPx > 15) got << yl(d)->mean();
        QCOMPARE(got.size(), angles.size());
        for (int i = 0; i < got.size(); ++i)
            QVERIFY2(std::abs(got[i] - angles[i]) < 2.5, qPrintable(QString("%1 vs %2").arg(got[i]).arg(angles[i])));
        for (const SceneDrop& d : s.drops) QVERIFY(!d.onNeedle);   // the needle and the hanging drop are not drops
    }

    void synthetic() {   // two drops, surface found from their reflections
        const Scene s = analyzeScene(sideBySide(reflected(40, 160, 700), reflected(100, 100, 700), 0));
        QVERIFY2(s.ok, qPrintable(s.error));
        QCOMPARE(int(s.drops.size()), 2);
        QVERIFY(std::abs(s.surface.p0.y - 299.5) < 1.5);
        QVERIFY2(std::abs(yl(s.drops[0])->mean() - 40) < 2, qPrintable(QString::number(yl(s.drops[0])->mean())));
        QVERIFY2(std::abs(yl(s.drops[1])->mean() - 100) < 3, qPrintable(QString::number(yl(s.drops[1])->mean())));
    }

    void tracking() {   // the slide is pushed between frames 3 and 4: both drops move 120 px, then stay
        const GrayImage a = reflected(40, 160, 700), b = reflected(100, 100, 700);
        QTemporaryDir dir;
        QStringList files;
        for (int i = 0; i < 8; ++i) {
            const QString f = dir.filePath(QStringLiteral("f%1.png").arg(i));
            QVERIFY(toQImage(sideBySide(a, b, i < 4 ? 0 : 120)).save(f));
            files << f;
        }
        VideoOptions o;
        o.fps = 10;
        o.minDuration = 0.2;
        const VideoResult r = analyzeImages(files, o);
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(int(r.tracks.size()), 2);
        for (const DropTrack& t : r.tracks) QCOMPARE(int(t.points.size()), 8);
        QVERIFY(std::abs(angleOf(r.tracks[0].points.back().drop.a) - 40) < 2);
        QVERIFY(std::abs(angleOf(r.tracks[1].points.back().drop.a) - 100) < 3);
        const SeriesSet set = seriesOf(r, Units{});
        const TrackSummary s = summarize(set.drops[0]);
        QCOMPARE(s.points, 8);
        QVERIFY(std::abs(s.angleRate) < 1);
        QVERIFY(std::abs(s.angleEnd - 40) < 2);
        // the table read back gives the same summary; one drop picked out
        const QString csv = tracksCsv(r, Units{});
        QCOMPARE(csv.count(QLatin1Char('\n')), 1 + 16);
        SeriesSet back;
        QString err;
        QVERIFY2(readTracksCsv(csv, &back, &err), qPrintable(err));
        QCOMPARE(int(back.drops.size()), 2);
        QCOMPARE(back.lenUnit, QStringLiteral("px"));
        QVERIFY(std::abs(summarize(back.drops[0]).angleEnd - s.angleEnd) < 0.01);
        selectDrops(&back, {2});
        QCOMPARE(int(back.drops.size()), 1);
        QCOMPARE(back.drops[0].id, 2);
        QCOMPARE(tracksCsv(r, Units{}, {2}).count(QLatin1Char('\n')), 1 + 8);
    }
};

QTEST_GUILESS_MAIN(TestScene)
#include "test_scene.moc"
