#include "scene.h"

#include <cstdio>
#include <deque>

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
            // near 90° the rows reaching out furthest are the drop's wall and its mirror image: either one stretch
            // (take its middle) or two bulges with a shallow neck between them (take the neck)
            std::vector<std::pair<int, int>> runs;
            for (int i = 0; i < n; ++i) {
                const bool nearMax = std::isfinite(e[std::size_t(i)]) && e[std::size_t(i)] > m - 1.0;
                if (nearMax && (runs.empty() || runs.back().second != i - 1)) runs.push_back({i, i});
                else if (nearMax) runs.back().second = i;
            }
            const auto own = std::find_if(runs.begin(), runs.end(), [&](const auto& r) { return r.first <= k && k <= r.second; });
            const int gapMax = std::max(6, n / 8);
            const std::pair<int, int>* other = nullptr;
            for (const auto& r : runs)
                if (&r != &*own && std::min(std::abs(r.first - own->second), std::abs(own->first - r.second)) <= gapMax &&
                    (!other || std::abs(r.first - k) < std::abs(other->first - k)))
                    other = &r;
            if (other) {
                const int g0 = std::min(own->second, other->second), g1 = std::max(own->first, other->first);
                int kn = g0;
                for (int i = g0; i <= g1; ++i)
                    if (std::isfinite(e[std::size_t(i)]) && e[std::size_t(i)] < e[std::size_t(kn)]) kn = i;
                k = kn;
            } else if (own->second - own->first >= 4) {
                *yOut = c.y0 + 0.5 * (own->first + own->second);
                return own->first > 0 && own->second < n - 1;
            }
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

// Opaque substrates (paper, separators, films): no reflection below the drops, the sample is the dark band across
// the frame and a drop on it is joined to that band. The band's top edge per column (topmost pixel of a blob that
// spans the frame or reaches the bottom); -1 where there is none.
std::vector<int> substrateTop(const GrayImage& im, const std::vector<int>& lab, const std::vector<Blob>& blobs) {
    std::vector<char> sub(blobs.size() + 2, 0);
    for (const Blob& b : blobs)
        if (b.id >= 0 && std::size_t(b.id) < sub.size() && ((b.x0 == 0 && b.x1 == im.w - 1) || b.y1 >= im.h - 1)) sub[std::size_t(b.id)] = 1;
    std::vector<int> top(std::size_t(im.w), -1);
    for (int x = 0; x < im.w; ++x)
        for (int y = 0; y < im.h; ++y) {
            const int l = lab[std::size_t(y) * im.w + x];
            if (l > 0 && std::size_t(l) < sub.size() && sub[std::size_t(l)]) {
                top[std::size_t(x)] = y;
                break;
            }
        }
    return top;
}

// The sample's edge without what stands on it: a closing of the top-edge profile (y down: running maximum, then
// running minimum, over `win` columns). Bumps narrower than win (drops, fibres, dirt) are bridged over; slopes,
// curl and steps are kept. Columns without an edge take their neighbours' value.
std::vector<double> edgeReference(std::vector<int> top, int win) {
    const int w = int(top.size());
    int last = -1;
    for (int x = 0; x < w; ++x) {   // fill gaps
        if (top[std::size_t(x)] > 0) last = top[std::size_t(x)];
        else if (last > 0) top[std::size_t(x)] = last;
    }
    for (int x = w - 1; x >= 0 && top[std::size_t(x)] <= 0; --x) top[std::size_t(x)] = last;
    for (int x = 0; x < w; ++x)
        if (top[std::size_t(x)] <= 0) {
            int k = x;
            while (k < w && top[std::size_t(k)] <= 0) ++k;
            for (int j = x; j < k; ++j) top[std::size_t(j)] = k < w ? top[std::size_t(k)] : 0;
        }
    auto run = [w, win](const std::vector<double>& v, bool takeMax) {   // O(w·log) is plenty for a frame's width
        std::vector<double> out(v.size());
        const int r = win / 2;
        std::vector<double> buf;
        for (int x = 0; x < w; ++x) {
            double m = v[std::size_t(x)];
            for (int k = std::max(0, x - r); k <= std::min(w - 1, x + r); k += 4)
                m = takeMax ? std::max(m, v[std::size_t(k)]) : std::min(m, v[std::size_t(k)]);
            out[std::size_t(x)] = m;
        }
        return out;
    };
    std::vector<double> v(top.begin(), top.end());
    return run(run(v, true), false);
}

// A flat sample with a clean edge: a straight line fits most of the edge (what sticks up left out). Then that line
// is the reference; it is the reliable case (a curled or ridged sample has no single surface line).
bool straightEdge(const std::vector<int>& top, std::vector<double>* ref) {
    const int w = int(top.size());
    std::vector<Pt> pts;
    for (int x = 0; x < w; x += 2)
        if (top[std::size_t(x)] > 0) pts.push_back({double(x), double(top[std::size_t(x)])});
    if (pts.size() < std::size_t(w / 4)) return false;
    auto median = [](std::vector<double> v) {
        std::nth_element(v.begin(), v.begin() + long(v.size() / 2), v.end());
        return v[v.size() / 2];
    };
    std::vector<double> sl;
    const std::size_t gap = std::max<std::size_t>(1, pts.size() / 3);
    for (std::size_t i = 0; i + gap < pts.size(); i += 2) sl.push_back((pts[i + gap].y - pts[i].y) / (pts[i + gap].x - pts[i].x));
    if (sl.empty()) return false;
    double b = median(sl);
    std::vector<double> ic;
    for (const Pt& p : pts) ic.push_back(p.y - b * p.x);
    // the edge's own level: the lower half of the intercepts (drops and fibres only raise it)
    std::sort(ic.begin(), ic.end());
    double a = ic[ic.size() * 2 / 3];
    int n = 0;
    for (int pass = 0; pass < 3; ++pass) {
        double sx = 0, sy = 0, sxx = 0, sxy = 0;
        n = 0;
        for (const Pt& p : pts)
            if (std::abs(p.y - a - b * p.x) < 3) sx += p.x, sy += p.y, sxx += p.x * p.x, sxy += p.x * p.y, ++n;
        const double det = n * sxx - sx * sx;
        if (n < 20 || std::abs(det) < 1e-9) return false;
        b = (n * sxy - sx * sy) / det, a = (sy - b * sx) / n;
    }
    if (n < int(pts.size()) * 6 / 10 || std::abs(b) > std::tan(5 * M_PI / 180)) return false;
    ref->resize(std::size_t(w));
    for (int x = 0; x < w; ++x) (*ref)[std::size_t(x)] = a + b * x;
    return true;
}

// A drop's top is a dome: highest inside, falling towards both ends (a flat-topped ridge of the sample is not).
bool domed(const std::vector<int>& top, const std::vector<double>& ref, int x0, int x1) {
    double sum = 0, mx = 0;
    int at = x0, n = 0;
    for (int x = x0; x <= x1; ++x) {
        const double r = top[std::size_t(x)] > 0 ? ref[std::size_t(x)] - top[std::size_t(x)] : 0.0;
        sum += r, ++n;
        if (r > mx) mx = r, at = x;
    }
    const int w = x1 - x0 + 1;
    return n && mx >= 1.15 * sum / n && at > x0 + w / 6 && at < x1 - w / 6;
}

// The drop's contact points on an opaque sample, from its own outline: from the top of the drop outwards the edge
// first falls steeply (the drop's side), then flattens onto the sample; the contact is where it flattens. The bump's
// ends found against the reference may include the sample's own crests next to the drop (wavy sheet).
void contactEnds(const std::vector<int>& top, int x0, int x1, int* cx0, int* cx1) {
    const int w = int(top.size());
    std::vector<double> sm(top.size());   // the edge, median of 5 (fibres)
    for (int x = 0; x < w; ++x) {
        int v[5], n = 0;
        for (int k = std::max(0, x - 2); k <= std::min(w - 1, x + 2); ++k)
            if (top[std::size_t(k)] > 0) v[n++] = top[std::size_t(k)];
        std::sort(v, v + n);
        sm[std::size_t(x)] = n ? v[n / 2] : -1;
    }
    int apex = x0;
    for (int x = x0; x <= x1; ++x)
        if (sm[std::size_t(x)] > 0 && (sm[std::size_t(apex)] <= 0 || sm[std::size_t(x)] < sm[std::size_t(apex)])) apex = x;
    const int d = 4, lo = std::max(d, x0 - 40), hi = std::min(w - 1 - d, x1 + 40);
    *cx0 = x0, *cx1 = x1;
    auto fall = [&](int x, int dir) {   // how steeply the edge falls going outwards (dir −1: left, +1: right)
        return (sm[std::size_t(x + dir * d)] - sm[std::size_t(x - dir * d)]) / (2.0 * d);
    };
    // each side: the steepest point of the drop's flank, then outwards until the edge is flat (the sample)
    int sx = -1;
    double best = 0.15;
    for (int x = apex - 1; x >= lo; --x)
        if (fall(x, -1) > best) best = fall(x, -1), sx = x;
    if (sx >= 0)
        for (int x = sx; x >= lo; --x)
            if (fall(x, -1) < 0.07) {
                *cx0 = x;
                break;
            }
    sx = -1, best = 0.15;
    for (int x = apex + 1; x <= hi; ++x)
        if (fall(x, 1) > best) best = fall(x, 1), sx = x;
    if (sx >= 0)
        for (int x = sx; x <= hi; ++x)
            if (fall(x, 1) < 0.07) {
                *cx1 = x;
                break;
            }
}

// The baseline under a drop from the sample's edge just outside its contact points (the lower of the edge there:
// fibres stick up).
Baseline edgeBaseline(const std::vector<int>& top, int cx0, int cx1) {
    const int w = int(top.size());
    auto level = [&](int from, int to) {
        std::vector<int> v;
        for (int x = std::max(0, from); x <= std::min(w - 1, to); ++x)
            if (top[std::size_t(x)] > 0) v.push_back(top[std::size_t(x)]);
        if (v.empty()) return std::nan("");
        std::sort(v.begin(), v.end());
        return double(v[v.size() * 3 / 4]);
    };
    double yl = level(cx0 - 30, cx0 - 4), yr = level(cx1 + 4, cx1 + 30);
    if (!std::isfinite(yl)) yl = yr;
    if (!std::isfinite(yr)) yr = yl;
    const double xl = cx0 - 17, xr = cx1 + 17, b = (yr - yl) / (xr - xl);
    Baseline out;
    out.p0 = {0.5 * (cx0 + cx1), yl + b * (0.5 * (cx0 + cx1) - xl)};
    out.angle = std::atan(b);
    out.found = true;
    out.reflection = false;
    return out;
}

// A drop stands up steeply from the sample somewhere on its flanks (its contact angle); a gentle bulge of the
// sample does not: the edge's slope, over 15 px, reaches 0.3 (17°) in the outer thirds.
bool steepSide(const std::vector<int>& top, const std::vector<double>& ref, int x0, int x1) {
    auto riseAt = [&](int x) { return top[std::size_t(x)] > 0 ? ref[std::size_t(x)] - top[std::size_t(x)] : 0.0; };
    const int w = x1 - x0 + 1, d = 15;
    if (w < 3 * d) return false;
    for (int x = x0; x + d <= x0 + w / 3; ++x)
        if (riseAt(x + d) - riseAt(x) >= 0.3 * d) return true;
    for (int x = x1; x - d >= x1 - w / 3; --x)
        if (riseAt(x - d) - riseAt(x) >= 0.3 * d) return true;
    return false;
}

}  // namespace

std::optional<Baseline> combineSurfaces(const std::vector<Scene>& found) {
    std::vector<double> ys, as;
    int reflecting = 0;
    const double x = found.empty() ? 0 : found.front().surface.p0.x;
    std::vector<std::pair<Pt, double>> pts;
    for (const Scene& s : found)
        if (s.ok && s.surface.found) {
            ys.push_back(s.surface.p0.y + std::tan(s.surface.angle) * (x - s.surface.p0.x));
            as.push_back(s.surface.angle);
            reflecting += s.surface.reflection ? 1 : -1;
            pts.insert(pts.end(), s.surfaceCorners.begin(), s.surfaceCorners.end());
        }
    if (ys.empty()) return std::nullopt;
    auto median = [](std::vector<double> v) {
        std::nth_element(v.begin(), v.begin() + long(v.size() / 2), v.end());
        return v[v.size() / 2];
    };
    double b = std::tan(median(as)), a = median(ys) - b * x;
    for (int pass = 0; pass < 3; ++pass) {
        double sw = 0, sx = 0, sy = 0, sxx = 0, sxy = 0;
        for (const auto& [p, w] : pts)
            if (std::abs(p.y - a - b * p.x) < 1.5) sw += w, sx += w * p.x, sy += w * p.y, sxx += w * p.x * p.x, sxy += w * p.x * p.y;
        const double det = sw * sxx - sx * sx;
        if (sw <= 0 || std::abs(det) < 1e-9 * sw * sw) break;
        b = (sw * sxy - sx * sy) / det;
        a = (sy - b * sx) / sw;
    }
    Baseline bl;
    bl.p0 = {x, a + b * x};
    bl.angle = std::atan(b);
    bl.found = true;
    bl.reflection = reflecting > 0;
    return bl;
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
        // corners at each blob's own threshold (half-way between its darkest pixels and the background around
        // it), so they do not move when the picture is cropped or the stage is out of view
        for (Candidate& c : cands) {
            const Blob& b = *c.blob;
            std::vector<float> bg, dk;
            for (int y = b.y0; y <= b.y1; ++y)
                for (int x = std::max(0, b.x0 - 20); x <= std::min(im.w - 1, b.x1 + 20); ++x) {
                    const int l = lab[std::size_t(y) * im.w + x];
                    if (l == 0) bg.push_back(im.at(x, y));
                    else if (l == b.id) dk.push_back(im.at(x, y));
                }
            double Tc = T;
            if (bg.size() > 50 && dk.size() > 20) {
                std::nth_element(bg.begin(), bg.begin() + long(bg.size() / 2), bg.end());
                std::nth_element(dk.begin(), dk.begin() + long(dk.size() / 20), dk.end());
                Tc = 0.5 * (bg[bg.size() / 2] + dk[dk.size() / 20]);
                Tc = std::clamp(Tc, T - 25, T + 25);
            }
            c.corners = reflectionContacts(im, lab, b, Tc, &c.l, &c.r) && c.r.x > c.l.x + 8;
        }
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
        for (const Candidate& c : cands)   // a drop-sized blob with a reflection (not a speck, not the needle)
            anyFree |= c.corners && !c.top && c.blob->x1 - c.blob->x0 + 1 >= opt.minWidthPx &&
                       std::min(c.l.y, c.r.y) - c.blob->y0 >= std::max(opt.minHeightPx, 10.0);
        for (const Candidate& c : cands) {
            // (a blob cut off by the image's side has the image edge as its outer "corner")
            if (!c.corners || (c.top && anyFree) || c.blob->x0 == 0 || c.blob->x1 == im.w - 1) continue;
            const double rise = std::max(1.0, std::min(c.l.y, c.r.y) - c.blob->y0);
            pts.push_back({c.l, rise});
            pts.push_back({c.r, rise});
        }
        // no drop with a reflection, and a sample edge across most of the frame: an opaque sample. Its surface (for
        // showing, and to hold a video's view) is the line through the edge without what stands on it.
        if (!anyFree) {
            const std::vector<int> top = substrateTop(im, lab, blobs);
            int have = 0;
            for (int t : top) have += t > 0;
            if (have > im.w / 2) {
                std::vector<double> ref;
                if (!straightEdge(top, &ref)) ref = edgeReference(top, int(0.3 * im.w));
                double sx = 0, sy = 0, sxx = 0, sxy = 0;
                int n = 0;
                for (int x = 0; x < im.w; x += 2)
                    if (top[std::size_t(x)] > 0 && std::abs(top[std::size_t(x)] - ref[std::size_t(x)]) < 3) {
                        sx += x, sy += ref[std::size_t(x)], sxx += double(x) * x, sxy += x * ref[std::size_t(x)], ++n;
                        if (n % 8 == 0) s.surfaceCorners.push_back({{double(x), ref[std::size_t(x)]}, 1.0});
                    }
                const double det = n * sxx - sx * sx;
                if (n > 20 && std::abs(det) > 1e-9) {
                    const double bb = (n * sxy - sx * sy) / det, aa = (sy - bb * sx) / n;
                    s.surface.p0 = {im.w / 2.0, aa + bb * im.w / 2.0};
                    s.surface.angle = std::atan(bb);
                    s.surface.found = true;
                    s.surface.reflection = false;
                    s.surfaceVotes = n;
                    pts.clear();
                }
            }
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
        if (bestScore < 0 && s.surface.found) {
            // the opaque sample's surface (above)
        } else if (bestScore < 0) {
            s.error = QStringLiteral("surface not found");
            return s;
        } else
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
            if (pass == 1)
                for (const Corner& c : pts)
                    if (std::abs(c.p.y - a - b * c.p.x) < kInlier) s.surfaceCorners.push_back({c.p, c.w});
        }
        if (bestScore >= 0) {
            s.surface.p0 = {im.w / 2.0, a + b * im.w / 2.0};
            s.surface.angle = std::atan(b);
            s.surface.found = true;
            s.surface.reflection = true;
        }
    }
    s.ok = true;

    // drops: blobs crossing the surface and rising above it
    struct Span {
        const Candidate* c;
        int x0, x1;
        double rise;
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
        spans.push_back({&c, x0, x1, rise});
    }
    // drops joined to an opaque substrate: where the band's top edge rises above the surface
    std::deque<Candidate> joined;   // (spans keep pointers)
    std::vector<Baseline> joinedBase;   // each one's own baseline
    if (!s.surface.reflection) {
        const std::vector<int> top = substrateTop(im, lab, blobs);
        // the sample's edge without what stands on it, in this frame (the sample may curl or swell as it soaks)
        std::vector<double> ref;
        if (!straightEdge(top, &ref)) ref = edgeReference(top, int(0.3 * im.w));
        if (qEnvironmentVariableIsSet("LIBREDROP_EDGE_DUMP")) {   // debugging: x, edge, reference
            if (FILE* f = std::fopen(qPrintable(qEnvironmentVariable("LIBREDROP_EDGE_DUMP")), "w")) {
                for (int k = 0; k < im.w; ++k) std::fprintf(f, "%d %d %.1f\n", k, top[std::size_t(k)], ref[std::size_t(k)]);
                std::fclose(f);
            }
        }
        int x = 0;
        while (x < im.w) {
            auto riseAt = [&](int c) { return top[std::size_t(c)] < 0 ? 0.0 : ref[std::size_t(c)] - top[std::size_t(c)]; };
            if (riseAt(x) <= 1.5) {
                ++x;
                continue;
            }
            int x0 = x;
            double rise = 0;
            int yTop = im.h;
            while (x < im.w && riseAt(x) > 1.5) rise = std::max(rise, riseAt(x)), yTop = std::min(yTop, top[std::size_t(x)]), ++x;
            int x1 = x - 1;
            // (the sample's own bumps and fibres are low: a drop must stand clearly up)
            if (rise < std::max(opt.minHeightPx, 0.02 * im.h) || x1 - x0 + 1 < std::max(opt.minWidthPx, 0.04 * im.w) || x0 == 0 ||
                x1 == im.w - 1)
                continue;
            const int id = lab[std::size_t(top[std::size_t((x0 + x1) / 2)]) * im.w + (x0 + x1) / 2];
            const Blob* bp = nullptr;
            for (const Blob& b : blobs)
                if (b.id == id) bp = &b;
            if (!bp) continue;
            if (qEnvironmentVariableIsSet("LIBREDROP_DEBUG"))
                qDebug("joined run %d..%d rise %.1f steep %d domed %d", x0, x1, rise, steepSide(top, ref, x0, x1), domed(top, ref, x0, x1));
            if (!steepSide(top, ref, x0, x1) || !domed(top, ref, x0, x1)) continue;   // a bulge or ridge of the sample
            int cx0, cx1;
            contactEnds(top, x0, x1, &cx0, &cx1);   // the drop itself, not the sample's crests beside it
            if (cx1 - cx0 + 1 < std::max(opt.minWidthPx, 0.02 * im.w)) continue;
            const Baseline lb = edgeBaseline(top, cx0, cx1);
            if (qEnvironmentVariableIsSet("LIBREDROP_DEBUG"))
                qDebug("  ends %d..%d -> contacts %d..%d, baseline y %.1f tilt %.2f", x0, x1, cx0, cx1, lb.p0.y, lb.angle * 180 / M_PI);
            x0 = cx0, x1 = cx1;
            joined.push_back({bp, yTop == 0, false, {}, {}});
            joinedBase.push_back(lb);
            spans.push_back({&joined.back(), x0, x1, rise});
        }
    }
    // a blob whose columns lie mostly within a bigger one's is part of that drop (its outline broke apart in this
    // frame) or a speck in front of it, not a drop of its own: it would split the drop's columns between them
    {
        std::vector<Span> kept;
        for (const Span& a : spans) {
            bool inside = false;
            for (const Span& b : spans) {
                if (&a == &b || (b.x1 - b.x0) * b.rise <= (a.x1 - a.x0) * a.rise) continue;
                const int ov = std::min(a.x1, b.x1) - std::max(a.x0, b.x0) + 1;
                inside |= ov > 0.5 * (a.x1 - a.x0 + 1);
            }
            if (!inside) kept.push_back(a);
        }
        spans.swap(kept);
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
        Baseline base = s.surface;   // a drop on an opaque sample: its own baseline
        bool joinedDrop = false;
        for (std::size_t k = 0; k < joined.size(); ++k)
            if (&joined[k] == sp.c) joinedDrop = true, base = joinedBase[k];
        DropOptions o = opt.drop;
        Baseline bl = base;
        bl.p0 = {bl.p0.x - lo, bl.p0.y};
        o.baseline = bl;
        // grey levels of this drop, so that the threshold does not depend on what else is in the picture (a
        // cropped copy, the dark stage, the background's brightness gradient): background = median of the bright
        // pixels beside and above the drop, dark = 5th percentile of the drop's own pixels
        {
            std::vector<float> bg, dk;
            const int yTop = std::max(0, int(lineY(base, d.xc) - 2.5 * sp.rise) - 10);
            for (int y = yTop; y < im.h; ++y)
                for (int x = lo; x <= hi; ++x) {
                    const double h = lineY(base, x) - y;
                    if (h < 3) continue;
                    const int l = lab[std::size_t(y) * im.w + x];
                    if (l == 0) bg.push_back(im.at(x, y));
                    else if (l == sp.c->blob->id) dk.push_back(im.at(x, y));
                }
            auto q = [](std::vector<float>& v, double f) {
                std::nth_element(v.begin(), v.begin() + long(f * double(v.size() - 1)), v.end());
                return double(v[std::size_t(f * double(v.size() - 1))]);
            };
            o.dark = dk.size() > 20 ? q(dk, 0.05) : s.dark;
            o.background = bg.size() > 100 ? q(bg, 0.5) : s.background;
        }
        // only this blob is dark in the cut-out: other drops, the needle elsewhere and specks are blanked, with a
        // 3 px margin (their grey fringe may be darker than this drop's own threshold)
        GrayImage cut = crop(im, lo, 0, hi - lo + 1, im.h);
        const int id = sp.c->blob->id, r = 3;
        for (int y = 0; y < cut.h; ++y)
            for (int x = 0; x < cut.w; ++x) {
                const int l = lab[std::size_t(y) * im.w + lo + x];
                if (l && l == id && joinedDrop && (lo + x < sp.x0 - 2 || lo + x > sp.x1 + 2) && y < lineY(base, lo + x) - 1) {
                    cut.at(x, y) = float(o.background);   // the substrate's fibres/dirt beside the drop
                    continue;
                }
                if (!l || l == id || y >= lineY(base, lo + x) + 2) continue;
                for (int yy = std::max(0, y - r); yy <= std::min(cut.h - 1, y + r); ++yy)
                    for (int xx = std::max(0, x - r); xx <= std::min(cut.w - 1, x + r); ++xx)
                        if (lab[std::size_t(yy) * im.w + lo + xx] != id && yy < lineY(base, lo + xx) + 2)
                            cut.at(xx, yy) = float(o.background);
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
