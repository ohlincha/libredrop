# Contact-angle methods (src/core/drop.cpp)

Baseline frame: u along the baseline, v height above it. Outline: sub-pixel threshold crossings (half-way
between background and drop levels) along rows and columns; verified exact on a rendered sphere (mean radius
200.001 px for 200). Points within `minHeightPx` (2 px) of the baseline are not fitted.

Baseline: substrate edge in the outer 2–12 % columns on each side; per side the lowest edges (a drop lies
above the substrate), each side must be straight and nearly level; if only one side qualifies its line is
used with a warning.

| Method | Implementation | Verified on synthetic drops (Bond ≤ 0.5) |
|---|---|---|
| young-laplace | LM fit of apex (u0, height), apex radius, Bond number to the axisymmetric profile; **signed** point-to-profile distance | within 0.1° from 5° to 170°; Bond within 5 %, apex radius within 0.5 % |
| tangent | quadratic in a frame along the surface at the contact point (from the YL guess), two passes, left/right separately | within 0.5° for drops > ~300 px wide; small flat drops (30°, 27 px high) +1.5° |
| circle | geometric circle fit | exact without gravity; several degrees low when gravity flattens the drop |
| ellipse | direct least squares (Halíř–Flusser) | approximate; not recommended alone |
| height-width | θ = 2 atan(2h/D) | exact without gravity; robust for low angles |

Lessons: unsigned distances made the YL fit stall 0.3–0.8° short (kink at zero residual); fitting the
tangent as u(v) or v(u) is ill-conditioned at some angles, the surface-aligned frame is not.
