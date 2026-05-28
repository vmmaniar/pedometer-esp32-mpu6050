# Pedometer — ESP32 + MPU-6050 (Wearable Prototype)

A wrist-worn step counter built around an ESP32 and the InvenSense MPU-6050 6-axis IMU, with a 128x64 SSD1306 OLED status display. Firmware is ESP-IDF (C, FreeRTOS) with a custom MPU-6050 component and a peak-detection step counter that runs on the magnitude of acceleration. The same algorithm is also implemented in Python so it can be tuned against recorded walks before changes are copied back to firmware.

## Features

* 100 Hz sampling of all 6 IMU axes over I2C
* Peak detection on `|a| − 1g` with an **adaptive threshold + minimum-energy gate** (resists arm-swing false positives and pure-noise false positives)
* Step count, cadence, and battery level shown on OLED
* NVS-persisted step count, survives reboots
* Python algorithm port + synthetic walk generator + `pytest` suite for offline tuning
* 3D-printed enclosure files in `enclosure/`

## Quick start (no hardware required)

```bash
# Python algorithm + tests
cd python_analysis
pip install -r requirements.txt
pytest -v                          # 10 tests, all green
python tune_thresholds.py          # sweeps k_threshold against synthesized walks
python tune_thresholds.py --plot   # also saves threshold_sweep.png
```

## With hardware

```bash
cd firmware
idf.py set-target esp32
idf.py build flash monitor
```

## Hardware

```
              +-----------+
ESP32  ── SDA ── │ MPU-6050  │
       ── SCL ── │ + SSD1306 │ (shared I2C bus, 0x68 and 0x3C)
              +-----------+
       ── GPIO0 (BOOT)  optional button: reset step count
       ── 3V7  LiPo
```

| Ref | Part              | Description                       |
|-----|-------------------|-----------------------------------|
| U1  | ESP32-WROOM-32    | MCU                               |
| U2  | MPU-6050          | 6-axis IMU (I2C 0x68)             |
| U3  | SSD1306 0.96"     | OLED, I2C 0x3C                    |
| BATT| 502035 LiPo      | 350 mAh, 3.7 V                    |
| U4  | TP4056 (BLUE, with DW01 BMS) | Charge controller    |

## Step-detection algorithm

1. Compute `mag = sqrt(ax² + ay² + az²) - 1.0g` (gravity-removed magnitude)
2. Low-pass at 5 Hz with a first-order IIR
3. Maintain rolling mean + std over the last 2 s of the low-passed signal
4. Detect a peak when **all** of:
   * `lp > mean + 1.2 * std`        (adaptive threshold)
   * `std > 0.05 g`                  (minimum-energy gate, rejects pure noise)
   * `time_since_last_step ≥ 250 ms` (refractory period, < 240 SPM ceiling)
   * history buffer is at least half-full (cold-start protection)

The algorithm is implemented identically in [firmware/main/step_detector.c](firmware/main/step_detector.c) and [python_analysis/step_detector.py](python_analysis/step_detector.py); pytest tests in `python_analysis/test_step_detector.py` lock in the counting accuracy (5 % at normal/brisk/jog cadences, 10 % at slow walk) and the zero-false-positive behaviour on stationary + typing traces.

## Repository layout

```
firmware/                ESP-IDF v5.4 project
  main/
    app_main.c           Boot, NVS persistence, UI refresh
    step_detector.[ch]   Peak detection + adaptive threshold + energy gate
  components/mpu6050/    Custom I2C driver for the MPU-6050
  components/ssd1306/    OLED driver
python_analysis/         Python port + synthetic walks + pytest suite
  step_detector.py       Reference Python implementation
  generate_synthetic_walks.py
  tune_thresholds.py     Sweep k_threshold; CSV + plot
  test_step_detector.py  pytest suite
  recordings/            Drop real CSVs here for tuning against actual data
enclosure/               STL placeholder + design notes
docs/                    Algorithm theory
.github/workflows/       CI: Python tests + ESP-IDF firmware build
BUILD_PLAN.md            Full 5-week build plan with India-sourced BOM
```

## CI

GitHub Actions runs two jobs on every push:

1. **python-tests** — `python_analysis/` pytest suite + smoke-test of `tune_thresholds.py`.
2. **esp-idf-build** — compiles `firmware/` inside the official `espressif/idf:v5.4` container.

## License

MIT — see [LICENSE](LICENSE).
