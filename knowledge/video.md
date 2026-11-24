---
type: Design
title: Whole-frame and video analysis
description: How LibreDrop finds the surface and the drops in a whole frame and follows them through a video; results on the first recordings.
tags: [video, tracking, baseline]
timestamp: 2026-11-24
---
# Whole-frame and video analysis

Aim: better than the vendor (Ossila) software, which analyses recorded video frame by frame but needs the
baseline set by hand. Here nothing is set by hand.

## How (src/core/scene.*, video.*, report.*)

1. Dark blobs at one threshold (half-way between the 2nd and 98th grey-level percentiles). The stage (touching
   the bottom or both sides) and blobs under 150 px are dropped.
2. Surface: each blob that stands on a reflecting surface has two corners where drop and mirror image meet. The
   line through two corners that most corners agree with (2.5 px, weighted by blob height) wins, then a weighted
   refit. Blobs on the needle count only if nothing else has corners (a hanging drop has "corners" too).
3. Drops: blobs touching the surface, rising >= 4 px and >= 40 px wide. Each is cut out (its columns, at most
   half-way to the neighbours), other blobs blanked, and measured by analyzeDrop with the surface as a fixed
   baseline and the frame's grey levels (a cut-out's own levels let the needle's grey edge count as dark).
4. Video: Motion-JPEG (the camera's own format; Matroska or AVI) is read directly (src/core/mjpeg.*: container
   index + Qt's JPEG decoder, frame times from the file); other formats through ffmpeg (packet times from ffprobe,
   frames piped as raw grey). Frames are decoded and measured in parallel batches (one per core). Pass 1 takes the median surface of ~40
   frames; pass 2 measures every frame with it. Tracking by position and width; when the slide is pushed, all
   drops move together, so the common shift is found first. Tracks under 0.5 s or with no usable frame are
   dropped. A point is usable if measured and without error checks (e.g. cut off at the image edge).

## Grey levels and cropping (2026-11-24)

Thresholds are per drop: half-way between the median of the bright pixels beside/above it and the 5th percentile
of its own pixels; the reflection corners use each blob's own threshold too; blobs cut off at the image's side do
not vote on the surface; the video surface is a weighted fit through all sampled frames' corner points. Before,
one threshold for the whole frame made volumes depend on what else was in the picture (cropping away the dark
stage changed the volume by up to 9%). Now original vs compact copies (crop + grey + JPEG 60-75) agree within
1% in volume and 0.1 deg in angle (14 s of water_glass_2). Volumes are ~5% lower than with the old global
threshold (glass_1: 1.59 -> 1.27 uL instead of 1.68 -> 1.38); the per-drop threshold is the better definition,
but there is no independent volume reference yet (a known volume from a calibrated syringe would settle it).

Near 90 deg the reflection corner is a vertical stretch (wall + mirror wall) or two bulges with a shallow neck
(< 3 px): the contact is taken as the middle of the stretch or the neck between the bulges, not the row that
happens to reach out furthest (that flipped between bulges with tiny threshold changes).

Compact copy (File > Save compact copy, `LibreDropCLI compact`): rows around the surface (from the analysis),
grey, JPEG, Matroska with the original frame times. 100 MB -> 6.7 MB (q60) / 10.3 MB (q75) for 14 s at 1080p30.
Synthetic test drops have 0.8 px edges, where JPEG q80 already moves a 40 deg drop by 3 deg; real edges are 4-8
px. Default quality 85.

## Results on the first videos (2026-11-24, 1080p30, cropped to rows 300-900, 94.23 px/mm)

- water_glass_1 (3581 frames; ~2 min on the desktop at first, 53 s with parallel batches): drop 1 58.4 -> 28.9 deg, base 2.41 -> 3.01 mm,
  1.68 -> 1.38 uL over 98 s; drop 2 68.7 -> 21.4 deg.
- water_glass_2: the main drop 70 -> 18 deg in 58 s, volume 1.82 -> 1.49 uL smooth (evaporation ~0.005 uL/s).
  The first drop was pushed to the image edge and is (correctly) not measured there.
- water_tape_1: 61-62 deg and 87-90 deg drops steady, as by hand. Short tracks at 62-65 s: the scene moved at
  the end of the recording.
- Whole frames match the hand values within 1.5 deg (tests/test_scene.cpp, three frames in tests/data).

## Open

- Base width now comes from the Young-Laplace fit (whole outline): frame-to-frame scatter 0.040 -> 0.006 mm
  (water_glass_2, 25 s of a resting drop). Tangent contacts remain for the tangent method itself.
- Contact angle while the needle is still in the drop is not meaningful (flagged, hollow markers in the plot).
- Not yet tried: absorbing samples (separators), 640x480 at 120 fps, drops without a visible reflection (dark,
  rough or opaque samples need the dark-substrate baseline or a manual one).
- A stain on the surface that looks like a flat drop is reported as a drop (tape, left). Decided (user): the
  user picks the drops afterwards (`--drops`, `report TABLE --drops`; later by clicking in the GUI) rather than
  guessing in code.
