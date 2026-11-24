// Project folder: create, add recordings (with and without the capture .json), save and load, look-ups.
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

#include "core/project.h"

using namespace ld;

class TestProject : public QObject {
    Q_OBJECT
private slots:
    void roundTrip() {
        QTemporaryDir tmp, outside;
        Project p;
        QString err;
        QVERIFY2(Project::create(tmp.filePath(QStringLiteral("sep")), QStringLiteral("Separators"), &p, &err), qPrintable(err));
        QVERIFY(!Project::create(tmp.filePath(QStringLiteral("sep")), {}, &p, &err));   // not twice
        QVERIFY(p.liquid(QStringLiteral("Water")));
        // a recording with the capture tool's .json, outside the project: copied in, sample made
        const QString v = outside.filePath(QStringLiteral("DMC_kraft-A_front_MD_20261125_101000.mkv"));
        QFile(v).open(QIODevice::WriteOnly);
        QFile j(outside.filePath(QStringLiteral("DMC_kraft-A_front_MD_20261125_101000.json")));
        QVERIFY(j.open(QIODevice::WriteOnly));
        j.write(R"({"started": "2026-11-25T10:10:00.000", "note": "first",
                    "sample": {"sample": "kraft-A", "side": "front", "direction": "MD", "liquid": "DMC", "volume_ul": 2.0}})");
        j.close();
        Recording r;
        QVERIFY2(p.addRecording(v, true, &r, &err), qPrintable(err));
        QCOMPARE(r.sample, QStringLiteral("kraft-A front MD"));
        QCOMPARE(r.liquid, QStringLiteral("DMC"));
        QCOMPARE(r.date, QStringLiteral("2026-11-25"));
        QCOMPARE(r.file, QStringLiteral("recordings/DMC_kraft-A_front_MD_20261125_101000.mkv"));
        QVERIFY(QFile::exists(p.absolute(r.file)));
        QVERIFY(!p.addRecording(v, true, nullptr, &err));   // already there
        // one without a .json: no sample yet
        const QString g = outside.filePath(QStringLiteral("glass.mkv"));
        QFile(g).open(QIODevice::WriteOnly);
        QVERIFY(p.addRecording(g, false, &r, &err));
        QVERIFY(r.sample.isEmpty());
        p.recordings.back().glassReference = true;
        p.recordings.back().excluded = {2};
        p.evaporation = {{"DMC", "2026-11-20", 0.05, "a"}, {"DMC", "2026-11-25", 0.08, "b"}, {"water", "2026-11-25", 0.018, "c"}};
        p.calibrations = {{1920, 94.23, 0.47, "2026-11-24", "sphere"}};
        QVERIFY(p.save(&err));
        Project q;
        QVERIFY2(Project::load(p.dir, &q, &err), qPrintable(err));
        QCOMPARE(int(q.recordings.size()), 2);
        QCOMPARE(int(q.samples.size()), 1);
        QCOMPARE(q.samples[0].direction, QStringLiteral("MD"));
        QCOMPARE(q.recordings[1].excluded, QList<int>{2});
        QVERIFY(q.recordings[1].glassReference);
        QCOMPARE(q.evaporationFor(QStringLiteral("dmc"), QStringLiteral("2026-11-25")).rate, 0.08);
        QCOMPARE(q.evaporationFor(QStringLiteral("DMC"), QStringLiteral("2026-11-23")).rate, 0.05);   // latest earlier
        QVERIFY(!std::isfinite(q.evaporationFor(QStringLiteral("DMC"), QStringLiteral("2026-11-01")).rate));
        QCOMPARE(q.calibrationFor(1920).pxPerMm, 94.23);
        QVERIFY(!std::isfinite(q.calibrationFor(640).pxPerMm));
    }
    void conditions() {
        QTemporaryDir tmp;
        Project p;
        QString err;
        QVERIFY(Project::create(tmp.filePath(QStringLiteral("c")), {}, &p, &err));
        QFile f(p.dir + QStringLiteral("/conditions.csv"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("time,sensor,temperature_c,humidity_pct\n2026-11-25T10:00:00,TEMPerHUM,22.50,35.0\n"
                "2026-11-25T10:10:00,TEMPerHUM,23.10,33.0\n2026-11-25T11:00:00,TEMPerHUM,24.00,30.0\n");
        f.close();
        double t, h;
        QVERIFY(p.conditionsAt(QStringLiteral("2026-11-25T10:08:00"), &t, &h));
        QCOMPARE(t, 23.1);
        QCOMPARE(h, 33.0);
        QVERIFY(!p.conditionsAt(QStringLiteral("2026-11-25T10:35:00"), &t, &h));   // nothing within 15 min
        Recording r;
        r.started = QStringLiteral("2026-11-25T10:58:30");
        QVERIFY(p.fillConditions(r));
        QCOMPARE(r.temperature, 24.0);
        const qint64 before = QFileInfo(p.dir + QStringLiteral("/conditions.csv")).size();
        p.logConditions(QStringLiteral("x"), 20.0, kNaN);   // appends
        QVERIFY(QFileInfo(p.dir + QStringLiteral("/conditions.csv")).size() > before);
    }
};

QTEST_GUILESS_MAIN(TestProject)
#include "test_project.moc"
