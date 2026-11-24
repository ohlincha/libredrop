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
4. Video: ffprobe packet times, frames piped from ffmpeg as raw grey. Pass 1 takes the median surface of ~40
   frames; pass 2 measures every frame with it. Tracking by position and width; when the slide is pushed, all
   drops move together, so the common shift is found first. Tracks under 0.5 s or with no usable frame are
   dropped. A point is usable if measured and without error checks (e.g. cut off at the image edge).

## Results on the first videos (2026-11-24, 1080p30, cropped to rows 300-900, 94.23 px/mm)

- water_glass_1 (3581 frames, ~2 min on the desktop for 2 passes): drop 1 58.4 -> 28.9 deg, base 2.41 -> 3.01 mm,
  1.68 -> 1.38 uL over 98 s; drop 2 68.7 -> 21.4 deg.
- water_glass_2: the main drop 70 -> 18 deg in 58 s, volume 1.82 -> 1.49 uL smooth (evaporation ~0.005 uL/s).
  The first drop was pushed to the image edge and is (correctly) not measured there.
- water_tape_1: 61-62 deg and 87-90 deg drops steady, as by hand. Short tracks at 62-65 s: the scene moved at
  the end of the recording.
- Whole frames match the hand values within 1.5 deg (tests/test_scene.cpp, three frames in tests/data).

## Open

- Base width is noisy (+-0.1 mm frame to frame) while angle and volume are smooth: it comes from the
  tangent/circle contact points; take it from where the outline meets the surface instead.
- Contact angle while the needle is still in the drop is not meaningful (flagged, hollow markers in the plot).
- Not yet tried: absorbing samples (separators), 640x480 at 120 fps, drops without a visible reflection (dark,
  rough or opaque samples need the dark-substrate baseline or a manual one).
- A stain on the surface that looks like a flat drop is reported as a drop (tape, left).
