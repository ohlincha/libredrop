#!/usr/bin/env python3
"""Goniometer camera capture with the same camera settings as Ossila's software (part of LibreDrop;
installed on Debian/Ubuntu as `libredrop-capture`). Linux only: V4L2, python3-opencv, ffmpeg, v4l-utils.

Settings were read from a USB capture of the official software (goniometer.pcapng):
MJPEG 1920x1080 @ 30 fps, manual exposure 39 (x100 us = 3.9 ms), gain 0,
brightness 0, contrast 32, hue 0, saturation 64, sharpness 0, gamma 72,
backlight compensation 2. White balance left on auto (Ossila does not touch it).
The short exposure assumes the goniometer's backlight is ON.

Examples:
  ./goniometer_capture.py                       # one image -> drop_<timestamp>.jpg
  ./goniometer_capture.py -n 20 -i 0.5          # 20 images, 0.5 s apart
  ./goniometer_capture.py --preview             # live view; SPACE saves, q quits
  ./goniometer_capture.py --exposure 80         # longer exposure if image is dark
  ./goniometer_capture.py --video 30 --size 640x480 --fps 120 --prefix sep_A_water
                                                # 30 s video at 120 fps (frames copied, not re-encoded)
  ./goniometer_capture.py --video 0 ...         # video until q is pressed in the terminal

Video: start recording, then put the drop on; keep recording until the drop has stopped changing.
A <name>.json next to the video holds the settings, the notes (--note) and the sample fields (--sample,
--side, --direction, --liquid, --volume, --control): LibreDrop's project library files the recording by these.
  libredrop-capture --video 20 --size 640x480 --fps 120 --sample kraft-A --side front --direction MD --liquid DMC
"""
import argparse
import datetime as dt
import glob
import json
import os
import subprocess
import sys
import time

import cv2

DEVICE_GLOB = "/dev/v4l/by-id/usb-HD_USB_Camera_HD_USB_Camera-video-index0"

OSSILA_CONTROLS = {
    "brightness": 0,
    "contrast": 32,
    "hue": 0,
    "saturation": 64,
    "sharpness": 0,
    "gamma": 72,
    "gain": 0,
    "backlight_compensation": 2,
    "exposure_dynamic_framerate": 0,
}


def find_camera():
    hits = glob.glob(DEVICE_GLOB)
    if not hits:
        sys.exit("Goniometer camera not found. Plug in the goniometer camera "
                 "(it shares the USB port with the four-point probe).")
    return os.path.realpath(hits[0])


def v4l2_set(dev, **ctrls):
    arg = ",".join(f"{k}={v}" for k, v in ctrls.items())
    subprocess.run(["v4l2-ctl", "-d", dev, "-c", arg], check=True)


def apply_settings(dev, exposure, wb):
    # auto_exposure must be set to manual (1) before exposure_time_absolute is accepted
    v4l2_set(dev, auto_exposure=1)
    ctrls = dict(OSSILA_CONTROLS, exposure_time_absolute=exposure)
    if wb is None:
        ctrls["white_balance_automatic"] = 1
    else:
        ctrls["white_balance_automatic"] = 0
    v4l2_set(dev, **ctrls)
    if wb is not None:
        v4l2_set(dev, white_balance_temperature=wb)


def open_camera(dev, width, height, fps):
    cap = cv2.VideoCapture(dev, cv2.CAP_V4L2)
    if not cap.isOpened():
        sys.exit(f"Could not open {dev} (is another program using the camera?)")
    cap.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc(*"MJPG"))
    cap.set(cv2.CAP_PROP_FRAME_WIDTH, width)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, height)
    cap.set(cv2.CAP_PROP_FPS, fps)
    cap.set(cv2.CAP_PROP_BUFFERSIZE, 1)
    return cap


def grab(cap):
    ok, frame = cap.read()
    if not ok:
        sys.exit("Frame grab failed.")
    return frame


def save(frame, outdir, prefix, idx=None):
    stamp = dt.datetime.now().strftime("%Y%m%d_%H%M%S_%f")[:-3]
    name = f"{prefix}_{stamp}" + (f"_{idx:04d}" if idx is not None else "") + ".png"
    path = os.path.join(outdir, name)
    cv2.imwrite(path, frame)  # PNG: lossless, no extra JPEG generation loss
    return path


def sample_meta(a):
    """What LibreDrop files the recording under (its project library); empty fields are left out."""
    m = {"sample": a.sample, "batch": a.batch, "side": a.side, "direction": a.direction, "liquid": a.liquid,
         "volume_ul": a.volume, "control": a.control or None}
    return {k: v for k, v in m.items() if v not in (None, "")}


def record_video(dev, a, w, h):
    """MJPEG stream copied into Matroska with the camera's timestamps (no generation loss)."""
    stamp = dt.datetime.now().strftime("%Y%m%d_%H%M%S")
    base = os.path.join(a.outdir, f"{a.prefix}_{stamp}")
    cmd = ["ffmpeg", "-hide_banner", "-loglevel", "warning", "-stats",
           "-f", "v4l2", "-input_format", "mjpeg", "-framerate", str(a.fps), "-video_size", f"{w}x{h}",
           "-ts", "mono2abs", "-i", dev, "-c:v", "copy"]
    if a.video > 0:
        cmd += ["-t", str(a.video)]
    cmd += [base + ".mkv"]
    meta = {"started": dt.datetime.now().isoformat(timespec="milliseconds"), "device": dev,
            "size": f"{w}x{h}", "fps_requested": a.fps, "exposure_100us": a.exposure,
            "white_balance": a.wb or "auto", "controls": OSSILA_CONTROLS, "note": a.note,
            "sample": sample_meta(a)}
    print(f"Recording {base}.mkv" + (f" for {a.video} s" if a.video > 0 else " (q to stop)"), file=sys.stderr)
    proc = subprocess.Popen(cmd, stdin=None if a.video == 0 else subprocess.DEVNULL)
    time.sleep(1.0)
    apply_settings(dev, a.exposure, a.wb)   # opening the stream can reset controls
    proc.wait()
    probe = subprocess.run(["ffprobe", "-v", "error", "-count_packets", "-select_streams", "v:0",
                            "-show_entries", "stream=nb_read_packets:format=duration", "-of", "json",
                            base + ".mkv"], capture_output=True, text=True)
    try:
        info = json.loads(probe.stdout)
        frames = int(info["streams"][0]["nb_read_packets"])
        dur = float(info["format"]["duration"])
        meta.update(frames=frames, duration_s=dur, fps_measured=round(frames / dur, 2) if dur else None)
        print(f"{frames} frames in {dur:.2f} s = {frames / dur:.1f} fps", file=sys.stderr)
        if dur and frames / dur < 0.9 * a.fps:
            print(f"WARNING: {frames / dur:.0f} fps instead of {a.fps}: the exposure may be too long "
                  f"for this frame rate, or the USB bus is busy.", file=sys.stderr)
    except (ValueError, KeyError, IndexError, ZeroDivisionError):
        print("Could not read the recording back (ffprobe).", file=sys.stderr)
    with open(base + ".json", "w") as f:
        json.dump(meta, f, indent=2)
    print(base + ".mkv")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-n", "--count", type=int, default=1, help="number of images (default 1)")
    ap.add_argument("-i", "--interval", type=float, default=0.0, help="seconds between images")
    ap.add_argument("-o", "--outdir", default=".", help="output directory")
    ap.add_argument("--prefix", default="drop", help="file name prefix")
    ap.add_argument("--exposure", type=int, default=39, help="exposure in 100 us units, 1-5000 (Ossila: 39)")
    ap.add_argument("--wb", type=int, default=None, help="lock white balance to this K (2800-6500); default auto")
    ap.add_argument("--size", default="1920x1080", help="WxH (default 1920x1080)")
    ap.add_argument("--fps", type=int, default=30)
    ap.add_argument("--warmup", type=float, default=1.0, help="seconds of frames to discard first")
    ap.add_argument("--preview", action="store_true", help="live window: SPACE saves, q/ESC quits")
    ap.add_argument("--video", type=float, default=None, metavar="SECONDS",
                    help="record video (0 = until q); use e.g. --size 640x480 --fps 120 for fast absorption")
    ap.add_argument("--note", default="", help="free text stored with a video")
    g = ap.add_argument_group("sample (stored with a video; LibreDrop files the recording by these)")
    g.add_argument("--sample", default="", help="sample name, e.g. kraft-A")
    g.add_argument("--batch", default="", help="batch or sheet")
    g.add_argument("--side", default="", help="e.g. front, back")
    g.add_argument("--direction", default="", help="e.g. MD, CD")
    g.add_argument("--liquid", default="", help="e.g. water, DMC, LP30")
    g.add_argument("--volume", type=float, default=None, help="dispensed volume, uL")
    g.add_argument("--control", action="store_true", help="a control/reference sample (glass, PTFE, a known separator)")
    a = ap.parse_args()
    if a.prefix == "drop" and (a.sample or a.liquid):   # file name from the sample: liquid_sample_side_direction
        parts = [a.liquid, a.sample, a.side, a.direction]
        a.prefix = "_".join("".join(c if c.isalnum() or c in "-." else "-" for c in p) for p in parts if p)

    w, h = (int(x) for x in a.size.lower().split("x"))
    os.makedirs(a.outdir, exist_ok=True)
    dev = find_camera()
    apply_settings(dev, a.exposure, a.wb)
    if a.video is not None:
        if a.exposure * 100e-6 > 1.0 / a.fps:
            sys.exit(f"Exposure {a.exposure * 0.1:.1f} ms is longer than a frame at {a.fps} fps.")
        record_video(dev, a, w, h)
        return
    cap = open_camera(dev, w, h, a.fps)
    apply_settings(dev, a.exposure, a.wb)  # re-apply once streaming, in case the open reset anything

    t_end = time.time() + a.warmup
    while time.time() < t_end:
        grab(cap)

    frame = grab(cap)
    if frame.mean() < 15:
        print(f"WARNING: image is nearly black (mean {frame.mean():.1f}/255). "
              "Is the goniometer backlight on? Otherwise raise --exposure.", file=sys.stderr)

    try:
        if a.preview:
            n = 0
            while True:
                frame = grab(cap)
                cv2.imshow("goniometer (SPACE=save, q=quit)", frame)
                k = cv2.waitKey(1) & 0xFF
                if k == ord(" "):
                    n += 1
                    print(save(frame, a.outdir, a.prefix, n))
                elif k in (ord("q"), 27):
                    break
            cv2.destroyAllWindows()
        else:
            for k in range(a.count):
                if k:
                    t_next = time.time() + a.interval
                    while time.time() < t_next:  # keep draining so frames are fresh
                        grab(cap)
                frame = grab(cap)
                print(save(frame, a.outdir, a.prefix, k + 1 if a.count > 1 else None))
    finally:
        cap.release()


if __name__ == "__main__":
    main()
