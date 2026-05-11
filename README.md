# Pedometer — ESP32 + MPU-6050 (Wearable Prototype)

A wrist-worn step counter built around an ESP32 and the InvenSense MPU-6050 6-axis IMU, with a 128x64 SSD1306 OLED status display. Firmware is ESP-IDF (C, FreeRTOS) with a custom MPU-6050 component and a peak-detection step counter that runs on the magnitude of acceleration.

## Features

* 100 Hz sampling of all 6 IMU axes over I2C
* Peak-detection step counter with adaptive threshold (resists arm-swing false positives)
* Step count, cadence, and battery level shown on OLED
* 3D-printed enclosure files in `enclosure/`

## Hardware

```
              +-----------+
ESP32  ── SDA ── │ MPU-6050  │
       ── SCL ── │ + SSD1306 │ (shared I2C bus, 0x68 and 0x3C)
              +-----------+
       ── GPIO0 (BOOT)  optional button: reset step count
       ── 3V7  LiPo
```

| Ref       | Part              | Description                              |
|-----------|-------------------|------------------------------------------|
| U1        | ESP32-WROOM-32    | MCU                                      |
| U2        | MPU-6050          | 6-axis IMU (I2C 0x68)                    |
| U3        | SSD1306 0.96"     | OLED, I2C 0x3C                           |
| BATT      | 502035 LiPo 500mAh| Power source                             |
| U4        | TP4056            | Charge controller                        |

## Building the firmware

```bash
cd firmware
idf.py set-target esp32
idf.py build flash monitor
```

## Step-detection algorithm

1. Compute `a_mag = sqrt(ax² + ay² + az²) - 1.0g` (gravity-removed magnitude)
2. Low-pass at 5 Hz to suppress high-frequency motion
3. Detect peaks above an adaptive threshold (mean + 1.2 · std over the last 2 s)
4. Require a minimum inter-step interval of 250 ms (excludes >240 steps/min)

Tuning constants live in [firmware/main/step_detector.c](firmware/main/step_detector.c).

## Repository layout

```
firmware/
  main/                  Application + step detector
  components/mpu6050/    Custom I2C driver
  components/ssd1306/    OLED driver
enclosure/               STL placeholder + design notes
docs/                    Algorithm theory, test data
```
