// Recording from the simulated camera: frames kept from before Record, times, the .json, reading back.
#include <QFile>
#include <QImage>
#include <QProcess>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

#include "core/camera.h"
#include "core/mjpeg.h"
#include "core/recorder.h"
#include "core/video.h"

class TestCamera : public QObject {
    Q_OBJECT
private slots:
    // The ffmpeg camera (Windows and macOS) with ffmpeg's test picture as the camera: whole JPEG frames, the
    // frame times ffmpeg reports, and ffmpeg's error when the input is wrong.
    void ffmpegCamera() {
        QProcess probe;
        probe.start(ld::ffmpegProgram(), {QStringLiteral("-version")});
        if (!probe.waitForFinished(10000) || probe.exitCode() != 0) QSKIP("ffmpeg not installed");
        QString err;
        auto cam = ld::Camera::open(QStringLiteral("ffmpeg:lavfi:testsrc2=size=320x240:rate=50"), &err);
        QVERIFY2(cam, qPrintable(err));
        QCOMPARE(cam->modes().front().w, 320);
        QVERIFY2(cam->start(cam->modes().front(), &err), qPrintable(err));
        ld::CameraFrame f;
        std::vector<double> t;
        for (int i = 0; i < 25; ++i) {
            QVERIFY2(cam->grab(&f, &err), qPrintable(err));
            QVERIFY(f.jpeg.startsWith("\xff\xd8") && f.jpeg.endsWith("\xff\xd9"));
            QCOMPARE(f.sequence, i);
            t.push_back(f.t);
        }
        QImage im;
        QVERIFY(im.loadFromData(f.jpeg, "JPG"));
        QCOMPARE(im.size(), QSize(320, 240));
        if (probe.readAll().contains("version 7") || t[1] - t[0] > 0.015)   // ffmpeg >= 6.1: its own frame times
            for (std::size_t i = 1; i < t.size(); ++i) QVERIFY(std::abs(t[i] - t[i - 1] - 0.02) < 1e-3);
        cam->stop();

        auto bad = ld::Camera::open(QStringLiteral("ffmpeg:lavfi:nosuchsource"), &err);
        QVERIFY(bad);
        QVERIFY(!bad->start(bad->modes().front(), &err) || !bad->grab(&f, &err));
        QVERIFY2(!err.isEmpty(), "no error message");
    }

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
