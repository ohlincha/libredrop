#pragma once
// Surface tension from a hanging (pendant) drop: the drop's outline below the needle fitted with the axisymmetric
// Young–Laplace equation (younglaplace.h, β < 0). γ = Δρ g b² / |β| with b the apex radius, so the image scale
// and the density difference must be known. The needle's width in the image checks the scale.
//
// Accuracy depends on how much gravity deforms the drop, given by the Worthington number
// Wo = Δρ g V / (π γ D_needle) (0..1): above ~0.5 γ is good to about 1 %, below ~0.2 the drop is nearly a sphere
// and γ becomes uncertain.
#include <QString>
#include <vector>

#include "drop.h"
#include "image.h"

namespace ld {

struct PendantOptions {
    double pxPerMm = 0;          // image scale (required for γ)
    double densityDiff = 0;      // kg/m³ (required for γ)
    double needleMm = 0;         // outer diameter, if known: checks the scale
    double dark = -1, background = -1;   // grey levels (else from the image)
};

struct PendantResult {
    bool ok = false;
    QString error;
    double gamma = kNaN;             // mN/m
    double bond = kNaN;              // β (negative)
    double apexRadiusPx = kNaN;
    double axisX = kNaN, apexY = kNaN;
    double needleWidthPx = kNaN, needleTipY = kNaN;
    double volumePx3 = kNaN;         // below the needle tip
    double volumeUl = kNaN;
    double worthington = kNaN;
    double rmsPx = kNaN;
    double scaleFromNeedle = kNaN;   // px/mm from needleMm (if given)
    std::vector<Pt> edge;            // fitted edge points (image coordinates)
    std::vector<Pt> fitted;          // fitted profile, left side tip → apex → right side tip
    std::vector<Check> checks;
};

PendantResult analyzePendant(const GrayImage& im, const PendantOptions& opt);

}  // namespace ld
