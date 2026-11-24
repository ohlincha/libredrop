# <img src="docs/icon.png" width="48" align="center"> LibreDrop

Open contact-angle and drop-shape analysis for backlit side views (developed for an Ossila contact angle
goniometer). The main application is the wetting of battery separators by water, organic carbonates and
electrolytes: porous, absorbing samples where the drop spreads and soaks in, so dynamic analysis from video
comes first.

**Status: early development.** The analysis works on synthetic drops of known angle and on the first real
recordings from an Ossila goniometer (water on glass and tape, 17–90°), whole frames and whole videos, from the
command line. No graphical program yet; not yet tried on absorbing samples (separators).

- Contact angle by Young–Laplace fit (with gravity; also gives the Bond number), tangent (left and right
  separately), circle, ellipse and height–width methods.
- Baseline from a dark substrate at the image sides, or, on a bright glass/film surface, from the corners where
  the drop meets its reflection.
- Checks on every image: backlight, focus, stage tilt, flat sample, needle in the drop, drop at the image edge,
  resolution, gravity flattening, low angles.
- Whole frames, no hand-set baseline: the surface is found from the drops' reflections, every drop on it is
  measured, and the needle, hanging drops, specks and lines on the sample are left out.
- Video: every frame (with the file's own timestamps), each drop followed from the moment it touches the surface,
  also when the slide is pushed sideways. Per drop: angle (left/right, all methods), base width, height, volume,
  whether the needle is still in it, and the checks; a summary (angle, base and volume at release and at the end,
  rates of change) and a plot.
- `LibreDropCLI analyze IMAGE` (one drop), `scene IMAGE` (whole frame), `video FILE -o table.csv --plot plot.png`
  (needs ffmpeg), `synth` (test drops).

Planned: graphical program with live camera, absorption models for separators, camera control, pendant-drop surface tension, surface free energy for non-absorbing samples.

Build: C++17, Qt 6 (Core, Gui), Eigen 3.4+, CMake 3.21+.

    cmake -S . -B build && cmake --build build && ctest --test-dir build

MIT licence.
