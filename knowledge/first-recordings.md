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
