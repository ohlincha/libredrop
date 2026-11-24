#pragma once
// Splash screen: a drop on a porous separator with its mirror image and the contact angle drawn in, the name and
// version. Shown while the program starts (at least a moment); a click hides it.
#include <QImage>
#include <QSplashScreen>

QImage drawSplash(qreal dpr = 1.0);   // logical size 560 × 320

class Splash : public QSplashScreen {
    Q_OBJECT
public:
    Splash();

protected:
    void mousePressEvent(QMouseEvent* e) override;
};
