// Recording from the simulated camera: frames kept from before Record, times, the .json, reading back.
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

#include "core/camera.h"
#include "core/mjpeg.h"
#include "core/recorder.h"

class TestCamera : public QObject {
    Q_OBJECT
private slots:
    void recordFromSimulated() {
        const QString example = QStringLiteral(TEST_DATA "/../../examples/water_on_glass.mkv");
        QString err;
        auto cam = ld::Camera::open(QStringLiteral("sim:") + example, &err);
        QVERIFY2(cam, qPrintable(err));
        QCOMPARE(cam->modes().size(), std::size_t(1));
        QVERIFY(cam->start(cam->modes().front(), &err));
        const ld::CameraMode m = cam->mode();
        QVERIFY(m.fps > 20 && m.fps < 40);

        QTemporaryDir dir;
        const QString path = dir.filePath(ld::recordingName(QStringLiteral("DMC"), QStringLiteral("kraft A"), QStringLiteral("front"),
                                                            QStringLiteral("MD"), QDateTime(QDate(2026, 11, 25), QTime(10, 0, 0))) +
                                          QStringLiteral(".mkv"));
        QVERIFY(path.endsWith(QStringLiteral("DMC_kraft-A_front_MD_20261125_100000.mkv")));
        ld::Recorder rec;
        rec.preSeconds = 0.2;
        ld::CameraFrame f;
        for (int i = 0; i < 15; ++i) {   // 0.5 s before Record: only the last 0.2 s is kept
            QVERIFY(cam->grab(&f, &err));
            QVERIFY(rec.push(f, &err));
        }
        QJsonObject meta{{QStringLiteral("sample"), QJsonObject{{QStringLiteral("sample"), QStringLiteral("kraft A")}}}};
        QVERIFY2(rec.start(path, m.w, m.h, meta, &err), qPrintable(err));
        const int pre = rec.frames();
        QVERIFY(pre >= 6 && pre <= 8);
        for (int i = 0; i < 10; ++i) {
            QVERIFY(cam->grab(&f, &err));
            QVERIFY(rec.push(f, &err));
        }
        QVERIFY(rec.stop(&err));
        QCOMPARE(rec.frames(), pre + 10);
        QCOMPARE(rec.dropped(), 0);

        ld::MjpegReader r;
        QVERIFY2(r.open(path, &err), qPrintable(err));
        QCOMPARE(r.frames(), pre + 10);
        QCOMPARE(r.width(), m.w);
        QCOMPARE(r.times().front(), 0.0);
        for (int i = 1; i < r.frames(); ++i) QVERIFY(r.times()[i] > r.times()[i - 1]);
        QVERIFY(std::abs(r.times().back() - (pre + 9) / m.fps) < 0.01);

        QFile j(path.chopped(4) + QStringLiteral(".json"));
        QVERIFY(j.open(QIODevice::ReadOnly));
        const QJsonObject o = QJsonDocument::fromJson(j.readAll()).object();
        QCOMPARE(o[QStringLiteral("frames")].toInt(), pre + 10);
        QCOMPARE(o[QStringLiteral("sample")].toObject()[QStringLiteral("sample")].toString(), QStringLiteral("kraft A"));
        QVERIFY(o[QStringLiteral("started")].toString().size() >= 19);
    }
};

QTEST_GUILESS_MAIN(TestCamera)
#include "test_camera.moc"
