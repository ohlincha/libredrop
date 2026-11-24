#pragma once
// A whole camera frame: the substrate surface, every drop sitting on it, each drop's contact angle.
//
// The frame may hold the needle (with or without a pendant drop), several sessile drops, dirt on the surface
// and the dark stage below. Steps:
// 1. Dark blobs (threshold half-way between background and drop levels). The stage/substrate (touching the
//    bottom or both sides) and specks are dropped.
// 2. Each blob that is a drop standing on a reflecting surface has two contact corners where the drop meets its
//    mirror image (see reflectionContacts). The surface is the line through two corners that most corners agree
//    with (within 2.5 px, weighted by the height of their blob), refitted to those; blobs on the needle count only
//    when nothing else does (a hanging drop also has "corners").
// 3. Drops: blobs that cross the surface line and rise above it. Each is cut out (its own columns, up to half-way
//    to its neighbours) and measured by analyzeDrop with the surface as a fixed baseline.
// For video the surface is found once from several frames and then held fixed (stage and camera do not move).
#include <QString>
#include <optional>
#include <vector>

#include "drop.h"
#include "image.h"

namespace ld {

struct Blob {
    int id = 0, n = 0, x0 = 0, x1 = -1, y0 = 0, y1 = -1;
};

// Connected dark regions (4-neighbour); lab holds the blob id per pixel (0 = background).
std::vector<Blob> labelDark(const GrayImage& im, double T, std::vector<int>* lab);

// Contact points of a drop and its reflection: on each side the outermost point of the outline (θ ≤ 90°) or
// the innermost point of the neck between drop and reflection (θ > 90°). False if no reflection lies below.
bool reflectionContacts(const GrayImage& im, const std::vector<int>& lab, const Blob& b, double T, Pt* left, Pt* right);

struct SceneDrop {
    int x0 = 0, x1 = 0;          // columns of the cut-out (image coordinates)
    double xc = 0;               // centre of the drop's columns (image x)
    double width = 0;            // width of the drop's columns (px)
    bool onNeedle = false;       // the blob reaches the top of the image (needle touching the drop)
    DropAnalysis a;              // in cut-out coordinates; add x0 to x for image coordinates
};

struct SceneOptions {
    DropOptions drop;                    // its baseline is ignored; the surface is used
    std::optional<Baseline> surface;     // fixed surface (video); otherwise found in this frame
    int minPixels = 150;                 // smaller blobs are specks
    double minHeightPx = 4;              // drops must rise this far above the surface
    double minWidthPx = 40;              // and be this wide (narrower: specks and dirt)
};

struct Scene {
    bool ok = false;                     // surface known
    QString error;
    double background = 0, dark = 0, threshold = 0;
    Baseline surface;
    int surfaceVotes = 0;                // corner points on the surface
    std::vector<SceneDrop> drops;        // left to right
};

Scene analyzeScene(const GrayImage& im, const SceneOptions& opt = {});

// Surface from the corners of several frames' scenes: median height and tilt of the found surfaces.
std::optional<Baseline> combineSurfaces(const std::vector<Baseline>& found);

}  // namespace ld
