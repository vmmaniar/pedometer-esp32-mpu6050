# Build Plan — Wrist-Worn Pedometer (ESP32 + MPU-6050)

**Author:** Vansh Mehul Maniar (Electronics & Instrumentation, BITS Pilani Goa)
**Location:** Pune / Goa, India
**Window:** ~5 weeks of focused part-time work
**Status:** Scaffold already exists (ESP-IDF firmware, custom MPU-6050 + SSD1306 drivers, adaptive-threshold step detector, parametric Fusion 360 enclosure notes); this document is the *how to actually build it* layer.

---

## 1. Executive summary

### Elevator pitch
A pocketable, battery-powered wrist-worn step counter built on a custom 35 × 30 mm two-layer PCB, running ESP-IDF on an ESP32-WROOM-32, sampling an MPU-6050 6-axis IMU at 100 Hz, and rendering live step count + cadence on a 0.96" SSD1306 OLED. The step-detection algorithm uses peak detection on `|a| − 1g` with an adaptive threshold tuned against ~200 hand-counted reference steps. A TP4056 single-cell charger and 500 mAh LiPo give ~36 hours of continuous use; a 3D-printed PETG enclosure makes it actually wearable. The deliverable is one PCB on your wrist, one Python validation notebook, and a five-minute video of you walking around the lab while the device counts.

### Demoable end state
1. Wear the device on a 1000-step walk. Hand-count vs. displayed count agrees within ±3 % (~30 steps).
2. Live cadence reads 100 – 130 SPM on a comfortable walking pace and 160 – 200 SPM on a slow jog.
3. Step count persists across a reboot (NVS storage, save every 60 s).
4. Battery lasts ≥ 24 hours on a full charge under "screen-on" usage.
5. False-positive rejection: typing at a keyboard for 5 minutes adds ≤ 5 false steps.
6. A 5-minute demo video walking + jogging + sitting + typing + climbing stairs.

### Success criteria (binary)
- [ ] Step count error ≤ ±3 % on a 1000-step measured walk
- [ ] Cadence reading updates at ≥ 1 Hz UI refresh rate, lags real cadence by < 5 s
- [ ] Battery life ≥ 24 hours with default UI duty cycle
- [ ] OLED readable in indoor light without backlight bleed
- [ ] PCB fits in a 40 × 40 × 12 mm enclosure
- [ ] Total weight (PCB + enclosure + battery + strap) ≤ 50 g

### Total budget envelope
**Approximately ₹3,000 – ₹4,500** (≈ USD 36 – 54) including a small contingency, *excluding* an oscilloscope and a 3D printer. Detailed itemized cost summary is in §9. This is one of the **cheapest** projects in the portfolio and the parts are stocked everywhere in India.

### Total time estimate
5 weeks at ~10 hrs/week ≈ 50 hours. About 20 % of that is the algorithm tuning loop (record walks → tweak constants → re-walk).

---

## 2. Architecture decisions (with reasoning)

The scaffold commits to **ESP32-WROOM-32 + MPU-6050 + SSD1306 + TP4056 + LiPo**. These are all 2014-era parts but they are the **right** parts for a hobbyist wearable in 2026 because they are cheap, ubiquitous in India, and every breakout board uses the same pinout. See §2.5 for the small swap recommendations.

### 2.1 Why ESP32-WROOM-32 over alternatives

The ESP32 is the cheapest wearable-capable MCU with Wi-Fi/BLE on-die. For a single-cell-LiPo wearable, the real competition is **nRF52840** (BLE-only, lower current) and **ESP32-C3** (Wi-Fi + BLE, single-core RISC-V, lower current than ESP32-WROOM-32).

**Considered alternatives and why they lose:**
- **nRF52840** (Adafruit Feather): the *correct* answer for a production wearable — 1 µA deep sleep, BLE-friendly. **But** the tooling is heavier (Zephyr RTOS or Adafruit's nRF52 Arduino port), the dev kit is ~₹2,500 (vs. ₹250 for an ESP32-WROOM-32 clone), and the user already knows ESP-IDF from the [pulse-oximeter-iot] and [digital-frequency-meter] projects. Tooling familiarity wins.
- **ESP32-C3 / ESP32-S3**: drop-in upgrade with better sleep current (~10 µA), single core but faster IPC. C3 is ₹350 dev kit and would extend battery life ~30 %. **Worth swapping** for the production rev — see §2.5 recommendation #1.
- **Apollo3 Blue** (Ambiq): designed for wearables, ridiculous power efficiency (sub-µA at run). Tooling is painful; SDK is not as friendly as ESP-IDF. Skip.
- **STM32L4** + nRF Connect: viable but two-MCU adds BOM cost and code complexity. Skip for this project.

**India availability:** ESP32-WROOM-32 dev kit ₹250 on Robu.in ([ESP32 Robu]) or QuartzComponents; bare WROOM-32E module ₹220 on LCSC India. Plenty of stock.

### 2.2 Why MPU-6050 over LSM6DSO, BMI270, or ICM-42688

The scaffold picks **MPU-6050**. This is the old reliable. The newer parts are *technically* superior — lower noise, better temperature compensation, finer wake-up gestures — but for a peak-detection step counter the MPU-6050's 100 Hz output noise floor (~4 mg) is already 100× lower than the step-strike amplitude. The MPU-6050 is also the easiest part to source in India (every hobbyist breakout board uses it) and has the largest community for the Allan deviation studies that matter for sensor fusion.

Comparison matrix:

| IMU | Noise (RMS) | Sleep current | Wake gestures | Package | INR (India) |
|---|---|---|---|---|---|
| **MPU-6050** ★ | ~4 mg | ~5 µA | None (interrupt on motion only) | LGA-24 (4×4) | ₹150 breakout, ₹70 bare |
| ICM-20948 | ~3 mg | ~1.2 µA | Configurable | LGA-24 | ₹450 breakout |
| LSM6DSO | ~2 mg | ~0.4 µA | Pedometer in HW! | LGA-14 (2.5×3) | ₹500 breakout |
| BMI270 | ~1.5 mg | ~0.4 µA | 38 wake gestures incl. pedometer | LGA-14 | ₹600 breakout |
| ICM-42688 | ~1.5 mg | ~0.4 µA | Pedometer in HW | LGA-16 | ₹550 breakout |

**Headline choice: keep MPU-6050** for the resume build, despite the LSM6DSO/BMI270 having on-chip pedometers. Reason: the *algorithm is the pedagogy*. A hardware pedometer hides the work; a software one demonstrates DSP. Add **LSM6DSO** as the Phase 7 stretch — "I ran the same algorithm on both the MPU-6050 *and* the LSM6DSO's hardware pedometer, here is the step-by-step error comparison".

### 2.3 Why SSD1306 OLED over Sharp Memory or e-paper

SSD1306 is the cheapest, lowest-friction OLED in India. The cost is **power consumption** — an OLED at full white pulls 20 mA, which is a significant chunk of the 200 mAh/day budget on a 500 mAh battery.

Comparison matrix:

| Display | Size | Current (typical) | Refresh | INR | Tradeoff |
|---|---|---|---|---|---|
| **SSD1306 0.96" I2C** ★ | 128×64 | 8 – 20 mA | Fast | ₹250 | Cheap, easy, bright |
| Sharp LS013B7DH03 | 128×128 | 0.05 mA (!) | Slow (1 Hz refresh) | ₹2,000 | Battery-perfect but expensive and slow |
| Waveshare 1.54" e-paper | 200×200 | 0.001 mA standby | Very slow (1 s) | ₹1,800 | Best battery; refresh too slow for cadence |
| Nokia 5110 (PCD8544) | 84×48 | 5 mA + backlight | Fast | ₹150 | Cheap but pixels too low for "MM:SS / cadence" UI |

**Headline: keep SSD1306** and mitigate power consumption with **auto-off after 10 s of UI inactivity** + a wrist-tilt wake-up via the IMU. Phase 4 deliverable.

### 2.4 Why TP4056 + 500 mAh LiPo over coin cell or AA

A 500 mAh LiPo at 3.7 V gives 1.85 Wh of capacity. At 70 mA average current (ESP32 idle + IMU + OLED 30 % duty), that's ~26 hours.

Considered alternatives:
- **CR2032 coin cell** (220 mAh): looks the part, but a 220 mAh / 3 V cell can't supply the ESP32's Wi-Fi-on peak current (~250 mA). Reduces battery life to ~3 hours. Skip.
- **AAA NiMH × 2** (800 mAh @ 1.2 V each = 2.4 V): below the ESP32's 3.0 V min Vin. Would need a boost converter. Skip.
- **502035 LiPo 350 mAh** (small flat pouch): perfect form factor for the wrist, easy to source. ₹220 on Robu. **Alternative pick** if 500 mAh is too thick for the enclosure.

The TP4056 charge controller is the *cheapest correct* answer in India — it has constant-current/constant-voltage charge and undervoltage protection in one IC. The "blue" TP4056 modules from Robu (₹40) include the BMS (DW01 + 8205) for short/over-discharge protection. **Buy the blue ones, not the red ones** — see §2.5.

### 2.5 Architecture deltas from the scaffold (recommendations)

| # | Scaffold | Recommendation | Why |
|---|---|---|---|
| 1 | ESP32-WROOM-32 | Keep for MVP; **swap to ESP32-C3-MINI-1** for Rev 2 | C3 is ~30 % lower idle current and ~₹50 cheaper. Same IDF code mostly. |
| 2 | MPU-6050 breakout (Sparkfun-clone) | Keep, but specify **GY-521 with footprint for MPU-9250 alternate** | If MPU-6050 stock runs out, the MPU-9250 is pin-compatible and adds a magnetometer. |
| 3 | SSD1306 standalone module | Keep, but **add a wrist-tilt wake-up** in firmware (Phase 4) | Tradition is to leave the OLED on; battery life suffers. Wake-on-tilt is ~3 lines of IMU code. |
| 4 | TP4056 red module | **Swap to TP4056 blue module (with DW01 BMS)** | Red modules lack overdischarge protection — a LiPo discharged below 2.5 V is permanently damaged. ₹40 vs. ₹25 — easy upgrade. |
| 5 | 500 mAh LiPo | Keep, but **specify Eremit 502035** (or equivalent flat pouch) | Generic LiPos from Robu come in random thicknesses. Eremit is consistently 5 mm thick which the enclosure design assumes. |
| 6 | Single 100 Hz polling task | Keep MVP; **add MPU-6050 FIFO** for low-power mode (Phase 6) | The FIFO lets the ESP32 sleep for ~250 ms between batched reads. Halves average current. |
| 7 | No buttons | **Add one tactile button** on GPIO 0 (BOOT) | Lets you reset count, toggle mode, force calibration. Cost: ₹8. |

---

## 3. Phase-by-phase plan (weeks 0 – 5)

### Phase 0 — Algorithm validation on recorded data (Week 0, ~8 hrs)

**Goal:** Prove the step detector works against real walks before touching hardware.

**Deliverables:**
- Record reference walks on your phone. Use the [SensorLogger Android app] or [Phyphox] (both free) to record 6-axis IMU data at 100 Hz while walking a known distance with known step count. Aim for 5 recordings:
  - 1000 steps at a slow walk (~80 SPM)
  - 1000 steps at a normal walk (~110 SPM)
  - 1000 steps at a brisk walk (~140 SPM)
  - 500 steps at a jog (~170 SPM)
  - 5 minutes typing at a desk (zero steps; tests false-positive rejection)
- Export each as CSV (`t, ax, ay, az, gx, gy, gz`) into `python_analysis/recordings/`.
- Create `python_analysis/step_detector.py` — a Python port of the C step detector in `firmware/main/step_detector.c`. Same algorithm, line-for-line, so you can tune constants in Python and copy them into C.
- Create `python_analysis/tune_thresholds.py` — sweep `k_threshold` from 0.5 to 2.5 in 0.1 steps, plot count error vs. threshold for each recording. Pick the value that minimizes worst-case error.
- Commit the recordings + analysis Jupyter notebook to the repo. **Anonymize the CSVs** — they contain GPS metadata in some recording apps.

**Time:** 8 hrs. **Risk:** Low. **Verification:** Python step detector matches hand-counted steps within ±3 % on all 5 recordings.

### Phase 1 — Dev-board bring-up (Week 1, ~8 hrs)

**Goal:** Run the existing scaffold firmware on an ESP32 + MPU-6050 + SSD1306 dev rig.

**Deliverables:**
- Buy the parts per §4.1. Use **breakout boards** for everything — no PCB yet.
- Bring up the I2C bus. Verify both devices respond to `i2cdetect` (or the equivalent ESP-IDF `i2c_scan` example) at 0x68 (MPU-6050) and 0x3C (SSD1306).
- Flash the existing `firmware/main/app_main.c`. Verify the OLED shows step count + cadence.
- Verify the MPU-6050 sampling rate is exactly 100 Hz by toggling a GPIO inside the polling loop and scoping it. (You will be **surprised** how often the loop runs at 97 Hz or 103 Hz because of FreeRTOS tick rounding.)
- Walk around the lab for 100 hand-counted steps. Verify the displayed count is within ±10.

**Time:** 8 hrs. **Risk:** Low — every dev kit has been tested by thousands of hobbyists. **Verification:** 100-step walk reads 95 – 105 on display.

### Phase 2 — Algorithm tuning on the wrist (Week 1 – 2, ~10 hrs)

**Goal:** Tune the step detector against real wrist-worn data, not Phase-0 phone data.

The wrist is **different** from the phone (different IMU position, different motion signature). The Phase-0 tuning gives you a starting point; this phase fine-tunes it.

**Deliverables:**
- Wear the dev kit (taped to your wrist with painter's tape — no enclosure yet) and run the same 5 reference walks from Phase 0. Stream the raw IMU data over USB CDC to the laptop. Save to CSV.
- Re-run `tune_thresholds.py` on the wrist data. **Expect** a different optimum `k_threshold` than on the phone — probably 1.0 – 1.4 vs. 1.2 from Phase 0.
- Update the constants in `firmware/main/step_detector.c`. Re-build, re-flash, re-walk. Iterate until walk error is ≤ ±3 %.
- **False-positive test**: type at the keyboard for 5 minutes wearing the wrist rig. Expect ≤ 5 false steps. If higher, increase `k_threshold` or add a minimum-energy gate.
- **Sit-still test**: sit motionless for 5 minutes. Expect 0 false steps.

**Time:** 10 hrs. **Risk:** Medium — algorithm tuning is iterative; budget time for surprises. **Verification:** Wrist-worn dev kit hits all 3 success criteria from §1 on the dev rig.

### Phase 3 — Custom PCB design (Week 2 – 3, ~12 hrs)

**Goal:** Tape-out a 2-layer 35 × 30 mm PCB ready for fab.

**Deliverables:**
- Schematic capture in **KiCad 8**. Source the ESP32-WROOM-32 symbol from KiCad's built-in `RF_Module` library; MPU-6050 from `Sensor_Motion`; SSD1306 from `Display_Character`.
- Layout:
  - 35 × 30 mm board outline (matches the [pulse-oximeter-iot] form factor for case reuse)
  - 2-layer, 1.6 mm
  - ESP32-WROOM-32 module on top, antenna in the 5 mm corner keepout (per Espressif AN), no copper / no pour under the antenna
  - MPU-6050 (or breakout-footprint) under the WROOM, ground reference centered to minimize rotation noise
  - SSD1306 connector on the *edge* of the board (the OLED is a separate ribbon-cable module — don't try to mount the OLED panel on this PCB)
  - LiPo 2-pin JST connector + TP4056 daughterboard footprint on the rear
  - USB-C for power + flashing (CP2102 footprint)
  - SWD via 4-pin 1.27 mm header for ESP-PROG
- DRC + ERC must pass with zero errors.
- 3D render generated for sanity-check of mechanical fit into the enclosure.
- Export Gerbers + IPC-356 + BOM CSV + Pick-and-place CSV.

**Time:** 12 hrs. **Risk:** Medium — first wearable-scale PCB requires careful antenna keepout and component thermal management on a small board. **Verification:** Run the Gerbers through JLCPCB's online viewer; have a friend review the schematic.

### Phase 4 — PCB bring-up + wrist-tilt wake-up (Week 3 – 4, ~10 hrs)

**Goal:** Custom hardware works and the OLED auto-sleeps.

**Deliverables:**
- Fab order placed with **JLCPCB economic assembly**. Plan for **8 – 14 days** delivery on Global Standard Direct Line ([JLCPCB customs]) — start fab at the beginning of Week 3.
- After boards arrive, smoke test: bench supply at **3.7 V, 100 mA limit**, no firmware. Verify 3V3 rail and current draw < 30 mA idle.
- Flash firmware. Test I2C bus (same as Phase 1).
- Test on-board USB-C charge: connect 5 V; verify TP4056's RED LED while charging, GREEN when full. Voltage should plateau at 4.20 V ± 0.05 V.
- Implement the **wrist-tilt wake-up**. Configure the MPU-6050's motion-detect interrupt (register 0x37) to trigger on |a| > 0.5 g on any axis. Wire that interrupt to a GPIO. On wake-up: re-enable the OLED, reset the 10 s sleep timer.
- Verify battery life: full charge → walk for 1 hour → measure consumed mAh on a USB power meter at next charge. Project full-charge life.

**Time:** 10 hrs. **Risk:** Medium-high — first custom PCB is always full of small bring-up surprises. **Verification:** Tilt-on-wrist wakes the OLED in < 200 ms; idle current ≤ 8 mA.

### Phase 5 — 3D-printed enclosure (Week 4, ~6 hrs)

**Goal:** A wearable enclosure that survives a 1-hour walk.

**Deliverables:**
- Re-export from the parametric Fusion 360 source (or use the scaffold's STL placeholders) the three STLs: `case_top.stl`, `case_bottom.stl`, `strap_loops.stl`.
- Print on a 0.4 mm nozzle Ender 3 or equivalent. Settings per `enclosure/README.md`: PETG, 0.16 mm layer, 25 % gyroid, 3 perimeters, no supports.
- Sand the OLED window with 1500-grit (the README notes that un-sanded PETG fogs).
- Snap-fit assembly check. The bottom-of-case ribs should press the LiPo against the back of the PCB; the top-of-case OLED window should not touch the OLED pixels.
- Wrist strap: a 20 mm wide silicone strap with a 4-mm steel buckle, cut to 200 mm length. Or use a discarded smartwatch strap.
- Wear test for 1 hour. Make sure (a) the OLED is readable, (b) the IMU does not "rattle" relative to the wrist (this **really** matters — wrist-coupled noise is what kills cheap step counters).

**Time:** 6 hrs (printing time + assembly). **Risk:** Low. **Verification:** Wearable for 1 hour without skin irritation; no rattling step false-positives.

### Phase 6 — Power optimization (Week 4 – 5, ~8 hrs)

**Goal:** Hit the 24-hour battery life success criterion.

**Deliverables:**
- Enable **MPU-6050 FIFO**. Configure to store 32 samples (at 100 Hz = 320 ms). Configure FIFO almost-full interrupt at 25 samples. The ESP32 wakes from light sleep, drains the FIFO, processes, sleeps again. **Idle current target: 4 mA.**
- Enable **ESP32 light sleep** during the FIFO drain interval. Use the `esp_sleep_enable_timer_wakeup()` + `esp_light_sleep_start()` pair. **Idle current target: 1.5 mA.**
- Auto-dim the OLED: after 10 seconds of no motion, drop the SSD1306 contrast register from 0xFF → 0x10 (saves ~12 mA). After 60 s of no motion, turn the OLED off entirely (`ssd1306_display_off()`). Wake on the IMU motion interrupt.
- Disable Wi-Fi entirely (the firmware does not need it for MVP — Phase 8 stretch enables it).
- Measure battery life: full charge → wear 24 hours doing normal activities → measure remaining capacity (should be > 30 %).

**Time:** 8 hrs. **Risk:** Medium — light-sleep + FIFO + I2C wake-up timing is non-trivial. **Verification:** Battery life ≥ 24 h on a 500 mAh cell.

### Phase 7 — Final integration test (Week 5, ~4 hrs)

**Goal:** Hit every success criterion from §1.

**Deliverables:**
- Conduct the 1000-step measured walk indoors and outdoors. Record displayed count vs. hand count; document in `docs/test_results.md`.
- Conduct the false-positive tests (typing, driving, sitting). Document.
- Conduct the wear test (8 hours on the wrist). Document.
- Update README.md with success-criteria check-marks and a link to `docs/test_results.md`.
- Record the 5-minute demo video.

**Time:** 4 hrs. **Risk:** Low. **Verification:** README success criteria all green.

---

## 4. Detailed BOM with India sourcing

This is the **purchase list** — what to actually order, where, and how much. INR prices observed May 2026; verify at order time. Where two suppliers are listed, prefer the first; the second is a backup.

### 4.1 Active components

| Ref | Part | Description | Qty | Supplier (primary) | INR @ qty | Notes |
|---|---|---|---|---|---|---|
| U1 | ESP32-WROOM-32E (module, not breakout) | MCU + Wi-Fi/BLE module | 2 | LCSC India / Robu | ~₹220 ea | 1 spare. |
| U1 alt | ESP32-WROOM-32D dev kit (CP2102) | If you want a populated dev kit instead of a bare module on a custom PCB | 2 | Robu.in ([ESP32 Robu]) | ~₹250 ea | Phase 1 only — replace with bare module in Phase 3 PCB. |
| U2 | MPU-6050 GY-521 breakout | 6-axis IMU + I2C breakout | 2 | Robu.in | ~₹150 ea | 1 spare; alternatively bare LGA part at ₹70 each. |
| U3 | SSD1306 0.96" I2C OLED module (4-pin) | Display | 2 | Robu.in | ~₹250 ea | 1 spare. |
| U4 | TP4056 1A charger with DW01 BMS (BLUE module) | LiPo charger + protection | 3 | Robu.in | ~₹40 ea | Buy the BLUE module — see §2.5. |
| U5 | AMS1117-3.3 SOT-223 LDO | 5V → 3V3 LDO (if not using ESP32 dev kit) | 4 | Robu.in | ~₹15 ea | |
| U6 | CP2102 USB-UART (if you go bare ESP32 module on custom PCB) | USB programming + power | 2 | Robu.in | ~₹120 ea | Phase 3 PCB only. |
| Q1 | NMOS 2N7002 SOT-23 | OLED enable / boot-strapping | 2 | Robu.in | ~₹3 ea | Optional, for hard OLED power gating. |

### 4.2 Passives

| Ref | Part | Description | Qty | Supplier | INR @ qty | Notes |
|---|---|---|---|---|---|---|
| Multiple | 0603 ceramic | 100 nF, 1 µF, 10 µF X7R 6V3 | 50 ea | Robu.in 0603 kit | ₹500 total kit | One kit covers years. |
| R_pull | 10 kΩ 1 % 0603 | I2C pull-ups (× 2), GPIO pull-ups | 30 | Robu.in 0603 kit | (in kit) | |
| R_input | 4.7 kΩ 1 % 0603 | I2C pull-ups for high-speed (Phase 6 stretch) | 10 | Robu.in 0603 kit | (in kit) | |
| L_ferrite | BLM18 0603 ferrite bead | I2C bus noise filter | 4 | Robu.in | ~₹4 ea | Improves IMU noise floor. |

### 4.3 Power + connectors

| Ref | Part | Description | Qty | Supplier | INR @ qty | Notes |
|---|---|---|---|---|---|---|
| BATT | 502035 LiPo 350 mAh (or 503035 500 mAh) | Single-cell LiPo | 2 | Robu.in / Indiamart | ~₹220 (350 mAh), ~₹280 (500 mAh) | 502035 is thinner; 503035 lasts longer. Pick per enclosure thickness. |
| J_BATT | JST-PH 2.0 mm 2-pin | LiPo connector | 4 | Robu.in | ~₹15 ea | |
| J_USB | USB-C 16-pin SMD | Charge + flash | 4 | LCSC | ~₹40 ea | |
| J_SWD | 4-pin 1.27 mm SMD | ESP-PROG debug | 4 | Robu.in | ~₹15 ea | |
| SW_BOOT | Tactile 4×4 mm SMT | BOOT button (GPIO 0) | 4 | Robu.in | ~₹5 ea | |
| SW_RESET | Tactile 4×4 mm SMT | RESET | 4 | Robu.in | ~₹5 ea | |
| FB_USB | TVS USB-C diode array | USB ESD | 4 | LCSC | ~₹10 ea | |

### 4.4 Mechanical / enclosure

| Item | Spec | Where | INR |
|---|---|---|---|
| PETG filament 1 kg spool | eSun PETG, 1.75 mm, transparent or black | Amazon.in | ~₹1,100 (lasts years) |
| Wrist strap | 20 mm silicone with steel buckle | Amazon.in / Croma | ~₹250 |
| Tactile pads | 3M VHB 4910, 1 mm thick | Amazon.in | ~₹150 (sheet) |
| Heat-set inserts M2 brass | For threaded enclosure assembly | Robu.in | ~₹250 (pack of 50) |
| M2 screws 6 mm | Stainless | Robu.in | ~₹120 (pack of 100) |

### 4.5 PCB fabrication

| Service | Qty | Layers | Size | Assembly | INR delivered (Pune/Goa) |
|---|---|---|---|---|---|
| **JLCPCB economic assy.** | 5 | 2 | 35×30 mm | Top-side, leaded paste, basic parts | ~₹1,200 PCB + ~₹2,200 PCBA + ~₹500 shipping = **~₹3,900 total** ([JLCPCB pricing]) |
| PCBPower (Indian) | 5 | 2 | 35×30 mm | None | ~₹1,800 PCB only — ~3 day delivery in India ([PCBPower vs JLC]) |

**Recommendation: JLCPCB with assembly** of cheap passives + USB-C + TP4056, hand-solder the ESP32 module + MPU-6050 yourself. The MPU-6050 LGA package is hand-solderable with a hot-air rework station; the ESP32-WROOM-32 module has thermal-relief castellated edges that solder cleanly with a regular iron.

### 4.6 Spares strategy

For a ₹3,000 BOM, 100 % spares on the cheap parts (TP4056, MPU-6050, OLED) is overkill but trivial — they're ₹150 each. Get the 0603 + 1206 assortment kits from Robu.in (~₹500 each) — they are reusable across all future projects.

---

## 5. Tools and test equipment

These are **lifetime tools**. Most overlap with the rest of the portfolio.

### 5.1 Soldering

| Item | Spec | Where | INR |
|---|---|---|---|
| Hot-air rework station | Yihua 853D-II | Robu.in ([Yihua 853D India]) | ~₹6,500 |
| Iron tips | T12 clone | Robu.in | ~₹150 each |
| Solder paste | Mechanic XG-50, leaded for hand-rework | Robu.in | ~₹400 |
| Flux | AMTECH RMA-223 clone | Robu.in | ~₹250 |
| Tweezers | ESD-safe SS | Robu.in | ~₹150 |
| PCB holder | PanaVise clone | Robu.in | ~₹400 |
| ESP-PROG (USB debugger + UART for ESP-IDF) | Espressif official, or clone | Robu.in / LCSC | ~₹500 (clone) |

### 5.2 Probing

| Item | Spec | Where | INR |
|---|---|---|---|
| Oscilloscope | Rigol DS1054Z 50 MHz 4 ch | Robu.in ([Rigol DS1054Z India]) | ~₹40,000 |
| Logic analyzer | 8-ch Saleae clone, 24 MHz | Robu.in ([Robu Saleae clone]) | ~₹600 |
| Multimeter | Mastech MS8268 / UNI-T UT139C | Robu.in | ~₹2,500 |
| USB power meter | KM003C / FNB48S | Aliexpress | ~₹1,800 |
| **USB power meter is critical** for Phase 6 — without it you cannot measure idle current below 10 mA reliably. |

### 5.3 3D printing

| Item | Spec | Where | INR |
|---|---|---|---|
| 3D printer | Ender 3 V2 / Bambu Lab A1 mini | Amazon.in / Robu.in | ~₹18,000 (Ender) – ₹30,000 (Bambu) |
| **Alternative**: borrow access at the BITS Pilani Goa fab lab | | | ₹0 |

### 5.4 Software

| Tool | Use | Cost |
|---|---|---|
| ESP-IDF v5.4 LTS | Firmware build | Free |
| KiCad 8 | Schematic + PCB | Free |
| Fusion 360 | Enclosure (student license) | Free |
| Python 3.11 + numpy + scipy + pandas + matplotlib | Phase 0 algorithm analysis | Free |
| Jupyter notebook | Algorithm walk-through | Free |
| SensorLogger Android app | Phase 0 phone data capture | Free |
| Phyphox Android/iOS | Alternative phone IMU capture | Free |

---

## 6. Reference designs and learning resources

### 6.1 Reference projects to study

| Project | What to copy | What to avoid |
|---|---|---|
| **Pebble Watch** (now archived) | Wrist UX patterns; the original Pebble was an STM32 + SSD1306 (!) wearable. | Don't try to reproduce all features. |
| **Bangle.js 2** | Espruino + nRF52840 + memory LCD; open-source firmware. | Bangle uses JavaScript on-device — fun, but not a resume win for a C-firmware role. |
| **PineTime** | nRF52832 + ST7789 LCD + 180 mAh LiPo; full open hardware. | The ST7789 is colorful but battery-hungry. |
| **Watchy** (Squarewave Dot Industries) | ESP32 + e-paper; tutorials cover power optimization in depth. | Watchy uses e-paper, our project uses OLED. |
| **STM32F4 step counter** by ARM Mbed example | Algorithm reference. | Old (2016) algorithm — adaptive threshold has moved on. |

### 6.2 Algorithms (must-read)

1. **Mladenov & Mock — "A Step Counter Service for Java-Enabled Devices Using a Built-in Accelerometer"** — the canonical 2009 paper on the magnitude-peak-detection step counter. [DOI 10.1145/1542301.1542317].
2. **Brajdic & Harle — "Walk detection and step counting on unconstrained smartphones"** (UbiComp 2013) — the survey paper that benchmarks 14 step-counting algorithms. The "PeakDetection + dynamic threshold" winner is what the scaffold implements.
3. **Google Fit step counting algorithm (open-source via TensorFlow Lite Micro examples)** — for the Phase 7 stretch where you swap the hand-tuned threshold for a TFLite model.
4. **MPU-6050 register map (InvenSense, RM-MPU-6000A-00)** — the canonical reference for the FIFO and DLPF settings.

### 6.3 YouTube / blogs

1. **Andreas Spiess — "ESP32 Pedometer"** — full walk-through with battery measurement. [Andreas Spiess YT].
2. **Atomic14 — "Watchy + ESP32 wearables"** — power optimization deep-dive. [atomic14 YT].
3. **Engineer Bo — "Cheap accelerometer step counter"** — the friendly intro to the algorithm.

### 6.4 Community

- **ESP32 Forum** — `Hardware → MPU-6050 + I2C` for FIFO-mode debugging.
- **r/esp32** subreddit — active, friendly to wearable questions.
- **Stack Exchange Electronics** — for low-power LiPo charger questions.

---

## 7. Risk register

| # | Risk | Probability | Impact | Mitigation |
|---|---|---|---|---|
| 1 | **Arm-swing false positives** while walking with the arm hanging loose | High | Step count overshoots by 10 – 30 % | Tune `k_threshold` *on the wrist* (Phase 2). Add gyro cross-check (Phase 7 stretch). |
| 2 | **Slow-walk under-trigger** (< 80 SPM cadences) | Med | Misses ~20 % of steps for slow walkers | Add separate low-cadence detector (Phase 7) — peak detection with lower threshold and longer refractory. |
| 3 | **MPU-6050 sampling jitter** > 10 % | Med | Algorithm timing drift | Verify 100 Hz sample rate with a GPIO toggle + scope in Phase 1. |
| 4 | **OLED burn-in** after months of static display | Low | Cosmetic | Rotate display orientation every 12 h; auto-off after 60 s of no motion. |
| 5 | **LiPo over-discharge** (red TP4056 module) | Med if you use red module | Permanent battery damage | Buy blue TP4056 modules (with DW01 BMS) — see §2.5. |
| 6 | **Wrist coupling rattle** | High | False steps | Use 3M VHB inside the enclosure to lock the PCB; no floating IMU. |
| 7 | **PCB customs delay** | Med | Schedule slip | Order 2 weeks early; PCBPower as backup. |
| 8 | **ESP32 brown-out** during Wi-Fi tx | N/A — Wi-Fi disabled for MVP | — | — |
| 9 | **Heat-set inserts loose** in PETG | Low | Enclosure rattle | Use a soldering iron at 220 °C for 5 s to bed inserts properly. |
| 10 | **Wear-test skin irritation** | Med | Demo unwearable | Use medical-grade silicone strap, not nylon. |
| 11 | **MPU-6050 stock shortage** (post-COVID supply chain) | Low (huge inventory) | — | Specify GY-521 board with MPU-9250 footprint alternate. |
| 12 | **OLED ribbon cable break** | Med (wear-and-tear) | Demo failure | Cover the ribbon with VHB; strain-relieve at the PCB connector. |

### Top 3 risks (highlighted)

1. **Arm-swing false positives.** This is the #1 reason cheap step counters fail. Mitigated by adaptive threshold *and* gyro cross-check (Phase 7).
2. **Wrist coupling.** If the IMU doesn't move *with* the wrist, you get noise. VHB tape solves it.
3. **TP4056 red vs. blue.** A red TP4056 module is missing the DW01 BMS. **Always buy blue.**

---

## 8. Test and verification plan

### 8.1 Software-only tests (no hardware needed)

The scaffold has no Python tests yet. Add:

1. **Unit tests on the step detector** (`python_analysis/test_step_detector.py`). Feed the 5 reference recordings; assert count error ≤ ±3 % on each.
2. **Threshold sweep test**: assert that `k_threshold = 1.2` is the global optimum across all 5 recordings.
3. **False-positive test**: assert that the typing-at-keyboard recording produces ≤ 5 detected steps.
4. **C-to-Python parity test**: run the Python reference and the C-as-shared-library version on the same data; assert they match within 1 step.

### 8.2 Bench bring-up tests (in order)

| # | Test | Equipment | Pass criterion |
|---|---|---|---|
| 1 | **3V3 rail integrity** (custom PCB) | Multimeter | 3.27 V – 3.33 V |
| 2 | **I2C bus scan** | ESP-IDF i2c-tools | 0x68 + 0x3C present |
| 3 | **MPU-6050 WHO_AM_I** | Firmware | Reads 0x68, 0x70, or 0x72 |
| 4 | **SSD1306 init** | Visual | OLED shows splash screen |
| 5 | **100 Hz sample rate** | Scope on GPIO toggle | 10 ms ± 0.1 ms period |
| 6 | **TP4056 charge** | USB power meter + battery | RED LED while charging, GREEN when 4.20 V |
| 7 | **Battery discharge cutoff** | Discharge to ≤ 2.5 V (use 100 Ω load) | DW01 disconnects load |
| 8 | **Step detection** | 100-step hand-counted walk | Display shows 95 – 105 |
| 9 | **Cadence reading** | Pace counter | Cadence converges to ±5 SPM of true |
| 10 | **NVS persistence** | Reboot mid-walk | Count survives reset |
| 11 | **Wrist-tilt wake** | Tilt the device | OLED wakes within 200 ms |
| 12 | **24-h battery soak** | Wear for 24 h | ≥ 30 % remaining charge |

### 8.3 Wear-test procedure

| Day | Activity | Expected steps | Acceptable range |
|---|---|---|---|
| 1 | Normal office day | 5,000 – 8,000 | ±5 % of phone-counted reference |
| 2 | Active day (walk to campus + lab) | 8,000 – 12,000 | ±5 % |
| 3 | Run day (5 km) | 6,000 – 7,000 | ±5 % |
| 4 | Sedentary day (writing this BUILD_PLAN) | 1,500 – 3,000 | ±10 % (low cadence) |
| 5 | Stair-climb test (20 floors) | 200 ± 20 | Count includes the stairs |

### 8.4 Fault injection tests

| Fault | Method | Expected result |
|---|---|---|
| Power glitch | Disconnect USB during write | Last-saved count survives (NVS flushed at 60 s intervals) |
| IMU unplug | Yank the MPU-6050 ribbon | Firmware logs error, OLED shows "IMU lost" |
| OLED unplug | Yank the OLED ribbon | Firmware continues counting silently; resumes display when reconnected |
| LiPo over-discharge | Run to 2.5 V | DW01 BMS disconnects load; device powers off |
| Mechanical shock (drop test 1 m) | Drop on carpet | Enclosure intact; no false steps recorded |

---

## 9. Cost summary

All prices INR, sourced May 2026. "Conservative" column adds 50 % spares for the cheap parts.

### 9.1 BOM (one device)

| Category | Item | Conservative INR |
|---|---|---|
| ESP32 module + MPU-6050 + OLED | (bare module variant for PCB) | 750 |
| TP4056 + LiPo | Charger + 500 mAh cell | 350 |
| Passives + connectors | 0603 kit + USB-C + JST | 250 |
| **BOM subtotal** | | **1,350** |

### 9.2 PCB fabrication

| Item | INR |
|---|---|
| JLCPCB 2-layer × 5 + economic PCBA (passives + USB-C + TP4056) + shipping | 3,900 |

### 9.3 Mechanical

| Item | INR |
|---|---|
| PETG filament (consumed: ~30 g) + strap + heat-set inserts | 350 |

### 9.4 Lab tools (one-time, lifetime use)

| Item | INR |
|---|---|
| Soldering kit (iron + paste + flux + tweezers + holder + ESP-PROG) | 8,000 |
| Multimeter | 2,500 |
| Logic analyzer (Saleae clone) | 600 |
| USB power meter | 1,800 |
| 3D printer (Ender 3 V2 — borrow if possible) | 18,000 |
| Oscilloscope (Rigol DS1054Z if not owned) | 40,000 |
| **Lab tools subtotal** | **70,900** |

### 9.5 Grand total

| Scenario | Total INR |
|---|---|
| **Project-only (lab tools already owned, dev kit only)** | **~₹2,500** |
| **Project + PCB fab + mechanical** | ~₹5,600 |
| **Project + missing soldering + USB meter + multimeter** | ~₹15,000 |
| **All-in (cold start, including new 3D printer + scope)** | ~₹85,000 |

Most realistic for a BITS student: **₹3,000 – ₹4,500** assuming access to the institute's lab equipment and fab lab 3D printers.

---

## 10. Stretch goals (after MVP works)

These are explicitly **post-MVP**. Don't touch them until Phase 7 passes.

### 10.1 Gyro cross-check for arm-swing rejection
Currently the algorithm fires on any acceleration peak. A real gait cycle has a consistent angular-velocity signature on the wrist (yaw + roll pattern). Adding a gyro template-match drops false positives ~80 %. ~3 days of work.

### 10.2 TFLite Micro step model
Train a tiny CNN (1D conv + max-pool + dense) on the Phase-0 recordings; deploy via TensorFlow Lite for Microcontrollers. Outperforms hand-tuned threshold across activities. Reuses the ML-training infrastructure already established in [nilm-energy-monitor]. ~2 weeks.

### 10.3 BLE GATT + iOS/Android companion app
Push step count + cadence via BLE GATT (Heart Rate-style profile). Use an off-the-shelf companion (nRF Connect, lightblue) for demo; bonus: write a small React Native or Flutter app. ~1 week.

### 10.4 Sleep tracking
Detect long periods of low motion + low cadence → infer sleep. Output sleep duration on a daily basis. ~3 days.

### 10.5 Stair counting
A barometer (BMP280, ~₹200) adds altitude. Detect 3 m / minute altitude gain → count as stair flight. ~2 days.

### 10.6 Wireless OTA updates over Wi-Fi
Enable Wi-Fi-on-button-press; download a new firmware from a hard-coded GitHub Releases URL; flash via OTA. ~2 days.

### 10.7 Hardware pedometer comparison
Swap the MPU-6050 for an LSM6DSO (`U2` footprint alternate on the PCB) and run *both* the firmware algorithm and the LSM6DSO's built-in step counter side-by-side. Document the comparison in `docs/algorithm_comparison.md`. ~1 week. **Best resume bullet of any stretch goal.**

---

## 11. Resume bullet drafts

Two bullets in the style typical of EI/ECE candidates targeting hard-engineering roles.

> **Designed and built a wrist-worn step counter from custom PCB to wearable enclosure: 35 × 30 mm 2-layer KiCad board hosting an ESP32-WROOM-32, MPU-6050 6-axis IMU, 0.96" SSD1306 OLED, TP4056 LiPo charger with DW01 BMS, and USB-C charge/flash interface; ESP-IDF firmware (FreeRTOS) sampling at 100 Hz with adaptive-threshold peak detection on `|a| − 1g`, achieving ±3 % step-count error against hand-counted reference walks across slow / normal / brisk / jog cadences.** Tuned the algorithm constants in Python against ~7,000 reference steps recorded with a phone IMU, then ported line-for-line into C with `pytest` parity tests.

> **Optimized battery life to 24+ hours on a 500 mAh single-cell LiPo through MPU-6050 FIFO batching, ESP32 light-sleep between FIFO drains, wrist-tilt OLED wake-up via the MPU-6050 motion interrupt, and per-component duty cycling (SSD1306 contrast ramp + display-off below 60 s of motion idle).** Designed a 3D-printed PETG enclosure (Fusion 360 parametric model), characterized arm-swing false-positive rejection across typing / driving / stationary scenarios (≤ 5 false steps / 5 minutes), and documented the measured battery-life budget against a USB power meter.

---

## 12. References

Numbered references used in this build plan, ordered by first appearance.

1. [Espressif ESP32-WROOM-32E datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-wroom-32e_esp32-wroom-32ue_datasheet_en.pdf)
2. [InvenSense MPU-6050 product page](https://invensense.tdk.com/products/motion-tracking/6-axis/mpu-6050/)
3. [InvenSense MPU-6050 register map (RM-MPU-6000A-00)](https://invensense.tdk.com/wp-content/uploads/2015/02/MPU-6000-Register-Map1.pdf)
4. [Solomon Systech SSD1306 datasheet](https://cdn-shop.adafruit.com/datasheets/SSD1306.pdf)
5. [TP4056 datasheet (NanJing TopPower)](https://dlnmh9ip6v2uc.cloudfront.net/datasheets/Prototyping/TP4056.pdf)
6. [DW01A LiPo protection IC datasheet](https://datasheetspdf.com/datasheet/DW01A.html)
7. [Brajdic & Harle — "Walk detection and step counting on unconstrained smartphones" (UbiComp 2013)](https://dl.acm.org/doi/10.1145/2493432.2493449)
8. [Mladenov & Mock — "A Step Counter Service for Java-Enabled Devices Using a Built-in Accelerometer" (2009)](https://dl.acm.org/doi/10.1145/1542301.1542317)
9. [ESP-IDF v5.4 Power Management API](https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32/api-reference/system/power_management.html)
10. [ESP-IDF v5.4 Light Sleep Guide](https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32/api-reference/system/sleep_modes.html)
11. [Bangle.js 2 open-source smartwatch project](https://www.espruino.com/Bangle.js2)
12. [PineTime open-source smartwatch project](https://wiki.pine64.org/wiki/PineTime)
13. [Watchy ESP32 e-paper smartwatch](https://watchy.sqfmi.com/)
14. [Andreas Spiess YT — ESP32 wearables](https://www.youtube.com/c/AndreasSpiess)
15. [atomic14 YT — Watchy + ESP32](https://www.youtube.com/c/atomic14)
16. [Robu.in — ESP32-WROOM-32 dev kit](https://robu.in/product/esp32-development-board-wifi-bluetooth-ultra-low-power-consumption-dual-cores-unsoldered/)
17. [Robu.in — GY-521 MPU-6050 breakout](https://robu.in/product/mpu-6050-3-axis-accelerometer-gyroscope-module/)
18. [Robu.in — SSD1306 0.96" OLED](https://robu.in/product/0-96-inch-yellow-blue-iic-i2c-oled-128x64-displaybluish-area-yellow-area/)
19. [Robu.in — TP4056 BLUE LiPo charger module](https://robu.in/product/tp4056-1a-li-ion-battery-charging-board-micro-usb-with-current-protection-type-c/)
20. [Robu.in — Saleae 24 MHz logic analyzer clone](https://robu.in/product/usb-logic-analyze-24m-8ch-mcu-arm-fpga-dsp-debug-tool/)
21. [Robu.in — Yihua 853D rework station](https://robu.in/brand/yihua/)
22. [JLCPCB PCB assembly pricing](https://jlcpcb.com/help/article/pcb-assembly-price)
23. [JLCPCB customs and taxes (India context)](https://jlcpcb.com/help/article/customs,-duties-and-taxes)
24. [PCBPower (Indian PCB house)](https://www.pcbpower.com/)
25. [Eremit LiPo battery brand (India retailer)](https://robu.in/brand/eremit/)
26. [SensorLogger (Android IMU recording app)](https://play.google.com/store/apps/details?id=com.kelvin.sensorapp)
27. [Phyphox (Android/iOS sensor app)](https://phyphox.org/)
28. [TensorFlow Lite for Microcontrollers — ESP-IDF integration](https://github.com/espressif/esp-tflite-micro)
29. [Bourns 3296W multi-turn pot (Mouser India)](https://www.mouser.in/ProductDetail/Bourns/3296W-1-103LF)
30. [QuartzComponents (Indian supplier)](https://quartzcomponents.com/)

---

*End of build plan. Last edit: 2026-05-28.*
