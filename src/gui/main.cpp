// LibreDrop: graphical program.  LibreDrop [FILE | --example]
#include <QAbstractButton>
#include <QApplication>
#include <QDialog>
#include <QTabWidget>
#include <QIcon>
#include <QPalette>
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
    // a little colour, from the splash: the drop's blue as the accent, amber for what needs attention.
    // Only tints and accents, so light and dark desktop themes both keep working.
    {
        QPalette pal = app.palette();
        pal.setColor(QPalette::Highlight, QColor(0x2f7fd0));
        pal.setColor(QPalette::HighlightedText, Qt::white);
        pal.setColor(QPalette::Link, QColor(0x2f7fd0));
        app.setPalette(pal);
        app.setStyleSheet(QStringLiteral(
            "QGroupBox { font-weight: 600; }"
            "QGroupBox::title { color: #2f7fd0; }"
            "QGroupBox > QWidget { font-weight: normal; }"
            "QPushButton#analyse, QPushButton#compare { background: #2f7fd0; color: white; border: 1px solid #2869b0; border-radius: 4px;"
            "  padding: 5px 14px; font-weight: 600; }"
            "QPushButton#analyse:hover, QPushButton#compare:hover { background: #3a8be0; }"
            "QPushButton#analyse:pressed, QPushButton#compare:pressed { background: #2869b0; }"
            "QPushButton#analyse:disabled { background: rgba(47,127,208,90); border-color: rgba(47,127,208,90); }"
            "QHeaderView::section { background: rgba(47,127,208,24); border: none;"
            "  border-right: 1px solid rgba(47,127,208,40); border-bottom: 1px solid rgba(47,127,208,110); padding: 3px 6px; }"
            "QLabel#soakHint { border-radius: 4px; padding: 6px 8px; }"
            "QLabel#soakHint[state=\"todo\"] { background: rgba(255,209,102,70); border-left: 3px solid #e0a800; }"
            "QLabel#soakHint[state=\"ok\"] { background: rgba(47,127,208,26); border-left: 3px solid #2f7fd0; }"));
    }
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
        // LIBREDROP_SHOT_CLICK=objectName: click that button 1 s before the screenshot
        const QString click = qEnvironmentVariable("LIBREDROP_SHOT_CLICK");
        if (!click.isEmpty())
            QTimer::singleShot(std::max(0, (ms > 0 ? ms : 3000) - 1000), &w, [&w, click] {
                if (auto* b = w.findChild<QAbstractButton*>(click)) b->click();
            });
        // LIBREDROP_COMPARE=a.mkv;b.mkv: open Compare samples with these, analyse, and screenshot that window instead
        const QString cmp = qEnvironmentVariable("LIBREDROP_COMPARE");
        if (!cmp.isEmpty())
            QTimer::singleShot(300, &w, [&w, cmp] {
                w.openCompare(cmp.split(QLatin1Char(';'), Qt::SkipEmptyParts));
                if (auto* d = w.findChild<QDialog*>())
                    if (auto* b = d->findChild<QAbstractButton*>(QStringLiteral("analyse"))) b->click();
            });
        // LIBREDROP_SHOT_TAB=n: show that tab of the drop table
        const int tab = qEnvironmentVariableIntValue("LIBREDROP_SHOT_TAB");
        QTimer::singleShot(ms > 0 ? ms : 3000, &w, [&w, shot, tab] {
            if (auto* t = w.findChild<QTabWidget*>()) t->setCurrentIndex(tab);
            QWidget* target = &w;
            if (auto* d = w.findChild<QDialog*>(); d && d->isVisible()) target = d;
            target->grab().save(shot.section(QLatin1Char(','), 0, 0));
            w.close();
        });
    }
    return app.exec();
}
