// LibreDrop: graphical program.  LibreDrop [FILE]
#include <QApplication>
#include <QIcon>
#include <QTimer>

#include "mainwindow.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("LibreDrop"));
    QApplication::setApplicationName(QStringLiteral("LibreDrop"));
    QApplication::setApplicationVersion(QStringLiteral(LIBREDROP_VERSION));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icon.png")));
    MainWindow w;
    w.show();
    const QStringList args = app.arguments();
    if (args.size() > 1) w.openFile(args[1]);
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
