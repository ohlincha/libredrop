# Camera scale (Ossila goniometer, beryllium)

Measured 2026-11-24 with Ossila's 10 mm calibration sphere (kit item, per the Ossila manual), circle fitted to
the backlit silhouette (camera defaults, auto exposure). Valid only while camera distance and zoom/focus are
unchanged; re-measure after moving the camera.

| Mode (MJPEG) | Sphere diameter | Scale | Note |
|---|---|---|---|
| 1920x1080 @ 30 | 942.3 px (rms 0.7) | 94.2 px/mm, 10.61 um/px | full view |
| 640x480 @ 120 | 454.8 px (rms 0.5) | 45.5 px/mm, 22.0 um/px | 4:3 crop, not a plain downscale of 1080p |
| 1280x720 @ 60 | ~1080 px (sphere clipped) | ~108 px/mm (rough) | tighter crop; re-measure with a smaller object |

Each mode crops the sensor differently, so the scale must be stored per mode (the capture .json should record
the mode; LibreDrop should take px/mm from a calibration file keyed by mode).
