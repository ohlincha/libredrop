// LibreDrop: graphical program.  LibreDrop [FILE | --example]
#include <QApplication>
#include <QIcon>
#include <QTimer>

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
        QTimer::singleShot(ms > 0 ? ms : 3000, &w, [&w, shot] {
            w.grab().save(shot.section(QLatin1Char(','), 0, 0));
            w.close();
        });
    }
    return app.exec();
}
