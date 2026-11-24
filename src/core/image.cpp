#include "image.h"

#include <QImage>
#include <algorithm>
#include <cmath>

#include "younglaplace.h"

namespace ld {

double percentile(const GrayImage& im, double q) {
    std::vector<int> hist(256, 0);
    for (float v : im.px) ++hist[std::size_t(std::clamp(int(v), 0, 255))];
    const double target = q * double(im.px.size());
    double acc = 0;
    for (int i = 0; i < 256; ++i) {
        acc += hist[std::size_t(i)];
        if (acc >= target) return i + 0.5;
    }
    return 255;
}

GrayImage crop(const GrayImage& im, int x0, int y0, int w, int h) {
    x0 = std::clamp(x0, 0, im.w), y0 = std::clamp(y0, 0, im.h);
    w = std::clamp(w, 0, im.w - x0), h = std::clamp(h, 0, im.h - y0);
    GrayImage out(w, h);
    for (int y = 0; y < h; ++y)
        std::copy_n(im.px.begin() + long(std::size_t(y0 + y) * im.w + x0), w, out.px.begin() + long(std::size_t(y) * w));
    return out;
}

GrayImage gaussianBlur(const GrayImage& im, double sigma) {
    if (sigma <= 0.05 || im.empty()) return im;
    const int r = int(std::ceil(3 * sigma));
    std::vector<double> k(std::size_t(2 * r + 1));
    double sum = 0;
    for (int i = -r; i <= r; ++i) sum += k[std::size_t(i + r)] = std::exp(-0.5 * i * i / (sigma * sigma));
    for (double& v : k) v /= sum;
    GrayImage tmp(im.w, im.h), out(im.w, im.h);
    for (int y = 0; y < im.h; ++y)
        for (int x = 0; x < im.w; ++x) {
            double s = 0;
            for (int i = -r; i <= r; ++i) s += k[std::size_t(i + r)] * im.at(std::clamp(x + i, 0, im.w - 1), y);
            tmp.at(x, y) = float(s);
        }
    for (int y = 0; y < im.h; ++y)
        for (int x = 0; x < im.w; ++x) {
            double s = 0;
            for (int i = -r; i <= r; ++i) s += k[std::size_t(i + r)] * tmp.at(x, std::clamp(y + i, 0, im.h - 1));
            out.at(x, y) = float(s);
        }
    return out;
}

GrayImage fromQImage(const QImage& src) {
    if (src.format() == QImage::Format_Grayscale8) {
        GrayImage g(src.width(), src.height());
        for (int y = 0; y < src.height(); ++y) {
            const uchar* line = src.constScanLine(y);
            for (int x = 0; x < src.width(); ++x) g.at(x, y) = line[x];
        }
        return g;
    }
    const QImage im = src.convertToFormat(QImage::Format_RGB32);
    GrayImage g(im.width(), im.height());
    for (int y = 0; y < im.height(); ++y) {
        const auto* line = reinterpret_cast<const QRgb*>(im.constScanLine(y));
        for (int x = 0; x < im.width(); ++x)
            g.at(x, y) = float(0.299 * qRed(line[x]) + 0.587 * qGreen(line[x]) + 0.114 * qBlue(line[x]));
    }
    return g;
}

QImage toQImage(const GrayImage& g) {
    QImage im(g.w, g.h, QImage::Format_Grayscale8);
    for (int y = 0; y < g.h; ++y) {
        uchar* line = im.scanLine(y);
        for (int x = 0; x < g.w; ++x) line[x] = uchar(std::clamp(std::lround(g.at(x, y)), 0L, 255L));
    }
    return im;
}

void synthGeometry(const SynthDrop& d, double* heightPx, double* baseHalfWidthPx, double* volumePx3) {
    const Profile p = sessile(d.bond, d.thetaDeg * M_PI / 180);
    const double b = d.apexRadiusPx;
    if (heightPx) *heightPx = p.Z.back() * b;
    if (baseHalfWidthPx) *baseHalfWidthPx = p.X.back() * b;
    if (volumePx3) *volumePx3 = p.V.back() * b * b * b;
}

GrayImage renderDrop(const SynthDrop& d) {
    const Profile p = sessile(d.bond, d.thetaDeg * M_PI / 180, 0.001);
    const double b = d.apexRadiusPx, H = p.Z.back() * b;
    const double t = d.tiltDeg * M_PI / 180, ct = std::cos(t), st = std::sin(t);
    constexpr int ss = 4;                                   // 4×4 supersampling
    GrayImage im(d.width, d.height);
    for (int y = 0; y < d.height; ++y)
        for (int x = 0; x < d.width; ++x) {
            int darkN = 0;
            for (int j = 0; j < ss; ++j)
                for (int i = 0; i < ss; ++i) {
                    const double dx = x - 0.5 + (i + 0.5) / ss - d.centerX, dy = y - 0.5 + (j + 0.5) / ss - d.baselineY;
                    const double u = dx * ct + dy * st;          // along the baseline
                    const double v = dx * st - dy * ct;          // height above the baseline
                    bool dark = v < 0;                           // substrate
                    if (!dark && v <= H) {
                        double xr;
                        if (p.atDepth((H - v) / b, &xr) && std::abs(u) <= xr * b) dark = true;
                    }
                    if (!dark && d.needle && std::abs(u) <= d.needleWidthPx / 2 && v >= H + d.needleGapPx) dark = true;
                    darkN += dark;
                }
            const double f = double(darkN) / (ss * ss);
            im.at(x, y) = float(d.background + f * (d.dark - d.background));
        }
    GrayImage out = gaussianBlur(im, d.blurPx);
    if (d.noise > 0) {                                      // portable noise (std distributions differ per library)
        std::uint32_t s = d.seed * 2654435761u + 12345u;
        auto uni = [&s] {
            s = s * 1664525u + 1013904223u;
            return (s + 0.5) / 4294967296.0;
        };
        for (float& v : out.px) v = float(v + d.noise * std::sqrt(-2 * std::log(uni())) * std::cos(2 * M_PI * uni()));
    }
    return out;
}

}  // namespace ld
