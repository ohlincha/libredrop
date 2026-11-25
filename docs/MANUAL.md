# LibreDrop user manual

LibreDrop measures contact angles and drop shapes from a backlit side view of a drop, as seen by the lab's Ossila
contact angle goniometer. It records the drop from the camera and finds the surface and every drop by itself, in
every frame of the video. It was written for battery separators (paper, pulp, nylon), where the drop spreads and
soaks in, so it works with videos rather than single pictures.

What you get for each drop: the contact angle (left, right and a fit of the whole outline), base width, height and
volume against time, and for soaking samples the angle at fixed times, the sorptivity and when the drop was
absorbed.

## 1. Installing

Download the installer for your computer from the releases page:
<https://github.com/ohlincha/libredrop/releases>

- **Debian / Ubuntu**: download the `.deb` file, then in a terminal in the download folder:
  `sudo apt install ./libredrop_…deb` (the full file name, e.g. `libredrop_0.2.9_amd64.deb`).
- **Windows**: run the `.exe` installer. **macOS**: open the `.dmg` and drag LibreDrop to Applications.

The **live camera works on Linux only** so far. On Windows and macOS you can open and analyse recordings, and
practise with the simulator (section 7). Recordings from the goniometer camera open directly; other video formats
need ffmpeg (Linux: `sudo apt install ffmpeg`).

## 2. First start and the main window

Start **LibreDrop** from the applications menu. To try it without the instrument, choose
**Help ▸ Open example (water on glass)**: a short recording of a water drop on glass opens and is analysed.

![The main window with the example recording analysed](manual-main.png)

| Part | What it is |
|---|---|
| **Image** (left) | The frame shown, with the surface (dashed line), each drop's outline, contact points, tangents and angle. Wheel: zoom; drag: move; click a drop: zoom to it; double-click: whole frame. |
| **Play bar** (bottom left) | Play/pause (Space), speed, and the slider to step through the video. |
| **Measurement** (top right) | Scale (px/mm), liquid, which frames to measure, soaking times and evaporation. **Measure…** opens the camera, **Analyse** measures the open recording again. |
| **Shape / Soaking tabs** | The table of drops (section 4) and the soaking results (section 5). |
| **Checks** (below the table) | Warnings about the image: focus, tilt, drop too small, needle in the drop, … |
| **Plot** (bottom right) | Angle, base width and volume against time. Click it to jump to that time. |
| **Status bar** | What the analysis found, or what went wrong. |

The menus: **File** (measure, open, export, compare samples), **Project** (a folder for a study, section 6) and
**Help** (this manual, the example, a short guide to separators).

## 3. Measuring with the camera

Open **File ▸ Measure with the camera…** (Ctrl+M), or press **Measure…** in the main window.

![The Measure window (here with the simulator as camera)](manual-measure.png)

1. **Camera.** Choose the goniometer camera in the *Camera* list (press *Look again* if you plugged it in after
   opening the window). *Size and rate*: 640 × 480 at 120 fps for liquids that soak in within a second or two,
   otherwise the larger size. The goniometer camera gets Ossila's settings; exposure 3.9 ms is right with the
   backlight on. If the image is too dark, raise the *Exposure*; the window warns about a black or white image.
2. **Scale** (once per camera size). Lower the needle into view so it comes down from the top edge of the image,
   press **From needle…** and enter the needle's **outer diameter**: the lab's needle is **0.4745 mm**. The
   scale (px/mm) is remembered for that image size. Without a scale, sizes and volumes are in pixels.
3. **Find drops in the live image** (tick box): shows the surface, the outline and the angle while you set up.
   Useful to check the focus and the stage before the drop goes on.
4. **Sample details, before recording.** Fill in *Sample* (e.g. kraft-A), *Side*, *Direction* (MD or CD for paper),
   *Liquid* and *Volume*; tick *Control or reference sample* for glass or PTFE. Add a *Note* if useful.
5. **File name.** Leave it empty: the name is made from liquid, sample, side and direction, and the date and time
   are added when you press Record (the grey line below shows it, e.g. `Water_kraft-A_front_MD_20261125_101500.mkv`).
   Type your own name only if you need to.
6. **Record.** Bring the drop down and press **Record** (Ctrl+R) as it touches the surface. The video **always
   starts 2 s before you press Record**, so the moment of contact is in it even if you press late.
7. **Stop.** Press **Stop** when the drop has settled or soaked in. Or set *Stop* to a number of seconds before
   recording, and it stops by itself.

**Where the files go.** With a project open: into the project's `recordings/` folder, filed under the sample.
Without a project: into `Documents/LibreDrop` in your home folder, or the folder chosen with *Folder…*; *Open*
shows it in the file manager. Each recording is a `.mkv` video with a `.json` file next to it holding the sample
details. Keep both. With *Open and analyse when stopped* ticked, the recording opens in the main window and is
analysed straight away.

## 4. Analysing a recording

1. **Open** it: *File ▸ Open video or image…* (Ctrl+O), or drag the file onto the window. Images (PNG, JPEG, TIFF)
   work too. The analysis starts by itself; the status bar shows its progress. *Analyse* runs it again (for
   example after changing the scale or the liquid); while it runs, the button reads *Cancel*.
2. **Check the scale and liquid** in *Measurement*. The liquid (water, DMC, DEC, PC or custom) sets density and
   surface tension for the shape fit. *Frames*: "all", or 1 in n to make long videos faster.
3. **Read the table** (*Shape* tab). One line per drop, with the time it touched the surface (*Contact*), then two
   rows:
   - **release**: just after the needle has left the drop (median of the first 5 frames after);
   - **end**: at the end of the recording (median of the last 5 frames).
   *Angle* is the Young–Laplace fit of the whole outline: **the contact angle to use**. *Left* and *Right* are the
   tangents at each contact point, as a check. A drop marked *on needle* never came free of the needle.
   Hover over the end row for the rates of change.
4. **Look at the plot and the images.** Step through the video with the slider or play it; click the plot to jump
   to a time. Check that the outline follows the drop and the surface line is on the surface. Tick
   *Time from each drop's contact* to put t = 0 at each drop's touch.
5. **Tick the drops that count.** Untick anything that is not a real drop on the sample (dust, a drop dragged by
   the needle). Only ticked drops are plotted and exported.
6. **Export**: *File ▸ Export table (CSV)…* writes every frame's values for the ticked drops (opens in a
   spreadsheet); *File ▸ Export plot (PNG)…* saves the plot.

*File ▸ Save compact copy…* makes a much smaller copy of a recording (the band around the drops, in grey), handy
for keeping or sending.

## 5. Soaking measurements on separators

For porous samples where the drop soaks in. Work in this order:

1. **Set up** once per camera size: the scale (section 3) and the liquid.
2. **Glass first**, once per liquid and day: record a drop of the same liquid on a clean glass slide, for about as
   long as you will record the separators. Open it, let it analyse, then go to the **Soaking** tab and press
   **Use as glass reference**. This measures how fast the liquid evaporates, so that evaporation is not counted as
   soaking. It is kept for that liquid. The Soaking tab tells you (in amber) when this is missing.
3. **Samples**: record each sample, **3 drops or more**; both sides and both directions (MD and CD) if they may
   differ. Use 640 × 480 at 120 fps when the liquid soaks in within a second or two.
4. **Read the Soaking tab** for each recording:

![The Soaking tab for a separator (simulated recording)](manual-soaking.png)

| Column | Meaning |
|---|---|
| **θ at 0.1 s, 1 s, 10 s** | Contact angle at these times after contact. Change the times under *Measurement ▸ Soaking*. "—" means the drop was still on the needle then, or already gone. |
| **Sorptivity** (mm/√s) | How fast the sample takes up liquid: absorbed volume = S · contact area · √t. On glass it should come out about 0. |
| **R²** | How well the drop follows that law (1 = exactly). Low values: treat S with care. |
| **Absorbed after** (s) | Time from contact until the drop disappeared; empty if it was still there at the end. |

The sorptivity depends on the scale: compare only samples measured at the same scale (camera size).

## 6. Projects and comparing samples

**Projects** keep a study together. *Project ▸ New project…* makes a folder with the recordings, sample list,
liquids, scales and glass references. The **Library** panel then lists samples and recordings: double-click a
recording to open it; *Add…* files existing recordings. Opening a recording from a project sets its liquid, scale
and that day's evaporation correction for you. Back up or share a project by copying its folder. One user at a
time.

**Compare samples** (*File ▸ Compare samples…*, or *Compare* in the Library): add the recordings of all samples
(same liquid) and press **Analyse**. Recordings with the same sample name are averaged (mean ± standard deviation
of the ticked drops). The sample name comes from the file name without date, time and repeat number;
double-click it to change it. *Export CSV…* writes every drop and the per-sample means.

![Compare samples (simulated recordings)](manual-compare.png)

## 7. Practising with the simulator

No instrument needed. In the Measure window, pick a camera whose name starts with **Simulator:**, for example
*Simulator: Flat separator, soaking* or *Simulator: Water on glass (reflection)*. A needle brings a drop down, it
touches, spreads and (on separators) soaks in, again and again. Everything works as with the real camera: set the
scale from the needle, fill in the sample, Record and Stop, then analyse.

The simulator draws every frame, which is slow on a busy computer. Choose the **half size** in *Size and rate*
(960 × 540) to keep up with 30 fps.

## 8. Tips for good images

- **Backlight on, lens cap off.** The drop must be a dark shape on a bright background.
- **Focus on the drop.** A soft edge gives a warning ("The drop's edge is blurred") and less accurate angles.
- **Level the stage.** The checks report a tilted baseline.
- **Separators: mount them flat**, e.g. taped on a glass slide, with a **clean, straight cut edge towards the
  camera**. Frayed fibres, waves or a lifted edge make the surface line hard to find.
- **Leave space**: the drop must not touch the image edge, and should be well over 150 px wide (zoom in or use a
  larger drop).
- **Withdraw the needle** after the drop is placed; the needle in the drop spoils the angle.
- Record for as long on glass as on the samples, and record the glass reference on the same day.

## 9. Troubleshooting

| Problem | What to do |
|---|---|
| The live image is **black** | Backlight on? Lens cap off? Otherwise raise the exposure. A **white** image: lower it. |
| **No camera found** | Plug the camera in and press *Look again*. Live camera: Linux only for now. |
| The camera runs slower than its rate | The exposure is longer than one frame: shorten it. The real frame times are recorded, so results are not affected. |
| **"no drop on a surface"** / **"surface not found"** | The surface must be visible as a straight edge across the image (or, on glass, the drop's reflection). Check backlight and focus; mount the sample flat with a straight edge. |
| Angle looks wrong | Step to the frame and look: does the outline follow the drop, is the dashed line on the surface? Read the checks below the table. Untick drops that are not real. |
| Sizes in px, no volume | Set the scale (*From needle…*, 0.4745 mm for the lab's needle). |
| **"No needle found"** when setting the scale | The needle must come down from the top edge of the image; move to a frame where it does. |
| Soaking tab: **no evaporation correction** | Record and analyse a drop on glass, then *Use as glass reference* (section 5). |
| The table or plot is cut off | Drag the divider between the image and the right-hand panel (or between table and plot) to make it wider. |
| A video does not open | Recordings from LibreDrop always open; other formats need ffmpeg installed. |
| Analysis of a long video is slow | Set *Frames* to 1 in 2 or more, and press *Analyse*. |

LibreDrop is free and open source (MIT licence): <https://github.com/ohlincha/libredrop>. Report problems there.
