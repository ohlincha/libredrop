# <img src="docs/icon.png" width="48" align="center"> LibreDrop

Open contact-angle and drop-shape analysis for backlit side views (developed for an Ossila contact angle
goniometer). The main application is the wetting of battery separators by water, organic carbonates and
electrolytes: porous, absorbing samples where the drop spreads and soaks in, so dynamic analysis from video
comes first.

**Status: early development.** The analysis works on synthetic drops of known angle and on the first real
recordings from an Ossila goniometer (water on glass and tape, 17–90°), whole frames and whole videos, from the
command line and in the graphical program, which also records from the camera (Linux, Windows, macOS). Not yet tried on
absorbing samples (separators).

**User manual**: [docs/MANUAL.md](docs/MANUAL.md), printable as [docs/LibreDrop-Manual.pdf](docs/LibreDrop-Manual.pdf)
(made by `tools/manual_pdf.sh`), and in the program under *Help ▸ Manual* (F1).

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
  (needs ffmpeg; `--drops 2,3` keeps only those drops), `report table.csv --drops 2,3 --plot plot.png` (summary and
  plot again from a saved table, e.g. to leave out a stain), `synth` (test drops).

- `LibreDrop [FILE]`: the graphical program. Open (or drop) a video or image; it is analysed in the background.
  Step through the frames with the surface, each drop's outline, contact points, tangents and angle drawn on
  them; tick the drops that count; angle, base width and volume against time with a cursor (click the plot to
  jump there); export the table and the plot. Scale and liquid (water, DMC, DEC, PC or custom) are remembered.

## Measuring (live camera)

*File ▸ Measure with the camera…* (Ctrl+M, or **Measure…** beside Analyse) opens the live view: the camera with
the surface, each drop's outline and its angle found in every shown frame, so the stage, focus and light can be
set before a drop goes on. Fill in sample, side, direction, liquid and volume; press **Record** (Ctrl+R) as the drop
touches (the video always starts 2 s earlier, so the contact is in it even if you press late) and **Stop** when it has settled or soaked
in, or set a duration. The video (the camera's own JPEG frames with their capture times, not re-encoded) and its
`.json` go into the open project's `recordings/` (or a chosen folder), are filed under the sample, and are opened
and analysed.

- The goniometer camera gets Ossila's settings (exposure 3.9 ms, gain 0, gamma 72, …); change the exposure if the
  image is too dark. The window warns about a black image (backlight off, lens cap on).
- Sizes and rates as the camera offers them. The goniometer camera says 120 fps at 640×480 but delivers about 99;
  the true frame times are recorded, so analysis is not affected.
- Linux: V4L2, with the camera's settings set by LibreDrop. Windows (DirectShow) and macOS (AVFoundation): through
  the bundled ffmpeg; Motion-JPEG is copied unchanged on Windows, re-encoded on macOS. The camera keeps its own
  settings there (Windows: *Camera settings…* opens the driver's dialog). Not yet tried with the goniometer camera
  on Windows or macOS.
- `LibreDropCLI cameras` lists cameras, sizes and settings; `LibreDropCLI record OUT.mkv --size 640x480 --seconds 10
  --sample kraft-A --liquid DMC` records without the window.

## Simulator

Without the instrument, or to see what the analysis does with a known answer: the Measure window's camera list has
**Simulator** scenarios, drawn frame by frame with Young–Laplace drop shapes: a needle brings a hanging drop down,
it touches, spreads, soaks in (volume falling as √t with the contact line pinned) and disappears.

| Scenario | Substrate |
|---|---|
| `glass` | reflecting slide: the drop's mirror image below the surface |
| `separator` | flat opaque sheet; the drop soaks in |
| `wavy-paper` | tilted, wavy sheet with fibres along its edge |
| `curled-postit` | curled sheet with a raised ridge, a step, a lifted edge, fibres, swelling under the wet drop (like the post-it of 2026-11-25) |
| `hydrophobic` | flat film, water beads up (~135°) |

`LibreDropCLI simulate SCENARIO -o out.mkv [--seconds S] [--fps F]` writes a recording with its true values
(`out_truth.csv`: angle, base, height, volume, contact points per frame); analyse it and compare. The tests do
this for every scenario (tests/test_simulate.cpp). Drawing a 1080p frame takes about 0.2 s, so the simulated live
view may run slower than real time on a busy machine (its recorded times are the simulation's); the half-size mode (960×540) keeps up with 30 fps.

## Measuring separators (soaking)

1. **Set up** (once per camera mode): the scale, and the liquid (Measurement ▸ Liquid).
2. **Glass first** (once per liquid and day): record a drop of the same liquid on a clean glass slide, about as
   long as the separator recordings. Open it, analyse, then Soaking tab ▸ **Use as glass reference**. This sets
   the evaporation correction for that liquid (or enter a known rate under Measurement ▸ Soaking). The Soaking tab
   says when it is missing.
3. **Separators**: record each sample, 3 drops or more; both sides and both directions (MD/CD) if they may differ.
   Use 640×480 at 120 fps when the liquid soaks in within a second or two.
4. **Read** the Soaking tab: the angle at fixed times after contact (0.1, 1, 10 s by default; set under
   Measurement), the **sorptivity** S (uptake per contact area: absorbed volume = S · area · √t, in mm/√s, fitted
   after release with evaporation added back; R² shows how well the drop follows it) and when the drop was
   absorbed. Untick drops that are not usable. On glass, S should come out ≈ 0.

5. **Compare** (File ▸ Compare samples…): add all recordings of all samples, Analyse. Recordings with the same
   sample name (from the file name without date/time and repeat number; double-click to change) are averaged:
   mean ± standard deviation of the ticked drops; Export CSV gives each drop and the per-sample means and SDs.

## Projects (library)

*Project ▸ New project…* makes a folder for a study: `project.json` (samples, recordings, liquids with literature
sources, evaporation references per liquid and date, calibrations), `recordings/`, `results/` (cached) and
`conditions.csv` (room temperature and humidity from a TEMPer/TEMPerHUM USB sensor, if attached; names and offsets
from USB Thermo Logger). The Library panel lists samples and recordings; *Add…* files recordings by the capture
tool's `.json`; opening a recording sets its liquid, scale and that day's evaporation correction; *Compare* averages
the selected samples. *Project ▸ Liquids…* and *Calibration…* edit the tables (scale from the needle's measured
diameter). One user at a time; copy the folder to back it up or share it.

Recording from a terminal (the older capture script): `libredrop-capture --video 20 --size 640x480 --fps 120 --sample kraft-A --side front
--direction MD --liquid DMC --volume 2` (installed with the .deb; needs python3-opencv, ffmpeg, v4l-utils).

S is proportional to the image scale: compare samples measured at the same scale. Not yet tried on real
separators.

Command line: `LibreDropCLI video FILE --scale 94.23 --at 0.1,1,10 --evaporation 0.018` prints the same values;
`LibreDropCLI pendant FILE --scale … --bottom ROW --from T0 --to T1` gives the surface tension from the hanging drop
before contact (only reliable for large drops and a verified scale).

Planned: live camera and recording in the program, camera control, surface tension in the program, surface free
energy for non-absorbing samples.

Install: Debian 13 / Ubuntu 24.04 `.deb`, Windows installer, macOS `.dmg` (Intel and Apple Silicon) from the
releases page. ffmpeg comes with it (a dependency of the .deb; inside the Windows installer and the macOS app:
LGPL build from BtbN/FFmpeg-Builds on Windows, static builds from ffmpeg.martin-riedl.de on macOS, each with its
license; sources at ffmpeg.org). It drives the camera on Windows and macOS and reads video formats other than
Motion-JPEG. File ▸ Save compact copy keeps the band around the drops in grey, 5–15× smaller. Help ▸ Open
example (or `LibreDrop --example`) shows a built-in recording, so no goniometer is needed to try it.

Build: C++17, Qt 6 (Core, Gui, Widgets, Concurrent), Eigen 3.4+, CMake 3.21+.

    cmake -S . -B build && cmake --build build && ctest --test-dir build

MIT licence.
