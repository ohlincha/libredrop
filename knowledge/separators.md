# Battery separators: what to measure

Decided 2026-11-23 with the user. Samples: cellulose/pulp and nylon separators (porous, absorbing); liquids:
water, organic carbonates (PC, DMC, DEC, EMC), electrolytes. No tilting stage; hand syringe.

Porous separators absorb and spread, so a single static angle is arbitrary. Priorities:

1. **Dynamic wetting from video**: contact angle, base width, height and volume vs time from first contact
   (t = 0 detected automatically). Report the initial angle, angles at fixed times (0.1, 1, 10 s), wetting
   time (angle below a threshold or drop gone), absorption rate (volume) and spreading rate (base width),
   power-law fits (capillary uptake ~ √t).
2. Same separator with several liquids.
3. Pendant drop: surface tension of each electrolyte (literature values for mixtures are scattered).
4. Machine/cross direction and the two faces of the sheet.
5. Static angle and surface free energy only for non-absorbing samples (films, polyolefin references);
   refuse surface energy on absorbing substrates.
6. Later: wicking (capillary rise of a strip), same video tracking, different setup.

Advancing/receding without a tilting stage: needle-in, add then withdraw liquid by hand while recording;
use frames where the contact line moves slowly (stick–slip with a hand syringe).

Checks: frame rate vs absorption speed (report "faster than X ms"); sample flat (substrate edge straight);
gentle deposition (no oscillation in the first frames); evaporation of DMC/DEC (reference on glass);
humidity and temperature logged (TEMPer); LiPF6 electrolytes release HF with moisture: fume hood, dry room
or glovebox; replicates; low angles (height–width method).

Camera: MJPEG 640×480 @ 120 fps or 1920×1080 @ 30 fps (speed vs resolution).
