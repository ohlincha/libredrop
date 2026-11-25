#include "simulate.h"

#include <algorithm>
#include <cmath>
#include <thread>

#include "younglaplace.h"

namespace ld {

namespace {

constexpr double kG = 9.81;

double smoothstep(double e0, double e1, double x) {
    const double t = std::clamp((x - e0) / (e1 - e0), 0.0, 1.0);
    return t * t * (3 - 2 * t);
}

// A sessile drop of angle θ and volume V at this scale: its profile and apex radius b (px), with the Bond number
// from b (fixed point: b depends on the Bond number through the shape, the Bond number on b).
struct Shape {
    Profile p;
    double b = 0;          // px
    double halfBase = 0;   // px
    double height = 0;     // px
};

Shape shapeFor(const SimSpec& s, double thetaDeg, double volumeUl, double ds = 0.002) {
    Shape out;
    const double vPx = volumeUl * s.pxPerMm * s.pxPerMm * s.pxPerMm;   // 1 µL = 1 mm³
    const double th = std::clamp(thetaDeg, 0.5, 179.0) * M_PI / 180;
    double b = 0, bond = 0;
    for (int it = 0; it < 8; ++it) {
        out.p = sessile(bond, th, ds);
        if (out.p.empty() || out.p.V.back() <= 0) return {};
        b = std::cbrt(vPx / out.p.V.back());
        const double bm = b / s.pxPerMm * 1e-3;   // m
        bond = s.density * kG * bm * bm / (s.tension * 1e-3);
    }
    out.b = b;
    out.halfBase = out.p.X.back() * b;
    out.height = out.p.Z.back() * b;
    return out;
}

// The angle at which a drop of this volume has this base half-width (pinned contact line): bisection, wider base ↔
// smaller angle.
double pinnedAngle(const SimSpec& s, double halfBasePx, double volumeUl) {
    double lo = 0.5, hi = 179;
    for (int it = 0; it < 30; ++it) {
        const double mid = 0.5 * (lo + hi);
        const Shape sh = shapeFor(s, mid, volumeUl, 0.004);
        if (sh.halfBase > halfBasePx) lo = mid;   // too wide: steeper
        else hi = mid;
    }
    return 0.5 * (lo + hi);
}

// Rows [0, n) split over the processor's cores.
template <class F>
void parallelRows(int n, F f) {
    const int k = std::clamp(int(std::thread::hardware_concurrency()), 1, 16);
    std::vector<std::thread> th;
    for (int i = 0; i < k; ++i)
        th.emplace_back([=, &f] {
            for (int y = n * i / k; y < n * (i + 1) / k; ++y) f(y);
        });
    for (std::thread& t : th) t.join();
}

double hash01(std::uint32_t a, std::uint32_t b) {
    std::uint32_t h = a * 2654435761u ^ (b + 0x9e3779b9u + (a << 6) + (a >> 2));
    h ^= h >> 16, h *= 0x7feb352du, h ^= h >> 15, h *= 0x846ca68bu, h ^= h >> 16;
    return (h + 0.5) / 4294967296.0;
}

}  // namespace

std::vector<SimSpec> simPresets() {
    std::vector<SimSpec> v;
    {
        SimSpec s;
        s.name = QStringLiteral("glass"), s.title = QStringLiteral("Water on glass (reflection)");
        s.sub.opaque = false, s.sub.surfaceY = 680, s.sub.stageY = 900;
        s.theta0 = 75, s.thetaEq = 35, s.spreadTau = 2.5, s.volumeUl = 4, s.evaporationUlPerS = 0.01;
        s.seconds = 20, s.tContact = 3;
        v.push_back(s);
    }
    {
        SimSpec s;
        s.name = QStringLiteral("separator"), s.title = QStringLiteral("Flat separator, soaking");
        s.theta0 = 120, s.thetaEq = 95, s.spreadTau = 1, s.volumeUl = 4, s.soakUlPerSqrtS = 0.9, s.soakDelay = 1;
        s.seconds = 30;
        v.push_back(s);
    }
    {
        SimSpec s;
        s.name = QStringLiteral("wavy-paper"), s.title = QStringLiteral("Wavy paper, soaking");
        s.sub.tiltDeg = 0.6, s.sub.waveAmpPx = 6, s.sub.waveLenPx = 700, s.sub.fibres = 3;
        s.theta0 = 115, s.thetaEq = 85, s.spreadTau = 2, s.volumeUl = 5, s.soakUlPerSqrtS = 0.6, s.soakDelay = 2;
        s.seconds = 40;
        v.push_back(s);
    }
    {
        SimSpec s;   // like the 3M post-it of 2026-11-25
        s.name = QStringLiteral("curled-postit"), s.title = QStringLiteral("Curled post-it with a ridge (hard)");
        s.sub.curlPx = 25, s.sub.ridgeX0 = 1250, s.sub.ridgeX1 = 1550, s.sub.ridgePx = 18, s.sub.stepX = 1560;
        s.sub.stepPx = 14, s.sub.liftX = 250, s.sub.liftPx = 35, s.sub.fibres = 5, s.sub.swellPx = 12;
        s.theta0 = 113, s.thetaEq = 90, s.spreadTau = 4, s.volumeUl = 6, s.soakUlPerSqrtS = 0.7, s.soakDelay = 2;
        s.dropX = 1000, s.seconds = 60;
        v.push_back(s);
    }
    {
        SimSpec s;
        s.name = QStringLiteral("hydrophobic"), s.title = QStringLiteral("Hydrophobic film (water beads up)");
        s.theta0 = 145, s.thetaEq = 135, s.spreadTau = 0.5, s.volumeUl = 3, s.evaporationUlPerS = 0.005;
        s.seconds = 15;
        v.push_back(s);
    }
    return v;
}

SimSpec simScaled(const SimSpec& s, double f) {
    SimSpec o = s;
    o.width = int(std::lround(s.width * f)), o.height = int(std::lround(s.height * f));
    o.pxPerMm = s.pxPerMm * f;
    o.dropX = s.dropX * f;
    SimSubstrate& u = o.sub;
    for (double* v : {&u.surfaceY, &u.waveAmpPx, &u.waveLenPx, &u.curlPx, &u.ridgeX0, &u.ridgeX1, &u.ridgePx, &u.stepX, &u.stepPx,
                      &u.liftX, &u.liftPx, &u.swellPx, &u.bandPx, &u.stageY})
        *v *= f;
    u.fibres = s.sub.fibres;   // per 100 px of the frame: as many fibres along the same edge would be denser; keep it simple
    o.blurPx = s.blurPx * f;
    return o;
}

bool simPreset(const QString& name, SimSpec* out) {
    for (const SimSpec& s : simPresets())
        if (s.name == name) {
            *out = s;
            return true;
        }
    return false;
}

namespace {

double surfaceAt(const SimSpec& s, double x, double t, double dropHalfBase) {
    const SimSubstrate& u = s.sub;
    const double c = s.width / 2.0;
    double y = u.surfaceY + std::tan(u.tiltDeg * M_PI / 180) * (x - c);
    if (u.waveAmpPx) y += u.waveAmpPx * std::sin(2 * M_PI * x / u.waveLenPx + u.wavePhase);
    if (u.curlPx) y -= u.curlPx * std::pow((x - c) / c, 2);
    if (u.ridgePx) y -= u.ridgePx * smoothstep(u.ridgeX0 - 20, u.ridgeX0, x) * (1 - smoothstep(u.ridgeX1, u.ridgeX1 + 20, x));
    if (u.stepPx) y += u.stepPx * smoothstep(u.stepX - 8, u.stepX + 8, x);
    if (u.swellPx && u.opaque && t > s.tContact) {
        const double w = dropHalfBase + 150;
        y -= u.swellPx * std::min(1.0, (t - s.tContact) / 15) * std::exp(-std::pow((x - s.dropX) / w, 2));
    }
    return y;
}

}  // namespace

SimTruth simTruth(const SimSpec& s, double t) {
    SimTruth tr;
    tr.t = t;
    if (t < s.tContact) return tr;
    const double tau = t - s.tContact;
    auto volumeAt = [&](double z) {
        return s.volumeUl - s.soakUlPerSqrtS * std::sqrt(std::max(0.0, z - s.soakDelay)) - s.evaporationUlPerS * z;
    };
    auto freeAngle = [&](double z) { return s.thetaEq + (s.theta0 - s.thetaEq) * std::exp(-z / std::max(1e-3, s.spreadTau)); };
    const double V = volumeAt(tau);
    if (V <= 0.02) return tr;   // soaked in
    double theta = freeAngle(tau);
    if (s.soakUlPerSqrtS > 0 && tau > s.soakDelay) {
        // pinned from when soaking starts; the line still advances if the free drop would be wider
        const Shape pin = shapeFor(s, freeAngle(s.soakDelay), volumeAt(s.soakDelay), 0.004);
        const Shape free = shapeFor(s, theta, V, 0.004);
        const double a = std::max(pin.halfBase, free.halfBase);
        theta = pinnedAngle(s, a, V);
    }
    const Shape sh = shapeFor(s, theta, V);
    if (sh.b <= 0 || sh.height < 2 || theta < 2) return tr;   // too flat to see: absorbed
    tr.onSurface = true;
    tr.thetaDeg = theta;
    tr.volumeUl = V;
    tr.baseMm = 2 * sh.halfBase / s.pxPerMm;
    tr.heightMm = sh.height / s.pxPerMm;
    const double yl = surfaceAt(s, s.dropX - sh.halfBase, t, sh.halfBase), yr = surfaceAt(s, s.dropX + sh.halfBase, t, sh.halfBase);
    tr.contactLeftX = s.dropX - sh.halfBase, tr.contactRightX = s.dropX + sh.halfBase;
    tr.baselineY = 0.5 * (yl + yr);
    return tr;
}

GrayImage simFrame(const SimSpec& s, double t, SimTruth* truthOut) {
    const SimTruth tr = simTruth(s, t);
    if (truthOut) *truthOut = tr;
    const SimSubstrate& u = s.sub;
    // the drop on the surface (rotated with the line between its contact points) or hanging from the needle
    Shape sh;
    double cx = s.dropX, by = 0, ang = 0, ca = 1, sa = 0;
    if (tr.onSurface) {
        sh = shapeFor(s, tr.thetaDeg, tr.volumeUl, 0.001);
        const double yl = surfaceAt(s, cx - sh.halfBase, t, sh.halfBase), yr = surfaceAt(s, cx + sh.halfBase, t, sh.halfBase);
        ang = std::atan2(yr - yl, 2 * sh.halfBase), ca = std::cos(ang), sa = std::sin(ang);
        by = 0.5 * (yl + yr);
    }
    const double needleW = s.needleMm * s.pxPerMm;
    const double R = std::cbrt(3 * s.volumeUl * std::pow(s.pxPerMm, 3) / (4 * M_PI));   // pendant drop ~ a sphere
    double needleTip;
    double pendantCy = -1e9;
    const double surfMid = surfaceAt(s, cx, 0, 0);
    if (t < s.tContact) {   // coming down: the hanging drop's bottom reaches the surface at tContact
        const double tipAtContact = surfMid - 1.9 * R, tipStart = std::max(needleW, tipAtContact - 350);
        const double f = std::clamp(t / std::max(0.1, s.tContact - 0.3), 0.0, 1.0);
        needleTip = tipStart + f * (tipAtContact - tipStart);
        pendantCy = needleTip + 0.9 * R;
    } else {               // the needle goes back up a little after the drop is left behind
        const double apex = tr.onSurface ? by - sh.height : surfMid;
        needleTip = std::min(apex, surfMid - 1.9 * R) - 40 - 120 * std::min(1.0, (t - s.tContact) / 2);
    }

    const double light = s.background;
    // the sample's edge per column (it depends on x only), interpolated for the sub-pixel samples
    std::vector<double> edge(std::size_t(s.width) + 2);
    for (int x = 0; x < s.width + 2; ++x) edge[std::size_t(x)] = surfaceAt(s, x - 1, t, sh.halfBase);
    auto surf = [&](double x) {
        const double f = std::clamp(x + 1, 0.0, double(s.width) + 0.999);
        const int i = int(f);
        return edge[std::size_t(i)] + (f - i) * (edge[std::size_t(std::min(i + 1, s.width + 1))] - edge[std::size_t(i)]);
    };
    auto intensity = [&](double x, double y) -> double {
        double v = light * (1.0 - 0.06 * y / s.height);
        if (std::abs(x - cx) <= needleW / 2 && y <= needleTip) return s.dark;                   // needle
        if (pendantCy > -1e8 && std::hypot(x - cx, y - pendantCy) <= R) return s.dark;         // hanging drop
        const double ys = surf(x);
        if (tr.onSurface) {
            const double dx = x - cx, dy = y - by;
            const double along = dx * ca + dy * sa, up = -(-dx * sa + dy * ca);   // height above the drop's baseline
            const double depth = std::abs(up);
            if (depth <= sh.height && (up >= 0 || !u.opaque)) {
                double xr;
                if (sh.p.atDepth((sh.height - depth) / sh.b, &xr) && std::abs(along) <= xr * sh.b)
                    return up >= 0 ? s.dark : s.dark + 18;                                       // drop / its reflection
            }
        }
        if (y >= u.stageY) return s.dark + 4;                                                    // the stage
        if (u.opaque) {
            if (u.liftPx && x < u.liftX) {   // the lifted sheet edge: a thin dark wedge above
                const double yl = ys - u.liftPx * (1 - x / u.liftX);
                if (y >= yl && y <= yl + 7) return s.dark + 6;
            }
            if (y >= ys && y < ys + u.bandPx) return s.dark + 2;                                 // the sheet
            if (y >= ys + u.bandPx) return 0.85 * v;                                             // the light strip
            if (u.fibres > 0 && y < ys && y > ys - 12) {   // fibres along the edge
                const int bin = int(std::floor(x / 3));
                if (hash01(s.seed, std::uint32_t(bin)) < u.fibres * 3 / 100) {
                    const double len = 3 + 9 * hash01(s.seed + 7, std::uint32_t(bin));
                    const double fx = bin * 3 + 3 * hash01(s.seed + 13, std::uint32_t(bin));
                    if (std::abs(x - fx) < 0.7 && y > ys - len) return s.dark + 10;
                }
            }
            return v;
        }
        if (y >= ys && y < ys + 2) return 0.8 * v;                                              // glass surface
        if (y >= ys) return 0.95 * v;                                                            // glass
        return v;
    };

    // pixels: the four corners decide; where they differ, 4×4 samples
    const int W = s.width, H = s.height;
    std::vector<float> corner(std::size_t(W + 1) * (H + 1));
    parallelRows(H + 1, [&](int y) {
        for (int x = 0; x <= W; ++x) corner[std::size_t(y) * (W + 1) + x] = float(intensity(x - 0.5, y - 0.5));
    });
    GrayImage im(W, H);
    parallelRows(H, [&](int y) {
        for (int x = 0; x < W; ++x) {
            const float a = corner[std::size_t(y) * (W + 1) + x], b = corner[std::size_t(y) * (W + 1) + x + 1];
            const float c = corner[std::size_t(y + 1) * (W + 1) + x], d = corner[std::size_t(y + 1) * (W + 1) + x + 1];
            if (std::abs(a - b) < 0.5f && std::abs(a - c) < 0.5f && std::abs(a - d) < 0.5f) {
                im.at(x, y) = 0.25f * (a + b + c + d);
                continue;
            }
            double sum = 0;
            for (int j = 0; j < 4; ++j)
                for (int i = 0; i < 4; ++i) sum += intensity(x - 0.5 + (i + 0.5) / 4, y - 0.5 + (j + 0.5) / 4);
            im.at(x, y) = float(sum / 16);
        }
    });
    // the lens: a Gaussian blur, separable, over all cores
    GrayImage out = im;
    if (s.blurPx > 0) {
        const int r = std::max(1, int(std::ceil(3 * s.blurPx)));
        std::vector<float> k(std::size_t(2 * r + 1));
        float ks = 0;
        for (int i = -r; i <= r; ++i) ks += k[std::size_t(i + r)] = float(std::exp(-0.5 * i * i / (s.blurPx * s.blurPx)));
        for (float& v : k) v /= ks;
        GrayImage tmp(W, H);
        parallelRows(H, [&](int y) {
            const float* in = &im.px[std::size_t(y) * W];
            float* o = &tmp.px[std::size_t(y) * W];
            for (int x = 0; x < W; ++x) {
                float a = 0;
                for (int i = -r; i <= r; ++i) a += k[std::size_t(i + r)] * in[std::clamp(x + i, 0, W - 1)];
                o[x] = a;
            }
        });
        parallelRows(H, [&](int y) {
            float* o = &out.px[std::size_t(y) * W];
            for (int x = 0; x < W; ++x) {
                float a = 0;
                for (int i = -r; i <= r; ++i) a += k[std::size_t(i + r)] * tmp.px[std::size_t(std::clamp(y + i, 0, H - 1)) * W + x];
                o[x] = a;
            }
        });
    }
    if (s.noise > 0)   // sensor noise, different in every frame; each row its own generator (the same on any machine)
        parallelRows(H, [&](int y) {
            std::uint32_t st = s.seed * 2654435761u + std::uint32_t(std::lround(t * 1000)) * 40503u + std::uint32_t(y) * 2246822519u + 12345u;
            float* row = &out.px[std::size_t(y) * W];
            for (int x = 0; x < W; ++x) {
                float g = 0;   // sum of four uniforms: nearly Gaussian, variance 4/12
                for (int i = 0; i < 4; ++i) st = st * 1664525u + 1013904223u, g += float(st >> 8) * (1.0f / 16777216.0f);
                row[x] = std::clamp(row[x] + float(s.noise) * (g - 2.0f) * 1.7320508f, 0.0f, 255.0f);
            }
        });
    return out;
}

}  // namespace ld
