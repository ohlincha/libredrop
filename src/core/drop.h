#pragma once
// Contact angle of a sessile drop in a backlit side view.
//
// 1. Levels: background (bright) and drop/substrate (dark) from the histogram; threshold half-way.
// 2. Baseline: the substrate's top edge in the outer columns of the image (left and right of the drop),
//    a straight line fitted to both sides. Where the substrate is not dark (a bright glass or film surface seen
//    at grazing incidence, with the drop's mirror image below it), the contact points are the corners where the
//    drop's silhouette meets its reflection: for each side the outermost point of the drop + reflection outline
//    (θ ≤ 90°) or the neck between the two bulges (θ > 90°). A manual baseline can be given instead.
// 3. Outline: sub-pixel threshold crossings along rows (sides) and columns (top) of the dark region standing
//    on the baseline. A dark column reaching the top of the image is the needle; its rows are left out.
// 4. Methods, all in the baseline frame (u along the baseline, v height above it), on points more than
//    `minHeightPx` above the baseline (blur and the contact line itself are excluded):
//      young-laplace  axisymmetric drop shape with gravity (apex, apex radius, Bond number); the reference
//      tangent        quadratic fitted to each side near the contact point; left and right separately
//      circle         spherical cap (no gravity)
//      ellipse        direct least-squares ellipse
//      height-width   θ = 2 atan(2h/D), spherical cap from height and base width; for small, flat drops
// 5. Checks: things that make a result wrong, each with what to do about it.
#include <QString>
#include <limits>
#include <optional>
#include <vector>

#include "image.h"

namespace ld {

inline constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

struct Pt {
    double x = 0, y = 0;
};

struct Baseline {
    Pt p0;                 // a point on the line (image coordinates)
    double angle = 0;      // direction (radians, image coordinates: positive = right side lower)
    double rmsPx = 0;      // scatter of the substrate edge about the line
    bool found = false;
    bool oneSide = false;  // the substrate was visible on one side of the drop only
    bool reflection = false;   // from the drop/reflection contact points (no dark substrate)
    Pt toUv(Pt p) const;   // image → baseline frame (u along, v up)
    Pt toImage(Pt uv) const;
};

struct AngleResult {
    QString method;
    double left = kNaN, right = kNaN;   // degrees
    double rmsPx = kNaN;                    // fit residual
    Pt contactL, contactR;                     // baseline frame
    bool ok = false;
    QString note;
    double mean() const { return 0.5 * (left + right); }
};

struct Check {
    enum Level { Info, Warning, Error };
    Level level = Info;
    QString id, message;
};

struct DropOptions {
    double minHeightPx = 2.0;                  // outline points closer to the baseline are not fitted
    std::optional<Baseline> baseline;          // manual baseline
    bool staticMeasurement = true;             // warn if the needle touches the drop
    // Known liquid and scale fix the Bond number from the apex radius (else it is fitted):
    double pxPerMm = 0, densityDiff = 0 /* kg/m³ */, surfaceTension = 0 /* mN/m */;
};

struct DropAnalysis {
    bool ok = false;
    QString error;
    double background = 0, dark = 0, threshold = 0;
    Baseline baseline;
    std::vector<Pt> outline;                   // image coordinates
    bool needle = false, needleTouches = false;
    double needleWidthPx = 0;
    double heightPx = 0, baseWidthPx = 0, volumePx3 = 0;
    double apexRadiusPx = 0, bond = 0;
    double edgeWidthPx = 0;                    // 10–90 % edge width along the outline normal
    std::vector<AngleResult> methods;
    std::vector<Check> checks;

    const AngleResult* method(const QString& name) const;
    bool hasCheck(const QString& id) const;
};

DropAnalysis analyzeDrop(const GrayImage& im, const DropOptions& opt = {});

}  // namespace ld
