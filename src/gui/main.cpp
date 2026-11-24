// LibreDrop: graphical program.  LibreDrop [FILE | --example]
#include <QAbstractButton>
#include <QApplication>
#include <QIcon>
#include <QTimer>
#include <QWheelEvent>

#include "frameview.h"
#include "mainwindow.h"
#include "splash.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("LibreDrop"));
    QApplication::setApplicationName(QStringLiteral("LibreDrop"));
    QApplication::setApplicationVersion(QStringLiteral(LIBREDROP_VERSION));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icon.png")));
    // testing: LIBREDROP_SPLASH_PNG=file.png saves the splash artwork and quits
    if (qEnvironmentVariableIsSet("LIBREDROP_SPLASH_PNG"))
        return drawSplash(2.0).save(qEnvironmentVariable("LIBREDROP_SPLASH_PNG")) ? 0 : 1;
    Splash splash;
    splash.show();
    app.processEvents();
    MainWindow w;
    const QStringList args = app.arguments();
    // the window (and the file given) after a short moment, so the splash can be seen
    QTimer::singleShot(1400, &w, [&] {
        w.show();
        splash.finish(&w);
        if (args.size() > 1 && args[1] == QLatin1String("--example")) w.openExample();
        else if (args.size() > 1) w.openFile(args[1]);
    });
    // testing: LIBREDROP_SHOT=file.png[,seconds] saves a screenshot of the window after that time and quits
    const QString shot = qEnvironmentVariable("LIBREDROP_SHOT");
    if (!shot.isEmpty()) {
        const int ms = int(1000 * shot.section(QLatin1Char(','), 1).toDouble());
        // LIBREDROP_SHOT_WHEEL=notches[,xfrac,yfrac]: mouse-wheel steps over the frame view just before (+ zooms in)
        const QString wheel = qEnvironmentVariable("LIBREDROP_SHOT_WHEEL");
        if (!wheel.isEmpty())
            QTimer::singleShot(std::max(0, (ms > 0 ? ms : 3000) - 300), &w, [&w, wheel] {
                if (FrameView* v = w.findChild<FrameView*>()) {
                    const int n = wheel.section(QLatin1Char(','), 0, 0).toInt();
                    const double fx = wheel.section(QLatin1Char(','), 1, 1).isEmpty() ? 0.5 : wheel.section(QLatin1Char(','), 1, 1).toDouble();
                    const double fy = wheel.section(QLatin1Char(','), 2, 2).isEmpty() ? 0.5 : wheel.section(QLatin1Char(','), 2, 2).toDouble();
                    const QPointF at(v->width() * fx, v->height() * fy);
                    for (int i = 0; i < std::abs(n); ++i) {
                        QWheelEvent e(at, v->mapToGlobal(at), {}, {0, n > 0 ? 120 : -120}, Qt::NoButton, Qt::NoModifier,
                                      Qt::NoScrollPhase, false);
                        QCoreApplication::sendEvent(v, &e);
                    }
                }
            });
        // LIBREDROP_SHOT_PLAY=seconds: press play that long before the screenshot
        const double play = qEnvironmentVariable("LIBREDROP_SHOT_PLAY").toDouble();
        if (play > 0)
            QTimer::singleShot(std::max(0, (ms > 0 ? ms : 3000) - int(1000 * play)), &w, [&w] {
                if (auto* b = w.findChild<QAbstractButton*>(QStringLiteral("play"))) b->click();
            });
        QTimer::singleShot(ms > 0 ? ms : 3000, &w, [&w, shot] {
            w.grab().save(shot.section(QLatin1Char(','), 0, 0));
            w.close();
        });
    }
    return app.exec();
}
