# Real recordings for testing

Recordings from the lab's Ossila goniometer, kept so that every change to the analysis can be checked against
them. The videos (`real/*.mkv`, 0.1–0.6 GB each) are not in git: they are on the development desktop in
`testdata/real/` (and the originals on beryllium in `~/Documents/LibreDrop/`); `real/SHA256SUMS` identifies them.
Their `.json` (camera settings, sample, frames) is in git. Single frames from them are in `tests/data/` and are
tested on every build (`tests/test_real.cpp`).

Run `tools/regress_videos.sh` and compare with `expected.txt` (the output when the files were added).

| Recording (2026-11-25) | Sample | What it shows | Expected |
|---|---|---|---|
| `Water_20261125_114532` | water on glass | reflection; 1920×1080, 30 fps, 19.8 s; drop lands at 14.4 s | 1 drop, 52.7° at release → 36° at the end, 4.4 → 5.0 mm, 0 frames lost (a speck once split the drop: gaps at 16.1–16.3 s) |
| `Water_3m-postit_20261125_115059` | water on a 3M post-it | opaque, no reflection; curled sheet, fibrous ridge, lifted sheet edge on the left, debris on the right; 34.5 s | 1 drop from 15.2 s, ~100° → ~55°. **Known weak**: the ridge under the spreading drop; volume grows (wrong) |
| `Water_3m-postit_20261125_115506` | water on a 3M post-it (2nd drop) | as above, wavy; 90 s at 30 fps; drop lands at 19.6 s and soaks in | 1 drop, 113° → 21–25° over 70 s, base 2.8 → 3.7 mm, volume above the paper 8.3 → 2.3 µL. Noisy in the last seconds |
| `Water_3m-postit_20261125_115637_cont` | same drop, continued | starts 1 s after the one above ends; 84.5 s; the drop finishes soaking in | **Known weak**: followed only for 20 s (26° → 12°), volume nonsense (546 µL) |

The user called the post-it "probably the hardest sample you will get": separators should be mounted flat, with a
straight clean edge towards the camera, which the analysis handles reliably (a straight sample edge is used as
the surface).

Known weaknesses (expected failures in `tests/test_real.cpp`): Young–Laplace on post-it A at 18 s (54° instead of
~90°) and 33 s (no fit), and post-it B at 88 s (18° instead of ~25°, the wet paper raised under the drop); the
circle and height-width methods are right there.
