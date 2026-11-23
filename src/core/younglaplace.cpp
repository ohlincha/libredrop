#include "younglaplace.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace ld {

namespace {
using State = std::array<double, 4>;   // X, Z, phi, V

State deriv(const State& s, double bond) {
    const double X = s[0], Z = s[1], ph = s[2];
    const double sinOverX = X > 1e-9 ? std::sin(ph) / X : 1.0;   // limit at the apex (curvature 1)
    return {std::cos(ph), std::sin(ph), 2.0 + bond * Z - sinOverX, M_PI * X * X * std::sin(ph)};
}

State add(const State& a, const State& b, double h) {
    return {a[0] + h * b[0], a[1] + h * b[1], a[2] + h * b[2], a[3] + h * b[3]};
}
}  // namespace

Profile integrate(double bond, double phiMax, double ds, double sMax) {
    Profile p;
    // start just off the apex with the series solution (sphere of radius 1 to second order)
    const double s0 = 1e-4;
    State s = {s0, 0.5 * s0 * s0, s0, 0.0};
    double S = s0;
    auto push = [&](double Sv, const State& st) {
        p.S.push_back(Sv);
        p.X.push_back(st[0]);
        p.Z.push_back(st[1]);
        p.phi.push_back(st[2]);
        p.V.push_back(st[3]);
    };
    push(0.0, {0, 0, 0, 0});
    push(S, s);
    while (S < sMax) {
        const State k1 = deriv(s, bond);
        const State k2 = deriv(add(s, k1, ds / 2), bond);
        const State k3 = deriv(add(s, k2, ds / 2), bond);
        const State k4 = deriv(add(s, k3, ds), bond);
        State n;
        for (int i = 0; i < 4; ++i) n[i] = s[i] + ds / 6 * (k1[i] + 2 * k2[i] + 2 * k3[i] + k4[i]);
        if (n[2] >= phiMax) {                      // end exactly at phiMax (linear in the last step)
            const double f = (phiMax - s[2]) / (n[2] - s[2]);
            State e;
            for (int i = 0; i < 4; ++i) e[i] = s[i] + f * (n[i] - s[i]);
            push(S + f * ds, e);
            break;
        }
        if (n[0] < 0) break;                       // pendant drop pinched off
        s = n;
        S += ds;
        push(S, s);
    }
    return p;
}

bool Profile::atDepth(double z, double* x, double* ph, double* v) const {
    if (X.size() < 2 || z < 0 || z > Z.back()) return false;
    const auto it = std::lower_bound(Z.begin(), Z.end(), z);
    std::size_t i = std::size_t(it - Z.begin());
    if (i == 0) i = 1;
    const double f = Z[i] > Z[i - 1] ? (z - Z[i - 1]) / (Z[i] - Z[i - 1]) : 0.0;
    if (x) *x = X[i - 1] + f * (X[i] - X[i - 1]);
    if (ph) *ph = phi[i - 1] + f * (phi[i] - phi[i - 1]);
    if (v) *v = V[i - 1] + f * (V[i] - V[i - 1]);
    return true;
}

}  // namespace ld
