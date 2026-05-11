# Enclosure

3D-printed two-piece snap-fit case for the ESP32 dev board, MPU-6050 breakout, OLED, and 500 mAh LiPo. Designed in Fusion 360.

## Files

| File             | Description                              |
|------------------|------------------------------------------|
| `case_top.stl`   | Top shell with OLED window               |
| `case_bottom.stl`| Battery and PCB tray                     |
| `strap_loops.stl`| Optional accessory for a wrist strap     |

> **Note:** STL meshes are not committed yet. Generate from the parametric source (Fusion timeline) or substitute any 60 x 40 x 18 mm enclosure for early bring-up.

## Print settings

| Parameter   | Value          |
|-------------|----------------|
| Material    | PETG           |
| Layer       | 0.16 mm        |
| Infill      | 25 % gyroid    |
| Walls       | 3 perimeters   |
| Supports    | none required  |

## Wear-test notes

* Sand the inside of the OLED window with 1500-grit for better readability — un-sanded clear PETG fogs after a few days.
* Keep the IMU mounted directly to the PCB, not floating on wires; vibration coupling adds false steps.
