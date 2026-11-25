#pragma once
// Scale from the needle: its outer diameter is a setting (measured once with a micrometer; the lab's needle:
// 0.4745 mm), and LibreDrop measures the needle's width in pixels itself, in the live image and in every recording
// it opens: px/mm = width / diameter. 0 mm: not used (the scale is entered by hand).
#include <QSettings>
#include <algorithm>
#include <cmath>
#include <vector>

#include "core/image.h"
#include "core/pendant.h"
#include "core/video.h"

inline double needleDiameterMm() { return QSettings().value(QStringLiteral("needleMm"), 0.4745).toDouble(); }
inline void setNeedleDiameterMm(double mm) { QSettings().setValue(QStringLiteral("needleMm"), mm); }

// The needle's width in px in one frame (NaN if no needle comes down from the top edge). bottom: rows below are
// left out (the surface and the drop on it), or -1 for the whole frame.
inline double needleWidthPx(const ld::GrayImage& g, int bottom = -1) {
    if (g.empty()) return std::nan("");
    const int h = bottom > 20 && bottom < g.h ? bottom : g.h;
    const ld::PendantResult r = ld::analyzePendant(ld::crop(g, 0, 0, g.w, h), {});
    return r.needleWidthPx > 2 ? r.needleWidthPx : std::nan("");
}

// The needle's width in a recording: the median over frames spread over it (NaN if not found in most of them).
inline double needleWidthInVideo(const QString& path, const ld::VideoInfo& info, int samples = 7) {
    const int n = int(info.times.size());
    if (n == 0) return std::nan("");
    std::vector<double> w;
    for (int k = 0; k < samples; ++k) {
        ld::GrayImage g;
        QString err;
        if (!ld::readFrameAt(path, info, std::min(n - 1, k * n / samples), &g, &err)) continue;
        const double px = needleWidthPx(g, g.h / 2);   // the upper half: the needle, not the drop on the sample
        if (std::isfinite(px)) w.push_back(px);
    }
    if (int(w.size()) * 2 < samples) return std::nan("");
    std::nth_element(w.begin(), w.begin() + long(w.size() / 2), w.end());
    return w[w.size() / 2];
}
