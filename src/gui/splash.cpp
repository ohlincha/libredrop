#include "splash.h"

#include <QGuiApplication>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QScreen>
#include <cmath>

QImage drawSplash(qreal dpr) {
    const int w = 560, h = 320;
    QImage img(QSize(int(w * dpr), int(h * dpr)), QImage::Format_ARGB32_Premultiplied);
    img.setDevicePixelRatio(dpr);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    QLinearGradient bg(0, 0, 0, h);
    bg.setColorAt(0, QColor("#1b2738"));
    bg.setColorAt(1, QColor("#0f151f"));
    p.setPen(Qt::NoPen);
    p.setBrush(bg);
    p.drawRoundedRect(QRectF(0, 0, w, h), 18, 18);

    // the drop: a spherical cap with contact angle θ on a separator, its mirror image fading below
    const double cx = 140, base = 190, R = 78, theta = 62 * M_PI / 180;
    const double half = R * std::sin(theta), hgt = R * (1 - std::cos(theta));
    const double yc = base - hgt + R;   // circle centre (below the apex by R)
    auto cap = [&](double sign) {
        QPainterPath path;
        const int n = 60;
        for (int i = 0; i <= n; ++i) {
            const double a = -theta + 2 * theta * i / n;   // angle from the vertical
            const QPointF pt(cx + R * std::sin(a), base + sign * (yc - R * std::cos(a) - base));
            if (i == 0) path.moveTo(pt);
            else path.lineTo(pt);
        }
        path.closeSubpath();
        return path;
    };
    QLinearGradient water(cx, base - hgt, cx, base);
    water.setColorAt(0, QColor("#6fc3ff"));
    water.setColorAt(1, QColor("#2f7fd0"));
    p.setBrush(water);
    p.drawPath(cap(1));
    QLinearGradient mirror(cx, base, cx, base + hgt);
    mirror.setColorAt(0, QColor(47, 127, 208, 110));
    mirror.setColorAt(1, QColor(47, 127, 208, 0));
    p.setBrush(mirror);
    p.drawPath(cap(-1));
    // highlight
    p.setBrush(QColor(255, 255, 255, 70));
    p.drawEllipse(QPointF(cx - 22, base - hgt * 0.62), 12, 6);
    // separator: a strip of fibres/pores
    p.setBrush(QColor("#c9b28a"));
    p.drawRoundedRect(QRectF(cx - 125, base, 250, 7), 3, 3);
    p.setBrush(QColor("#8a7a5e"));
    for (int k = 0; k < 22; ++k) p.drawEllipse(QPointF(cx - 118 + k * 11.3, base + 3.5), 1.6, 1.6);
    // surface line (as in the program: dashed yellow) and the contact angle at the left contact point
    p.setPen(QPen(QColor(255, 200, 0), 1.4, Qt::DashLine));
    p.drawLine(QPointF(cx - 125, base - 0.5), QPointF(cx + 125, base - 0.5));
    const QPointF c(cx - half, base);
    QPen tangent(QColor("#ffffff"), 2);
    tangent.setCapStyle(Qt::RoundCap);
    p.setPen(tangent);
    p.drawLine(c, c + QPointF(60 * std::cos(theta), -60 * std::sin(theta)));
    p.setPen(QPen(QColor("#ffd166"), 2));
    p.setBrush(Qt::NoBrush);
    p.drawArc(QRectF(c.x() - 30, c.y() - 30, 60, 60), 0, int(theta * 180 / M_PI * 16));
    p.setPen(Qt::NoPen);
    p.setBrush(Qt::white);
    p.drawEllipse(c, 3.5, 3.5);
    QFont f = QGuiApplication::font();
    f.setPixelSize(18);
    f.setItalic(true);
    p.setFont(f);
    p.setPen(QColor("#ffd166"));
    p.drawText(QPointF(c.x() + 34, c.y() - 8), QStringLiteral("θ"));

    // text
    f.setItalic(false);
    f.setPixelSize(42);
    f.setBold(true);
    p.setFont(f);
    p.setPen(Qt::white);
    p.drawText(QRectF(285, 70, 270, 56), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("LibreDrop"));
    f.setPixelSize(15);
    f.setBold(false);
    p.setFont(f);
    p.setPen(QColor("#b8c2d6"));
    p.drawText(QRectF(288, 128, 260, 64), Qt::AlignLeft | Qt::TextWordWrap,
               QStringLiteral("Contact angle and drop shape,\nfrom every frame of the video"));
    f.setPixelSize(12);
    p.setFont(f);
    p.setPen(QColor("#7d879b"));
    p.drawText(QRectF(288, 196, 260, 20), Qt::AlignLeft,
               QStringLiteral("Version %1 · MIT licence").arg(QStringLiteral(LIBREDROP_VERSION)));
    p.drawText(QRectF(288, 214, 260, 20), Qt::AlignLeft, QStringLiteral("github.com/ohlincha/libredrop"));
    p.end();
    return img;
}

Splash::Splash() {
    const QScreen* s = QGuiApplication::primaryScreen();
    setPixmap(QPixmap::fromImage(drawSplash(s ? s->devicePixelRatio() : 1.0)));
    setAttribute(Qt::WA_TranslucentBackground);
    setCursor(Qt::PointingHandCursor);
}

void Splash::mousePressEvent(QMouseEvent* e) {
    hide();
    QSplashScreen::mousePressEvent(e);
}
