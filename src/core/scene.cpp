#include "scene.h"

#include <QtGlobal>
#include <algorithm>
#include <cmath>

namespace ld {

std::vector<Blob> labelDark(const GrayImage& im, double T, std::vector<int>* labOut) {
    const int W = im.w, H = im.h;
    std::vector<int>& lab = *labOut;
    lab.assign(std::size_t(W) * H, 0);
    std::vector<Blob> blobs;
    std::vector<int> stack;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            if (im.at(x, y) >= T || lab[std::size_t(y) * W + x]) continue;
            Blob c;
            c.id = int(blobs.size()) + 1;
            c.x0 = c.y0 = 1 << 30;
            stack.assign(1, y * W + x);
            lab[std::size_t(y) * W + x] = c.id;
            while (!stack.empty()) {
                const int k = stack.back();
                stack.pop_back();
                const int cx = k % W, cy = k / W;
                ++c.n;
                c.x0 = std::min(c.x0, cx), c.x1 = std::max(c.x1, cx), c.y0 = std::min(c.y0, cy), c.y1 = std::max(c.y1, cy);
                const int nb[4][2] = {{cx - 1, cy}, {cx + 1, cy}, {cx, cy - 1}, {cx, cy + 1}};
                for (const auto& q : nb)
                    if (q[0] >= 0 && q[0] < W && q[1] >= 0 && q[1] < H && im.at(q[0], q[1]) < T &&
                        !lab[std::size_t(q[1]) * W + q[0]]) {
                        lab[std::size_t(q[1]) * W + q[0]] = c.id;
                        stack.push_back(q[1] * W + q[0]);
                    }
            }
            blobs.push_back(c);
        }
    return blobs;
}

bool reflectionContacts(const GrayImage& im, const std::vector<int>& lab, const Blob& c, double T, Pt* left, Pt* right) {
    const int W = im.w;
    const int n = c.y1 - c.y0 + 1;
    if (n < 5) return false;
    // per row: outermost pixel of the blob on each side (sub-pixel threshold crossing)
    std::vector<double> L(static_cast<std::size_t>(n), kNaN), R(static_cast<std::size_t>(n), kNaN);
    for (int y = c.y0; y <= c.y1; ++y) {
        int l = -1, r = -1;
        for (int x = c.x0; x <= c.x1; ++x)
            if (lab[std::size_t(y) * W + x] == c.id) {
                if (l < 0) l = x;
                r = x;
            }
        if (l < 0) continue;
        const std::size_t i = std::size_t(y - c.y0);
        const double al = l > 0 ? im.at(l - 1, y) : T, cl = im.at(l, y);
        const double ar = r < W - 1 ? im.at(r + 1, y) : T, cr = im.at(r, y);
        L[i] = al > cl ? l - 1 + (al - T) / (al - cl) : l;
        R[i] = ar > cr ? r + (cr - T) / (cr - ar) : r;
    }
    // e(y): how far the outline reaches out on one side; the contact row is its corner
    auto corner = [&](const std::vector<double>& e, double* yOut) {
        const double minDepth = 3.0;
        double bestDepth = 0;
        int neck = -1;
        auto val = [&](int i) { return std::isfinite(e[std::size_t(i)]) ? e[std::size_t(i)] : -1e9; };
        std::vector<double> up(static_cast<std::size_t>(n)), down(static_cast<std::size_t>(n));
        for (int i = 0; i < n; ++i) up[std::size_t(i)] = std::max(i ? up[std::size_t(i - 1)] : -1e9, val(i));
        for (int i = n - 1; i >= 0; --i) down[std::size_t(i)] = std::max(i < n - 1 ? down[std::size_t(i + 1)] : -1e9, val(i));
        for (int i = 1; i < n - 1; ++i) {
            if (!std::isfinite(e[std::size_t(i)])) continue;
            const double d = std::min(up[std::size_t(i - 1)], down[std::size_t(i + 1)]) - e[std::size_t(i)];
            if (d > bestDepth) bestDepth = d, neck = i;
        }
        int k = -1;
        // θ > 90°: the neck between drop and reflection; else the outermost point
        const double span = up[std::size_t(n - 1)] - *std::min_element(up.begin(), up.end());
        if (neck >= 0 && bestDepth >= std::max(minDepth, 0.03 * std::abs(span))) {
            k = neck;
        } else {
            double m = -1e9;
            for (int i = 0; i < n; ++i)
                if (std::isfinite(e[std::size_t(i)]) && e[std::size_t(i)] > m) m = e[std::size_t(i)], k = i;
        }
        if (k <= 0 || k >= n - 1) return false;   // a reflection must be visible below the contact point
        double yk = k;
        const double a = e[std::size_t(k - 1)], b = e[std::size_t(k)], cc = e[std::size_t(k + 1)];
        if (std::isfinite(a) && std::isfinite(cc)) {
            const double den = a - 2 * b + cc;
            if (std::abs(den) > 1e-9) yk = k + std::clamp(0.5 * (a - cc) / den, -0.5, 0.5);
        }
        *yOut = c.y0 + yk;
        return true;
    };
    std::vector<double> eL(static_cast<std::size_t>(n)), eR(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) eL[std::size_t(i)] = -L[std::size_t(i)], eR[std::size_t(i)] = R[std::size_t(i)];
    double yl = 0, yr = 0;
    if (!corner(eL, &yl) || !corner(eR, &yr)) return false;
    auto interp = [&](const std::vector<double>& v, double y) {
        const double t = y - c.y0;
        const int i = std::clamp(int(std::floor(t)), 0, n - 2);
        const double f = t - i;
        return (1 - f) * v[std::size_t(i)] + f * v[std::size_t(i + 1)];
    };
    *left = {interp(L, yl), yl};
    *right = {interp(R, yr), yr};
    return std::isfinite(left->x) && std::isfinite(right->x);
}

namespace {

constexpr double kInlier = 2.5;   // px: corners on the same surface line

double lineY(const Baseline& b, double x) { return b.p0.y + std::tan(b.angle) * (x - b.p0.x); }

struct Candidate {
    const Blob* blob;
    bool top;           // reaches the top row (needle)
    bool corners = false;
    Pt l, r;
};

}  // namespace

std::optional<Baseline> combineSurfaces(const std::vector<Baseline>& found) {
    std::vector<double> ys, as, xs;
    for (const Baseline& b : found)
        if (b.found) ys.push_back(b.p0.y), as.push_back(b.angle), xs.push_back(b.p0.x);
    if (ys.empty()) return std::nullopt;
    // all to a common x (the first one's), then medians
    const double x = xs[0];
    for (std::size_t i = 0; i < ys.size(); ++i) ys[i] += std::tan(as[i]) * (x - xs[i]);
    auto median = [](std::vector<double> v) {
        std::nth_element(v.begin(), v.begin() + long(v.size() / 2), v.end());
        return v[v.size() / 2];
    };
    Baseline b;
    b.p0 = {x, median(ys)};
    b.angle = median(as);
    b.found = true;
    b.reflection = true;
    return b;
}

Scene analyzeScene(const GrayImage& im, const SceneOptions& opt) {
    Scene s;
    if (im.w < 50 || im.h < 50) {
        s.error = QStringLiteral("image too small");
        return s;
    }
    s.dark = percentile(im, 0.02);
    s.background = percentile(im, 0.98);
    s.threshold = 0.5 * (s.dark + s.background);
    if (s.background < 60 || s.background - s.dark < 30) {
        s.error = QStringLiteral("image too dark or no contrast");
        return s;
    }
    const double T = s.threshold;
    std::vector<int> lab;
    const std::vector<Blob> blobs = labelDark(im, T, &lab);
    std::vector<Candidate> cands;
    for (const Blob& b : blobs) {
        if (b.y1 >= im.h - 1 || (b.x0 == 0 && b.x1 == im.w - 1) || b.n < opt.minPixels) continue;
        if (b.x1 - b.x0 < 8 || b.y1 - b.y0 < 8) continue;
        cands.push_back({&b, b.y0 == 0, false, {}, {}});
    }

    // surface: the line through one blob's corners that most other blobs' corners agree with
    if (opt.surface) {
        s.surface = *opt.surface;
    } else {
        for (Candidate& c : cands) c.corners = reflectionContacts(im, lab, *c.blob, T, &c.l, &c.r) && c.r.x > c.l.x + 8;
        if (qEnvironmentVariableIsSet("LIBREDROP_DEBUG"))
            for (const Candidate& c : cands)
                qDebug("blob %d n %d x %d..%d y %d..%d top %d corners %d L(%.1f,%.1f) R(%.1f,%.1f)", c.blob->id, c.blob->n,
                       c.blob->x0, c.blob->x1, c.blob->y0, c.blob->y1, c.top, c.corners, c.l.x, c.l.y, c.r.x, c.r.y);
        // corner points, weighted by how far their blob rises above them (drops are tall; specks and lines are
        // not). Blobs on the needle count only if nothing else has corners: a hanging drop has "corners" too.
        struct Corner {
            Pt p;
            double w;
        };
        std::vector<Corner> pts;
        bool anyFree = false;
        for (const Candidate& c : cands) anyFree |= c.corners && !c.top;
        for (const Candidate& c : cands) {
            if (!c.corners || (c.top && anyFree)) continue;
            const double rise = std::max(1.0, std::min(c.l.y, c.r.y) - c.blob->y0);
            pts.push_back({c.l, rise});
            pts.push_back({c.r, rise});
        }
        // the line through two corners that the most weight agrees with (within kInlier), then least squares
        const double maxSlope = std::tan(5 * M_PI / 180);
        double bestScore = -1, a = 0, b = 0;
        for (std::size_t i = 0; i < pts.size(); ++i)
            for (std::size_t j = i + 1; j < pts.size(); ++j) {
                const Pt p = pts[i].p, q = pts[j].p;
                if (std::abs(q.x - p.x) < 8) continue;
                const double sl = (q.y - p.y) / (q.x - p.x);
                if (std::abs(sl) > maxSlope) continue;
                double score = 0;
                for (const Corner& c : pts)
                    if (std::abs(c.p.y - p.y - sl * (c.p.x - p.x)) < kInlier) score += c.w;
                if (score > bestScore) bestScore = score, b = sl, a = p.y - sl * p.x;
            }
        if (bestScore < 0) {
            s.error = QStringLiteral("surface not found");
            return s;
        }
        for (int pass = 0; pass < 2; ++pass) {
            double sw = 0, sx = 0, sy = 0, sxx = 0, sxy = 0;
            int n = 0;
            for (const Corner& c : pts)
                if (std::abs(c.p.y - a - b * c.p.x) < kInlier) {
                    sw += c.w, sx += c.w * c.p.x, sy += c.w * c.p.y, sxx += c.w * c.p.x * c.p.x, sxy += c.w * c.p.x * c.p.y;
                    ++n;
                }
            const double det = sw * sxx - sx * sx;
            if (n >= 3 && std::abs(det) > 1e-9 * sw * sw) {
                b = (sw * sxy - sx * sy) / det;
                a = (sy - b * sx) / sw;
            }
            s.surfaceVotes = n;
        }
        s.surface.p0 = {im.w / 2.0, a + b * im.w / 2.0};
        s.surface.angle = std::atan(b);
        s.surface.found = true;
        s.surface.reflection = true;
    }
    s.ok = true;

    // drops: blobs crossing the surface and rising above it
    struct Span {
        const Candidate* c;
        int x0, x1;
    };
    std::vector<Span> spans;
    for (const Candidate& c : cands) {
        const Blob& b = *c.blob;
        // the needle: narrow rows from the top down (its width is the first row's)
        int yStart = b.y0;
        if (c.top) {
            int w0 = 0;
            for (int x = b.x0; x <= b.x1; ++x) w0 += lab[std::size_t(b.y0) * im.w + x] == b.id;
            for (; yStart <= b.y1; ++yStart) {
                int l = -1, r = -1;
                for (int x = b.x0; x <= b.x1; ++x)
                    if (lab[std::size_t(yStart) * im.w + x] == b.id) {
                        if (l < 0) l = x;
                        r = x;
                    }
                if (l >= 0 && r - l + 1 > 1.6 * w0 + 2) break;
            }
        }
        // columns of the blob above the surface (below the needle); must rise minHeightPx above it
        int x0 = im.w, x1 = -1;
        double rise = 0;
        bool below = false;
        for (int y = yStart; y <= b.y1; ++y)
            for (int x = b.x0; x <= b.x1; ++x) {
                if (lab[std::size_t(y) * im.w + x] != b.id) continue;
                const double h = lineY(s.surface, x) - y;
                if (h > 1) {
                    x0 = std::min(x0, x), x1 = std::max(x1, x);
                    rise = std::max(rise, h);
                } else if (h < -1) {
                    below = true;
                }
            }
        // a drop touches the surface: dark right above the line (a hanging drop ends higher up)
        bool touches = false;
        for (int x = std::max(x0, 0); x <= x1 && !touches; ++x) {
            const int y = int(std::floor(lineY(s.surface, x))) - 2;
            touches = y >= 0 && y < im.h && lab[std::size_t(y) * im.w + x] == b.id;
        }
        if (x1 < 0 || rise < opt.minHeightPx || x1 - x0 + 1 < opt.minWidthPx || !touches || !(below || !opt.surface)) continue;
        spans.push_back({&c, x0, x1});
    }
    std::sort(spans.begin(), spans.end(), [](const Span& a, const Span& b) { return a.x0 < b.x0; });
    for (std::size_t i = 0; i < spans.size(); ++i) {
        const Span& sp = spans[i];
        const int w = sp.x1 - sp.x0 + 1;
        const int margin = std::max(12, w / 5);
        int lo = sp.x0 - margin, hi = sp.x1 + margin;
        if (i > 0) lo = std::max(lo, (spans[i - 1].x1 + sp.x0) / 2 + 1);
        if (i + 1 < spans.size()) hi = std::min(hi, (sp.x1 + spans[i + 1].x0) / 2 - 1);
        lo = std::max(lo, 0), hi = std::min(hi, im.w - 1);
        SceneDrop d;
        d.x0 = lo, d.x1 = hi;
        d.xc = 0.5 * (sp.x0 + sp.x1);
        d.width = sp.x1 - sp.x0 + 1;
        d.onNeedle = sp.c->top;
        DropOptions o = opt.drop;
        Baseline bl = s.surface;
        bl.p0 = {bl.p0.x - lo, bl.p0.y};
        o.baseline = bl;
        o.dark = s.dark, o.background = s.background;   // same threshold as the blobs
        // only this blob is dark in the cut-out (other drops, the needle elsewhere and specks are blanked)
        GrayImage cut = crop(im, lo, 0, hi - lo + 1, im.h);
        for (int y = 0; y < cut.h; ++y)
            for (int x = 0; x < cut.w; ++x) {
                const int l = lab[std::size_t(y) * im.w + lo + x];
                if (l && l != sp.c->blob->id && y < lineY(s.surface, lo + x) + 2) cut.at(x, y) = float(s.background);
            }
        d.a = analyzeDrop(cut, o);
        if ((sp.x0 <= 1 || sp.x1 >= im.w - 2) && !d.a.hasCheck(QStringLiteral("border")))
            d.a.checks.push_back({Check::Error, QStringLiteral("border"),
                                  QStringLiteral("The drop reaches the edge of the image.")});
        s.drops.push_back(std::move(d));
    }
    return s;
}

}  // namespace ld
