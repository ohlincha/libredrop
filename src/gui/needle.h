#pragma once
// Scale from the needle: its width in a frame (px) and its outer diameter (mm, measured with a micrometer) give
// px/mm. No project needed: the scale is kept per frame width (QSettings scaleFor/<width>), and added to the open
// project's calibrations too when there is one.
#include <QInputDialog>
#include <QMessageBox>
#include <QSettings>
#include <QWidget>
#include <cmath>

#include "core/image.h"
#include "core/pendant.h"

// The needle's width in px (NaN if no needle comes down from the top edge). bottom: rows below are left out
// (the surface and the drop on it), or -1 for the whole frame.
inline double needleWidthPx(const ld::GrayImage& g, int bottom = -1) {
    if (g.empty()) return std::nan("");
    const int h = bottom > 20 && bottom < g.h ? bottom : g.h;
    const ld::PendantResult r = ld::analyzePendant(ld::crop(g, 0, 0, g.w, h), {});
    return r.needleWidthPx > 2 ? r.needleWidthPx : std::nan("");
}

// Asks for the needle's diameter and returns px/mm (0 if cancelled or no needle); *mm gets the diameter.
inline double askNeedleScale(QWidget* parent, double px, double* mm) {
    if (!std::isfinite(px)) {
        QMessageBox::information(parent, QObject::tr("Scale from the needle"),
                                 QObject::tr("No needle found: the needle must come down from the top edge of the image "
                                             "(move to a frame where it does)."));
        return 0;
    }
    bool ok = false;
    const double d = QInputDialog::getDouble(parent, QObject::tr("Scale from the needle"),
                                             QObject::tr("The needle is %1 px wide here.\nIts outer diameter (mm):").arg(px, 0, 'f', 1),
                                             QSettings().value(QStringLiteral("needleMm"), 0.4745).toDouble(), 0.01, 10.0, 4, &ok);
    if (!ok || !(d > 0)) return 0;
    QSettings().setValue(QStringLiteral("needleMm"), d);
    if (mm) *mm = d;
    return px / d;
}
