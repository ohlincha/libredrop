#pragma once
// Grey-scale image (0–255 floats, row-major, pixel centres at integer coordinates) and the synthetic drop
// images used by the tests: a backlit drop of known contact angle and Bond number on a dark substrate.
#include <cstdint>
#include <vector>

class QImage;

namespace ld {

struct GrayImage {
    int w = 0, h = 0;
    std::vector<float> px;
    GrayImage() = default;
    GrayImage(int w_, int h_, float v = 0) : w(w_), h(h_), px(std::size_t(w_) * h_, v) {}
    float at(int x, int y) const { return px[std::size_t(y) * w + x]; }
    float& at(int x, int y) { return px[std::size_t(y) * w + x]; }
    bool empty() const { return px.empty(); }
};

double percentile(const GrayImage& im, double q);   // grey level below which a fraction q of pixels lie
GrayImage crop(const GrayImage& im, int x0, int y0, int w, int h);
GrayImage gaussianBlur(const GrayImage& im, double sigma);
GrayImage fromQImage(const QImage& im);    // luminance
QImage toQImage(const GrayImage& im);

struct SynthDrop {
    double thetaDeg = 90;          // contact angle
    double bond = 0.1;             // Δρ g b² / γ with b the apex radius
    double apexRadiusPx = 200;
    int width = 960, height = 720;
    double centerX = 480, baselineY = 520;
    double tiltDeg = 0;            // baseline tilt (positive: right side lower in the image)
    double blurPx = 0.8, noise = 1.5;
    double background = 220, dark = 25;
    bool needle = false;
    double needleWidthPx = 36;
    double needleGapPx = 40;       // needle tip above the apex (negative: the needle dips into the drop)
    std::uint32_t seed = 1;
};

GrayImage renderDrop(const SynthDrop& d);
// Height and base half-width (px) of the rendered drop, for the tests.
void synthGeometry(const SynthDrop& d, double* heightPx, double* baseHalfWidthPx, double* volumePx3);

}  // namespace ld
