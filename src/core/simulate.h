#pragma once
// Simulated goniometer: camera frames of a drop's whole life on a chosen substrate, with the true values, for trying
// LibreDrop without the instrument and for testing the analysis where the answer is known.
//
// Substrates: a reflecting slide (glass: the drop's mirror image below the surface, the stage dark far below) or an
// opaque sheet (paper, separator: a dark band, a light strip, the dark stage), which may be tilted, wavy, curled,
// stepped, have a raised ridge, a lifted sheet edge, fibres along its edge, and swell under a wet drop.
// Drop: hangs from the needle and comes down; touches at tContact; its angle relaxes from theta0 towards thetaEq
// (spreading, base free) until the volume starts to fall (soaking as √t, plus evaporation): then the base is pinned
// and the angle drops with the volume; when its height is gone, it has been absorbed.
// Young–Laplace shapes with the liquid's density and surface tension at the given scale.
#include <QString>
#include <QStringList>
#include <cstdint>
#include <vector>

#include "image.h"

namespace ld {

struct SimSubstrate {
    bool opaque = true;            // false: reflecting slide (glass)
    double surfaceY = 650;         // px, the surface in the middle of the frame
    double tiltDeg = 0;            // positive: right side lower
    double waveAmpPx = 0, waveLenPx = 600, wavePhase = 0;
    double curlPx = 0;             // the sheet's ends rise by this much (curled)
    double ridgeX0 = 0, ridgeX1 = 0, ridgePx = 0;   // a raised flat strip (fibrous ridge)
    double stepX = 0, stepPx = 0;  // the surface drops by stepPx right of stepX
    double liftX = 0, liftPx = 0;  // the sheet's edge lifted left of liftX (a separate dark wedge above)
    double fibres = 0;             // fibres per 100 px along the edge (opaque)
    double swellPx = 0;            // the wet sheet rises under and around the drop, up to this much
    double bandPx = 40;            // opaque: thickness of the dark band (then a light strip, then the stage)
    double stageY = 820;           // the dark stage below, px
};

struct SimSpec {
    QString name, title;
    int width = 1920, height = 1080;
    double fps = 30, seconds = 30;
    double pxPerMm = 94.23;
    double density = 997, tension = 72;     // the liquid (kg/m³, mN/m)
    SimSubstrate sub;
    double dropX = 960;                     // px
    double tContact = 3;                    // s
    double volumeUl = 4;                    // dispensed
    double theta0 = 110, thetaEq = 60, spreadTau = 2;   // degrees, s
    double soakUlPerSqrtS = 0;              // volume soaked in = k·√(t − tContact)
    double soakDelay = 0;                   // s after contact before soaking starts (wetting the sheet first)
    double evaporationUlPerS = 0;
    double needleMm = 0.4745;
    double background = 112, dark = 12, noise = 2.0, blurPx = 1.5;
    std::uint32_t seed = 1;
};

struct SimTruth {
    double t = 0;
    bool onSurface = false;                 // touched and not yet absorbed
    double thetaDeg = 0, baseMm = 0, heightMm = 0, volumeUl = 0;
    double contactLeftX = 0, contactRightX = 0, baselineY = 0;   // px
};

// The scenarios offered (name: short id for the CLI and the simulated camera; title: for people).
std::vector<SimSpec> simPresets();
bool simPreset(const QString& name, SimSpec* out);

// The drop's state at time t (s from the start of the video).
SimTruth simTruth(const SimSpec& s, double t);
// The camera frame at time t (grey levels 0–255) and, if wanted, the truth.
GrayImage simFrame(const SimSpec& s, double t, SimTruth* truth = nullptr);

}  // namespace ld
