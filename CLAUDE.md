# LibreDrop

Contact angle and drop shape analysis for the lab's Ossila goniometer (any backlit side view works). Main use:
wetting of battery separators (cellulose/pulp, nylon) by water, organic carbonates and electrolytes, i.e. drops
that spread and soak in, so video and dynamic analysis come first. C++17, Qt 6 (Core, Gui), Eigen; same
toolchain as LibreRamen (`source ~/opt/libreramen-env.sh`, build dir `~/opt/build-libredrop`).

Read `knowledge/index.md` first. Hardware limits: no tilting stage; liquid is deposited by hand with a syringe.
The camera (UVC; recorded by the GUI's Measure window, src/core/camera.cpp V4L2, or `tools/goniometer_capture.py` = `libredrop-capture`) is on the Raman PC (Debian 13; reached via beryllium), not on this desktop: say when
something is untested on real images.

- Build/test: `cmake -S . -B ~/opt/build-libredrop && cmake --build ~/opt/build-libredrop && ctest --test-dir ~/opt/build-libredrop`
- `LibreDropCLI synth ...` makes test drops of known angle; `LibreDropCLI analyze IMAGE` prints all methods and checks;
  `scene`, `video`, `report` for whole frames, videos and saved tables (knowledge/video.md).
- GUI (`src/gui`, binary `LibreDrop`): check it without a display: `QT_QPA_PLATFORM=offscreen
  LIBREDROP_SHOT=out.png,SECONDS LibreDrop FILE` saves a screenshot after SECONDS and quits.
- Keep tests green; every method change is checked against synthetic drops (tests/test_drop.cpp).
