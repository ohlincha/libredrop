#include "drop.h"

#include <Eigen/Dense>
#include <algorithm>
#include <cmath>
#include <functional>
#include <numeric>

#include "scene.h"
#include "younglaplace.h"

namespace ld {

namespace {

constexpr double kDeg = 180.0 / M_PI;

double bilinear(const GrayImage& im, double x, double y) {
    x = std::clamp(x, 0.0, im.w - 1.001);
    y = std::clamp(y, 0.0, im.h - 1.001);
    const int x0 = int(x), y0 = int(y);
    const double fx = x - x0, fy = y - y0;
    return (1 - fx) * (1 - fy) * im.at(x0, y0) + fx * (1 - fy) * im.at(x0 + 1, y0) +
           (1 - fx) * fy * im.at(x0, y0 + 1) + fx * fy * im.at(x0 + 1, y0 + 1);
}

// Line through points (least squares y = a + b x), refitted without points more than 2 px off.
bool fitLine(const std::vector<Pt>& pts, double* a, double* b, double* rms) {
    std::vector<Pt> use = pts;
    for (int pass = 0; pass < 3; ++pass) {
        if (use.size() < 6) return false;
        double sx = 0, sy = 0, sxx = 0, sxy = 0;
        for (const Pt& p : use) sx += p.x, sy += p.y, sxx += p.x * p.x, sxy += p.x * p.y;
        const double n = double(use.size()), det = n * sxx - sx * sx;
        if (std::abs(det) < 1e-9) return false;
        *b = (n * sxy - sx * sy) / det;
        *a = (sy - *b * sx) / n;
        double ss = 0;
        for (const Pt& p : use) ss += std::pow(p.y - *a - *b * p.x, 2);
        *rms = std::sqrt(ss / n);
        std::vector<Pt> keep;
        for (const Pt& p : pts)
            if (std::abs(p.y - *a - *b * p.x) < std::max(2.0, 3 * *rms)) keep.push_back(p);
        if (keep.size() == use.size()) break;
        use = keep;
    }
    return true;
}

// Substrate edge in the outer columns: first bright→dark crossing from the top that stays dark below.
Baseline findBaseline(const GrayImage& im, double T) {
    Baseline bl;
    std::vector<Pt> pts;
    const int run = std::max(6, im.h / 40);
    auto column = [&](int x) {
        for (int y = 1; y < im.h - run; ++y) {
            if (im.at(x, y - 1) >= T && im.at(x, y) < T) {
                bool solid = true;
                for (int k = 1; k <= run && solid; ++k) solid = im.at(x, y + k) < T;
                if (!solid) continue;
                const double a = im.at(x, y - 1), c = im.at(x, y);
                pts.push_back({double(x), y - 1 + (a - T) / (a - c)});
                return;
            }
        }
    };
    for (int x = int(0.02 * im.w); x < int(0.12 * im.w); ++x) column(x);
    const std::size_t nLeft = pts.size();
    for (int x = int(0.88 * im.w); x < int(0.98 * im.w); ++x) column(x);
    // a drop (or anything else) in these columns lies above the substrate: keep the lowest edges of each side
    auto lowest = [](std::vector<Pt> side) {
        if (side.size() < 6) return std::vector<Pt>{};
        std::vector<double> ys;
        for (const Pt& p : side) ys.push_back(p.y);
        std::sort(ys.begin(), ys.end());
        const double ref = ys[std::size_t(0.9 * double(ys.size() - 1))];
        std::vector<Pt> keep;
        for (const Pt& p : side)
            if (std::abs(p.y - ref) < 5.0) keep.push_back(p);
        return keep;
    };
    const std::vector<Pt> L = lowest({pts.begin(), pts.begin() + long(nLeft)});
    const std::vector<Pt> R = lowest({pts.begin() + long(nLeft), pts.end()});
    // each side on its own must look like a substrate edge: straight and nearly level
    auto sideOk = [](const std::vector<Pt>& side, double* a, double* b) {
        double rms;
        return side.size() >= 6 && fitLine(side, a, b, &rms) && rms < 1.0 && std::abs(*b) < std::tan(5 * M_PI / 180);
    };
    double la = 0, lb = 0, ra = 0, rb = 0;
    const bool lOk = sideOk(L, &la, &lb), rOk = sideOk(R, &ra, &rb);
    double a, b, rms;
    if (lOk && rOk) {
        std::vector<Pt> both = L;
        both.insert(both.end(), R.begin(), R.end());
        if (!fitLine(both, &a, &b, &rms)) return bl;
        // both sides on one line (a tilted stage is fine; a step between them is not)
        if (rms > 1.5) return bl;
    } else if (lOk || rOk) {
        a = lOk ? la : ra, b = lOk ? lb : rb, rms = 0;
        bl.oneSide = true;
    } else {
        return bl;
    }
    bl.p0 = {im.w / 2.0, a + b * im.w / 2.0};
    bl.angle = std::atan(b);
    bl.rmsPx = rms;
    bl.found = true;
    return bl;
}

// Baseline through the reflection contact points of the largest dark blob that is not the substrate or stage.
Baseline findReflectionBaseline(const GrayImage& im, double T) {
    Baseline bl;
    std::vector<int> lab;
    const std::vector<Blob> blobs = labelDark(im, T, &lab);
    const Blob* best = nullptr;
    for (const Blob& c : blobs) {
        if (c.y1 >= im.h - 1 || (c.x0 == 0 && c.x1 == im.w - 1) || c.n < 50) continue;
        if (!best || c.n > best->n) best = &c;
    }
    Pt pl, pr;
    if (!best || !reflectionContacts(im, lab, *best, T, &pl, &pr)) return bl;
    if (!(pr.x > pl.x + 10)) return bl;
    const double b = (pr.y - pl.y) / (pr.x - pl.x);
    if (std::abs(b) > std::tan(5 * M_PI / 180)) return bl;
    bl.p0 = {0.5 * (pl.x + pr.x), 0.5 * (pl.y + pr.y)};
    bl.angle = std::atan(b);
    bl.found = true;
    bl.reflection = true;
    return bl;
}

// Levenberg–Marquardt with a forward-difference Jacobian.
Eigen::VectorXd levenbergMarquardt(const std::function<Eigen::VectorXd(const Eigen::VectorXd&)>& f,
                                   Eigen::VectorXd p, const Eigen::VectorXd& step, int maxIt = 60) {
    Eigen::VectorXd r = f(p);
    double cost = r.squaredNorm(), lambda = 1e-3;
    for (int it = 0; it < maxIt; ++it) {
        Eigen::MatrixXd J(r.size(), p.size());
        for (int k = 0; k < p.size(); ++k) {
            Eigen::VectorXd q = p;
            q[k] += step[k];
            J.col(k) = (f(q) - r) / step[k];
        }
        const Eigen::MatrixXd JtJ = J.transpose() * J;
        const Eigen::VectorXd g = J.transpose() * r;
        bool improved = false;
        for (int tries = 0; tries < 8 && !improved; ++tries) {
            Eigen::MatrixXd A = JtJ;
            A.diagonal() += lambda * JtJ.diagonal().cwiseMax(1e-12);
            const Eigen::VectorXd dp = A.ldlt().solve(-g);
            const Eigen::VectorXd pn = p + dp;
            const Eigen::VectorXd rn = f(pn);
            if (rn.squaredNorm() < cost) {
                const double rel = (cost - rn.squaredNorm()) / std::max(cost, 1e-30);
                p = pn, r = rn, cost = rn.squaredNorm();
                lambda = std::max(lambda / 4, 1e-9);
                improved = true;
                if (rel < 1e-10) return p;
            } else {
                lambda *= 8;
            }
        }
        if (!improved) break;
    }
    return p;
}

struct Circle {
    double uc = 0, vc = 0, R = 0, rms = 0;
    bool ok = false;
};

Circle fitCircle(const std::vector<Pt>& P) {
    Circle c;
    if (P.size() < 5) return c;
    Eigen::MatrixXd A(P.size(), 3);
    Eigen::VectorXd b(P.size());
    for (std::size_t i = 0; i < P.size(); ++i) {
        A(Eigen::Index(i), 0) = P[i].x, A(Eigen::Index(i), 1) = P[i].y, A(Eigen::Index(i), 2) = 1;
        b[Eigen::Index(i)] = -(P[i].x * P[i].x + P[i].y * P[i].y);
    }
    const Eigen::Vector3d s = A.colPivHouseholderQr().solve(b);
    Eigen::VectorXd p(3);
    p << -s[0] / 2, -s[1] / 2, 0;
    p[2] = std::sqrt(std::max(1e-9, p[0] * p[0] + p[1] * p[1] - s[2]));
    auto res = [&P](const Eigen::VectorXd& q) {
        Eigen::VectorXd r(P.size());
        for (std::size_t i = 0; i < P.size(); ++i) r[Eigen::Index(i)] = std::hypot(P[i].x - q[0], P[i].y - q[1]) - q[2];
        return r;
    };
    p = levenbergMarquardt(res, p, Eigen::Vector3d::Constant(1e-4 * std::max(1.0, p[2])), 30);
    c.uc = p[0], c.vc = p[1], c.R = p[2];
    c.rms = std::sqrt(res(p).squaredNorm() / double(P.size()));
    c.ok = std::isfinite(c.R) && c.R > 0;
    return c;
}

// Angle of the drop surface at a contact point from a tangent vector (tu, tv) pointing up along the surface.
double leftAngle(double tu, double tv) { return std::atan2(tv, tu) * kDeg; }
double rightAngle(double tu, double tv) { return std::atan2(tv, -tu) * kDeg; }

AngleResult circleMethod(const Circle& c) {
    AngleResult r;
    r.method = QStringLiteral("circle");
    if (!c.ok || std::abs(c.vc) >= c.R) {
        r.note = QStringLiteral("the circle does not cross the baseline");
        return r;
    }
    const double th = std::acos(std::clamp(-c.vc / c.R, -1.0, 1.0)) * kDeg;
    const double half = std::sqrt(c.R * c.R - c.vc * c.vc);
    r.left = r.right = th;
    r.contactL = {c.uc - half, 0};
    r.contactR = {c.uc + half, 0};
    r.rmsPx = c.rms;
    r.ok = true;
    return r;
}

// Direct least-squares ellipse (Fitzgibbon; Halíř–Flusser formulation) on normalised coordinates.
AngleResult ellipseMethod(const std::vector<Pt>& P) {
    AngleResult r;
    r.method = QStringLiteral("ellipse");
    if (P.size() < 8) return r;
    double mu = 0, mv = 0;
    for (const Pt& p : P) mu += p.x, mv += p.y;
    mu /= double(P.size()), mv /= double(P.size());
    double sc = 0;
    for (const Pt& p : P) sc = std::max({sc, std::abs(p.x - mu), std::abs(p.y - mv)});
    const Eigen::Index n = Eigen::Index(P.size());
    Eigen::MatrixXd D1(n, 3), D2(n, 3);
    for (Eigen::Index i = 0; i < n; ++i) {
        const double x = (P[std::size_t(i)].x - mu) / sc, y = (P[std::size_t(i)].y - mv) / sc;
        D1.row(i) << x * x, x * y, y * y;
        D2.row(i) << x, y, 1;
    }
    const Eigen::Matrix3d S1 = D1.transpose() * D1, S2 = D1.transpose() * D2, S3 = D2.transpose() * D2;
    const Eigen::Matrix3d T = -S3.inverse() * S2.transpose();
    Eigen::Matrix3d M = S1 + S2 * T;
    Eigen::Matrix3d C1inv;
    C1inv << 0, 0, 0.5, 0, -1, 0, 0.5, 0, 0;
    M = C1inv * M;
    Eigen::EigenSolver<Eigen::Matrix3d> es(M);
    Eigen::Vector3d a1;
    bool found = false;
    for (int k = 0; k < 3; ++k) {
        const Eigen::Vector3d v = es.eigenvectors().col(k).real();
        if (4 * v[0] * v[2] - v[1] * v[1] > 0) {
            a1 = v;
            found = true;
        }
    }
    if (!found) {
        r.note = QStringLiteral("no ellipse fits the outline");
        return r;
    }
    const Eigen::Vector3d a2 = T * a1;
    // conic in normalised coordinates: A x² + B xy + C y² + D x + E y + F = 0
    const double A = a1[0], B = a1[1], C = a1[2], D = a2[0], E = a2[1], F = a2[2];
    auto conic = [&](double u, double v) {
        const double x = (u - mu) / sc, y = (v - mv) / sc;
        return A * x * x + B * x * y + C * y * y + D * x + E * y + F;
    };
    auto grad = [&](double u, double v) {   // gradient in (u, v)
        const double x = (u - mu) / sc, y = (v - mv) / sc;
        return Pt{(2 * A * x + B * y + D) / sc, (B * x + 2 * C * y + E) / sc};
    };
    // intersections with v = 0
    const double y0 = -mv / sc;
    const double qa = A, qb = B * y0 + D, qc = C * y0 * y0 + E * y0 + F;
    const double disc = qb * qb - 4 * qa * qc;
    if (disc <= 0 || std::abs(qa) < 1e-15) {
        r.note = QStringLiteral("the ellipse does not cross the baseline");
        return r;
    }
    double x1 = (-qb - std::sqrt(disc)) / (2 * qa), x2 = (-qb + std::sqrt(disc)) / (2 * qa);
    if (x1 > x2) std::swap(x1, x2);
    const double uL = mu + sc * x1, uR = mu + sc * x2;
    auto angleAt = [&](double u, bool left) {
        const Pt g = grad(u, 0);
        double tu = -g.y, tv = g.x;              // tangent ⟂ gradient, pointing up
        if (tv < 0) tu = -tu, tv = -tv;
        return left ? leftAngle(tu, tv) : rightAngle(tu, tv);
    };
    r.left = angleAt(uL, true);
    r.right = angleAt(uR, false);
    r.contactL = {uL, 0};
    r.contactR = {uR, 0};
    double ss = 0;                               // Sampson distance
    for (const Pt& p : P) {
        const Pt g = grad(p.x, p.y);
        ss += std::pow(conic(p.x, p.y), 2) / std::max(1e-30, g.x * g.x + g.y * g.y);
    }
    r.rmsPx = std::sqrt(ss / double(P.size()));
    r.ok = true;
    return r;
}

// Quadratic fitted to the outline near each contact point, in a frame along the surface there (from a first
// guess: the Young–Laplace or circle result), so it is equally well conditioned at 5° and 170°. Two passes:
// the second uses the first pass's angle and contact point. Left and right are fitted separately.
AngleResult tangentMethod(const std::vector<Pt>& P, const AngleResult& guess, double height, double vmin) {
    AngleResult r;
    r.method = QStringLiteral("tangent");
    if (!guess.ok) return r;
    const double halfBase = 0.5 * (guess.contactR.x - guess.contactL.x);
    // long enough to rise well above the substrate (flat drops), at most half the drop
    const double thGuess = 0.5 * (guess.left + guess.right) / kDeg;
    const double zone = std::min(std::max(std::clamp(0.15 * (height + halfBase), 8.0, 60.0),
                                          (vmin + 8.0) / std::max(0.05, std::sin(thGuess))),
                                 0.6 * halfBase);
    double rss = 0;
    int nAll = 0;
    for (int side = 0; side < 2; ++side) {
        const double sgn = side == 0 ? 1 : -1;          // the right side is mirrored onto the left
        double th = (side == 0 ? guess.left : guess.right) / kDeg;
        double cu = sgn * (side == 0 ? guess.contactL.x : guess.contactR.x);
        for (int pass = 0; pass < 2; ++pass) {
            const double tu = std::cos(th), tv = std::sin(th), nu = -tv, nv = tu;
            std::vector<double> S, W;
            for (const Pt& p : P) {
                const double u = sgn * p.x - cu, v = p.y;
                if (v > vmin + 1.0 && std::hypot(u, v) < zone) {
                    S.push_back(u * tu + v * tv);
                    W.push_back(u * nu + v * nv);
                }
            }
            if (S.size() < 6) {
                r.note = QStringLiteral("too few outline points near the contact points");
                return r;
            }
            Eigen::MatrixXd A(S.size(), 3);
            Eigen::VectorXd b(S.size());
            for (std::size_t i = 0; i < S.size(); ++i) {
                A.row(Eigen::Index(i)) << 1, S[i], S[i] * S[i];
                b[Eigen::Index(i)] = W[i];
            }
            const Eigen::Vector3d c = A.colPivHouseholderQr().solve(b);
            // where the fitted curve meets the baseline: v(s) = s·sinθ + w(s)·cosθ = 0, the root nearest s = 0
            const double qa = c[2] * tu, qb = tv + c[1] * tu, qc = c[0] * tu;
            double s0;
            if (std::abs(qa) < 1e-12) {
                s0 = -qc / qb;
            } else {
                const double disc = qb * qb - 4 * qa * qc;
                if (disc < 0) {
                    r.note = QStringLiteral("the side fit does not reach the baseline");
                    return r;
                }
                const double q1 = (-qb - std::sqrt(disc)) / (2 * qa), q2 = (-qb + std::sqrt(disc)) / (2 * qa);
                s0 = std::abs(q1) < std::abs(q2) ? q1 : q2;
            }
            const double w0 = c[0] + c[1] * s0 + c[2] * s0 * s0;
            cu += s0 * tu + w0 * nu;
            th += std::atan(c[1] + 2 * c[2] * s0);
            if (pass == 1) {
                rss += (A * c - b).squaredNorm();
                nAll += int(S.size());
            }
        }
        if (side == 0) {
            r.left = th * kDeg;
            r.contactL = {cu, 0};
        } else {
            r.right = th * kDeg;
            r.contactR = {-cu, 0};
        }
    }
    r.rmsPx = std::sqrt(rss / std::max(1, nAll));
    r.ok = std::isfinite(r.left) && std::isfinite(r.right);
    return r;
}

// Signed distance from (x, z) to the profile polyline (dimensionless): negative inside the drop. A signed
// residual keeps the least-squares problem smooth where points cross the curve.
double profileDistance(const Profile& pr, double x, double z) {
    double best = 1e300, sign = 1;
    for (std::size_t i = 1; i < pr.X.size(); ++i) {
        const double ax = pr.X[i - 1], az = pr.Z[i - 1], bx = pr.X[i] - ax, bz = pr.Z[i] - az;
        const double L = bx * bx + bz * bz;
        const double t = L > 0 ? std::clamp(((x - ax) * bx + (z - az) * bz) / L, 0.0, 1.0) : 0.0;
        const double d = std::pow(x - ax - t * bx, 2) + std::pow(z - az - t * bz, 2);
        if (d < best) {
            best = d;
            sign = bx * (z - az) - bz * (x - ax) > 0 ? -1 : 1;   // left of the path from the apex = inside
        }
    }
    return sign * std::sqrt(best);
}

struct YLFit {
    AngleResult r;
    double b = 0, bond = 0, volume = 0;
};

YLFit youngLaplaceMethod(const std::vector<Pt>& Pall, const Circle& c, double bondPerPx2) {
    YLFit out;
    out.r.method = QStringLiteral("young-laplace");
    if (!c.ok) return out;
    std::vector<Pt> P;                           // at most 400 points, evenly spread
    const std::size_t stride = std::max<std::size_t>(1, Pall.size() / 400);
    for (std::size_t i = 0; i < Pall.size(); i += stride) P.push_back(Pall[i]);
    const bool fixed = bondPerPx2 > 0;
    auto bondOf = [&](const Eigen::VectorXd& q) { return fixed ? bondPerPx2 * q[2] * q[2] : q[3]; };
    auto res = [&](const Eigen::VectorXd& q) {
        const double u0 = q[0], H = q[1], b = std::max(1.0, q[2]);
        const Profile pr = integrate(bondOf(q), M_PI - 1e-3, 0.01);
        Eigen::VectorXd r(P.size());
        for (std::size_t i = 0; i < P.size(); ++i)
            r[Eigen::Index(i)] = b * profileDistance(pr, std::abs(P[i].x - u0) / b, (H - P[i].y) / b);
        return r;
    };
    Eigen::VectorXd p(fixed ? 3 : 4);
    p[0] = c.uc, p[1] = c.vc + c.R, p[2] = c.R;
    if (!fixed) p[3] = 0.05;
    Eigen::VectorXd step = Eigen::VectorXd::Constant(p.size(), 1e-3 * c.R);
    if (!fixed) step[3] = 1e-3;
    p = levenbergMarquardt(res, p, step, 60);
    const double b = p[2], H = p[1], bond = bondOf(p);
    const Profile pr = integrate(bond, M_PI - 1e-4, 0.002);
    double x, ph, v;
    if (!pr.atDepth(H / b, &x, &ph, &v)) {
        out.r.note = QStringLiteral("the fitted shape does not reach the baseline");
        return out;
    }
    out.r.left = out.r.right = ph * kDeg;
    out.r.contactL = {p[0] - x * b, 0};
    out.r.contactR = {p[0] + x * b, 0};
    out.r.rmsPx = std::sqrt(res(p).squaredNorm() / double(P.size()));
    out.r.ok = b > 0 && std::isfinite(ph);
    out.b = b;
    out.bond = bond;
    out.volume = v * b * b * b;
    return out;
}

void addCheck(DropAnalysis& a, Check::Level l, const QString& id, const QString& msg) { a.checks.push_back({l, id, msg}); }

}  // namespace

Pt Baseline::toUv(Pt p) const {
    const double c = std::cos(angle), s = std::sin(angle), dx = p.x - p0.x, dy = p.y - p0.y;
    return {dx * c + dy * s, dx * s - dy * c};
}

Pt Baseline::toImage(Pt q) const {
    const double c = std::cos(angle), s = std::sin(angle);
    return {p0.x + q.x * c + q.y * s, p0.y + q.x * s - q.y * c};
}

const AngleResult* DropAnalysis::method(const QString& name) const {
    for (const AngleResult& m : methods)
        if (m.method == name) return &m;
    return nullptr;
}

bool DropAnalysis::hasCheck(const QString& id) const {
    return std::any_of(checks.begin(), checks.end(), [&id](const Check& c) { return c.id == id; });
}

DropAnalysis analyzeDrop(const GrayImage& im, const DropOptions& opt) {
    DropAnalysis a;
    if (im.w < 50 || im.h < 50) {
        a.error = QStringLiteral("image too small");
        return a;
    }
    a.dark = opt.dark >= 0 ? opt.dark : percentile(im, 0.02);
    a.background = opt.background >= 0 ? opt.background : percentile(im, 0.98);
    a.threshold = 0.5 * (a.dark + a.background);
    if (a.background < 60) {
        a.error = QStringLiteral("image too dark");
        addCheck(a, Check::Error, QStringLiteral("backlight"),
                 QStringLiteral("The image is dark (background %1 of 255): switch the backlight on, or lengthen the exposure.")
                     .arg(a.background, 0, 'f', 0));
        return a;
    }
    if (a.background - a.dark < 60)
        addCheck(a, Check::Warning, QStringLiteral("contrast"),
                 QStringLiteral("Low contrast between drop and background (%1 levels): check the backlight and exposure.")
                     .arg(a.background - a.dark, 0, 'f', 0));
    const double T = a.threshold;

    a.baseline = opt.baseline ? *opt.baseline : findBaseline(im, T);
    if (!opt.baseline && (!a.baseline.found || a.baseline.oneSide)) {
        const Baseline rb = findReflectionBaseline(im, T);
        if (rb.found) a.baseline = rb;
    }
    if (!a.baseline.found) {
        a.error = QStringLiteral("baseline not found");
        addCheck(a, Check::Error, QStringLiteral("baseline"),
                 QStringLiteral("No substrate edge at the sides of the image and no drop reflection: the substrate must be "
                                "visible left and right of the drop, or its surface must reflect the drop (or set the "
                                "baseline by hand)."));
        return a;
    }
    const Baseline& bl = a.baseline;
    auto baseY = [&](double x) { return bl.p0.y + std::tan(bl.angle) * (x - bl.p0.x); };
    auto isDark = [&](int x, int y) { return im.at(x, y) < T && y < baseY(x) - 1.0; };   // above the substrate

    // drop columns: dark pixels above the baseline
    std::vector<int> count(std::size_t(im.w), 0);
    for (int x = 0; x < im.w; ++x)
        for (int y = 0; y < std::min(im.h, int(baseY(x)) - 1); ++y) count[std::size_t(x)] += im.at(x, y) < T;
    const int xMax = int(std::max_element(count.begin(), count.end()) - count.begin());
    if (count[std::size_t(xMax)] < 3) {
        a.error = QStringLiteral("no drop");
        addCheck(a, Check::Error, QStringLiteral("nodrop"), QStringLiteral("No drop found on the baseline."));
        return a;
    }
    int x0 = xMax, x1 = xMax;
    while (x0 > 0 && count[std::size_t(x0 - 1)] >= 2) --x0;
    while (x1 < im.w - 1 && count[std::size_t(x1 + 1)] >= 2) ++x1;
    if (x0 == 0 || x1 == im.w - 1)
        addCheck(a, Check::Error, QStringLiteral("border"),
                 QStringLiteral("The drop reaches the edge of the image: move the sample or use a smaller drop."));
    const int xc = (x0 + x1) / 2;

    // needle: a dark run at the top of the image above the drop
    int needleL = -1, needleR = -1;
    if (im.at(xc, 0) < T || im.at(xMax, 0) < T) {
        int s = im.at(xc, 0) < T ? xc : xMax;
        needleL = needleR = s;
        while (needleL > 0 && im.at(needleL - 1, 0) < T) --needleL;
        while (needleR < im.w - 1 && im.at(needleR + 1, 0) < T) ++needleR;
        a.needle = true;
        a.needleWidthPx = needleR - needleL + 1;
    }

    // rows: left and right ends of the dark run through the drop centre
    struct Row {
        double y, l, r;
    };
    std::vector<Row> rows;
    const int yBottom = std::min(im.h - 1, int(std::floor(std::max(baseY(x0), baseY(x1)))));
    int col = xc;
    bool started = false;
    for (int y = yBottom; y >= 0; --y) {
        if (!isDark(col, y)) {
            // near the apex (or below the baseline on the high side of a tilted one) look for the run nearby
            int found = -1;
            for (int d = 1; d <= (x1 - x0) / 2 && found < 0; ++d) {
                if (col - d >= 0 && isDark(col - d, y)) found = col - d;
                else if (col + d < im.w && isDark(col + d, y)) found = col + d;
            }
            if (found < 0) {
                if (started) break;
                continue;
            }
            col = found;
        }
        started = true;
        int l = col, r = col;
        while (l > 0 && isDark(l - 1, y)) --l;
        while (r < im.w - 1 && isDark(r + 1, y)) ++r;
        // light passing through the drop leaves bright patches inside it: the outline is the outermost dark
        // pixel of the drop's columns in this row, not the end of the run through the centre
        for (int x = std::max(1, x0 - 1); x < l; ++x)
            if (isDark(x, y)) {
                l = x;
                break;
            }
        for (int x = std::min(im.w - 2, x1 + 1); x > r; --x)
            if (isDark(x, y)) {
                r = x;
                break;
            }
        if (l == 0 || r == im.w - 1) continue;
        // a side is an edge only where the run ends at background (not where it meets the substrate)
        const double al = im.at(l - 1, y), cl = im.at(l, y), ar = im.at(r + 1, y), cr = im.at(r, y);
        Row row{double(y), al >= T ? l - 1 + (al - T) / (al - cl) : kNaN, ar >= T ? r + (cr - T) / (cr - ar) : kNaN};
        rows.push_back(row);
        col = (l + r) / 2;
    }
    if (a.needle) {   // touching: the needle's centre column is dark all the way down to the substrate
        const int nx = (needleL + needleR) / 2;
        a.needleTouches = true;
        for (int y = 0; y < int(baseY(nx)) - 1 && a.needleTouches; ++y) a.needleTouches = im.at(nx, y) < T;
    }
    if (a.needleTouches && !rows.empty()) {   // leave out the needle: runs as narrow as it, above the widest row
        std::size_t widest = 0;
        for (std::size_t i = 0; i < rows.size(); ++i)
            if (std::isfinite(rows[i].l) && std::isfinite(rows[i].r) &&
                (!std::isfinite(rows[widest].l) || rows[i].r - rows[i].l > rows[widest].r - rows[widest].l))
                widest = i;
        for (std::size_t i = widest; i < rows.size(); ++i)
            if (rows[i].r - rows[i].l < 1.6 * a.needleWidthPx) {
                rows.resize(i);
                break;
            }
    }
    for (const Row& r : rows) {
        if (std::isfinite(r.l)) a.outline.push_back({r.l, r.y});
        if (std::isfinite(r.r)) a.outline.push_back({r.r, r.y});
    }
    // columns: top edge of the drop (from above, below the needle)
    for (int x = x0; x <= x1; ++x) {
        if (a.needle && x >= needleL - 3 && x <= needleR + 3) continue;
        for (int y = 1; y < int(baseY(x)) - 1; ++y)
            if (im.at(x, y - 1) >= T && im.at(x, y) < T) {
                const double yy = y - 1 + (im.at(x, y - 1) - T) / (im.at(x, y - 1) - im.at(x, y));
                if (yy < baseY(x) - 1.5) a.outline.push_back({double(x), yy});
                break;
            }
    }

    // baseline frame
    std::vector<Pt> P;
    double vmax = 0;
    for (const Pt& p : a.outline) {
        const Pt q = bl.toUv(p);
        vmax = std::max(vmax, q.y);
        if (q.y > opt.minHeightPx) P.push_back(q);
    }
    a.heightPx = vmax;
    if (P.size() < 20 || vmax < 4) {
        a.error = QStringLiteral("drop outline too small");
        addCheck(a, Check::Error, QStringLiteral("small"),
                 QStringLiteral("The drop is too small or too flat to measure (height %1 px).").arg(vmax, 0, 'f', 1));
        return a;
    }
    std::sort(P.begin(), P.end(), [](Pt p, Pt q) { return std::atan2(p.y, p.x) < std::atan2(q.y, q.x); });

    const Circle circle = fitCircle(P);
    const AngleResult circ = circleMethod(circle);
    const AngleResult ell = ellipseMethod(P);
    double bondPerPx2 = 0;
    if (opt.pxPerMm > 0 && opt.densityDiff > 0 && opt.surfaceTension > 0) {
        const double m = 1e-3 / opt.pxPerMm;     // metres per pixel
        bondPerPx2 = opt.densityDiff * 9.81 * m * m / (opt.surfaceTension * 1e-3);
    }
    const YLFit yl = youngLaplaceMethod(P, circle, bondPerPx2);
    const AngleResult tang = tangentMethod(P, yl.r.ok ? yl.r : circ, vmax, opt.minHeightPx);

    AngleResult hw;
    hw.method = QStringLiteral("height-width");
    const AngleResult& base = tang.ok ? tang : circ;
    if (base.ok) {
        const double D = base.contactR.x - base.contactL.x;
        a.baseWidthPx = D;
        if (D > 0) {
            hw.left = hw.right = 2 * std::atan(2 * vmax / D) * kDeg;
            hw.contactL = base.contactL;
            hw.contactR = base.contactR;
            hw.ok = true;
        }
    }
    a.methods = {yl.r, tang, circ, ell, hw};
    a.apexRadiusPx = yl.r.ok ? yl.b : circle.R;
    a.bond = yl.bond;
    a.volumePx3 = yl.volume;
    a.ok = yl.r.ok || tang.ok || circ.ok;

    // edge width (10–90 %) along the circle's normals, on the upper half of the outline
    if (circle.ok) {
        std::vector<double> widths;
        const double lo = a.dark + 0.1 * (a.background - a.dark), hi = a.dark + 0.9 * (a.background - a.dark);
        for (std::size_t i = 0; i < P.size(); i += std::max<std::size_t>(1, P.size() / 40)) {
            if (P[i].y < 0.3 * vmax) continue;
            const double nu = P[i].x - circle.uc, nv = P[i].y - circle.vc, L = std::hypot(nu, nv);
            double tLo = kNaN, tHi = kNaN, prev = kNaN;
            for (double t = -8; t <= 8; t += 0.1) {
                const Pt q = bl.toImage({P[i].x + t * nu / L, P[i].y + t * nv / L});
                const double v = bilinear(im, q.x, q.y);
                if (std::isfinite(prev)) {
                    if (prev < lo && v >= lo && !std::isfinite(tLo)) tLo = t;
                    if (prev < hi && v >= hi) tHi = t;
                }
                prev = v;
            }
            if (std::isfinite(tLo) && std::isfinite(tHi) && tHi > tLo) widths.push_back(tHi - tLo);
        }
        if (!widths.empty()) {
            std::nth_element(widths.begin(), widths.begin() + long(widths.size() / 2), widths.end());
            a.edgeWidthPx = widths[widths.size() / 2];
        }
    }

    // checks
    if (bl.oneSide)
        addCheck(a, Check::Warning, QStringLiteral("baseline-side"),
                 QStringLiteral("The substrate is visible on one side of the drop only: the baseline comes from that side. "
                                "Centre the drop for a reliable baseline."));
    const double tiltDeg = bl.angle * kDeg;
    if (std::abs(tiltDeg) > 0.5)
        addCheck(a, Check::Warning, QStringLiteral("tilt"),
                 QStringLiteral("The baseline is tilted by %1°: level the stage or the camera (the drop may run downhill).")
                     .arg(tiltDeg, 0, 'f', 1));
    if (bl.rmsPx > 1.0)
        addCheck(a, Check::Warning, QStringLiteral("flat"),
                 QStringLiteral("The substrate edge is not straight (%1 px scatter): tape the sample flat on a glass slide.")
                     .arg(bl.rmsPx, 0, 'f', 1));
    if (a.needle && a.needleTouches && opt.staticMeasurement)
        addCheck(a, Check::Warning, QStringLiteral("needle"),
                 QStringLiteral("The needle touches the drop: for a static angle, withdraw the needle first."));
    if (a.edgeWidthPx > 3.0)
        addCheck(a, Check::Warning, QStringLiteral("focus"),
                 QStringLiteral("The drop's edge is blurred (%1 px): focus on the drop.").arg(a.edgeWidthPx, 0, 'f', 1));
    if (a.baseWidthPx > 0 && a.baseWidthPx < 150)
        addCheck(a, Check::Warning, QStringLiteral("resolution"),
                 QStringLiteral("The drop is only %1 px wide: zoom in or use a larger drop for accurate angles.")
                     .arg(a.baseWidthPx, 0, 'f', 0));
    if (tang.ok && std::abs(tang.left - tang.right) > 3)
        addCheck(a, Check::Warning, QStringLiteral("asymmetry"),
                 QStringLiteral("Left and right angles differ by %1°: tilt, an uneven surface, or a pinned contact line.")
                     .arg(std::abs(tang.left - tang.right), 0, 'f', 1));
    if (circ.ok && yl.r.ok && std::abs(circ.left - yl.r.left) > 2)
        addCheck(a, Check::Info, QStringLiteral("gravity"),
                 QStringLiteral("Gravity flattens this drop (circle and Young–Laplace differ by %1°): use the Young–Laplace "
                                "result, or a smaller drop.")
                     .arg(std::abs(circ.left - yl.r.left), 0, 'f', 1));
    if (yl.r.ok && yl.r.left < 10)
        addCheck(a, Check::Info, QStringLiteral("lowangle"),
                 QStringLiteral("Below 10° the height–width method is the most robust; small errors in the baseline matter more."));
    return a;
}

}  // namespace ld
