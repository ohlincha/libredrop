# First recordings (planned 2026-11-24, beryllium)

Goal: real footage for developing and testing the analysis, mainly battery separators
(cellulose/pulp, nylon) with water and organic carbonates.

Tool: `../ossila/goniometer_capture.py` (copy to beryllium). `--video SECONDS` records the MJPEG stream
without re-encoding (.mkv) plus a .json with settings, note and measured frame rate.

Setup: backlight on; separator taped flat on a glass slide; camera 1-2 degrees above the surface
(reflection visible); focus on the needle tip; one still of the needle for the scale (note the gauge).

| # | Recording | Options |
|---|---|---|
| 1 | water on bare glass (reference) | `--video 30` (1080p30) |
| 2 | water on each separator | `--video 20 --size 640x480 --fps 120` |
| 3 | same, slower phase | `--video 120` |
| 4 | DEC or DMC on each separator (no LiPF6: no HF) | `--video 10 --size 640x480 --fps 120` |
| 5 | one separator rotated 90 degrees (MD/CD) | as 2 |
| 6 | pendant drop, water and one carbonate | still |
| 7 | 2-3 drops in Ossila's software with its results noted | for comparison |

Three drops per condition; `--note "sample, side, direction, liquid, volume"`.

## Results 2026-11-24 (beryllium, water on glass slide, 1080p30)

Needle and stage are both fixed: drops are made by dispensing until the pendant drop touches the surface
(gap ~1.6 mm gives ~1.3-1.5 uL); sliding the slide sideways frees the drop from the needle.

| Video | Drop | Base | Height | Angle (cap) | Volume |
|---|---|---|---|---|---|
| water_glass_1_20261124_103100 | 1 | 2.93 mm | 0.37 mm | 28.5° (circle fit 28.8°) | 1.3 uL |
| water_glass_1_20261124_103100 | 2 | 3.49 mm | 0.32 mm | 20.7° (21.3°) | 1.5 uL |
| water_glass_2_20261124_103653 | final | 3.58 mm | 0.28 mm | 17.5° | 1.4 uL |

Quick script (silhouette threshold, widest row = contact line), not LibreDrop: LibreDrop's baseline search
expects a dark substrate on both sides, but here the glass top is a faint bright line and the drop's
reflection sits below it. Next in LibreDrop: baseline from the drop/reflection contact points (widest points of
the silhouette, where the outline has its corners). Videos stay on beryllium ~/goniometer (0.9 GB each).
