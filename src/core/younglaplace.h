#pragma once
// Axisymmetric Young–Laplace drop profile (sessile or pendant), dimensionless with the apex radius b = 1.
//
// Coordinates: X radial, Z from the apex into the drop (towards the substrate for a sessile drop),
// phi the angle of the tangent; arc length S. With the Bond number beta = Δρ g b² / γ:
//     dX/dS = cos φ,  dZ/dS = sin φ,  dφ/dS = 2 + β Z − sin φ / X,   dV/dS = π X² sin φ
// β > 0: sessile drop (gravity flattens it), β < 0: pendant drop, β = 0: sphere.
// A sessile drop with contact angle θ is the profile from the apex up to φ = θ.
#include <vector>

namespace ld {

struct Profile {
    std::vector<double> S, X, Z, phi, V;   // V: volume of the drop above depth Z (dimensionless, b³)
    bool empty() const { return X.empty(); }
    // Linear interpolation at depth z (Z increases monotonically while 0 < φ < π). Returns false beyond the end.
    bool atDepth(double z, double* x, double* ph = nullptr, double* v = nullptr) const;
};

// Integrates from the apex until φ reaches phiMax (radians, ≤ π) or S reaches sMax. Step ds (RK4).
Profile integrate(double bond, double phiMax, double ds = 0.002, double sMax = 40.0);

// Sessile drop of contact angle theta (radians): the profile up to φ = θ.
inline Profile sessile(double bond, double theta, double ds = 0.002) { return integrate(bond, theta, ds); }

}  // namespace ld
