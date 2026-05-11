# Step-detection algorithm

## Why peak detection on |a| - 1g

Each footstrike produces a brief vertical-axis acceleration spike. Subtracting gravity (`|a| - 1g`) removes the static tilt component and leaves a roughly zero-mean signal whose peaks correspond to footstrikes. This is robust to:

* Wrist orientation (the IMU sees a similar peak no matter how the wrist rotates)
* Walking vs. running cadence (changes the peak rate but not the peak shape)

## Adaptive threshold

A fixed threshold fails as soon as the user changes activity intensity. Instead, we compute the rolling mean and standard deviation of the filtered magnitude over the last 2 seconds and set:

```
threshold = mean + 1.2 * std
```

This adapts to the user's current cadence and amplitude. The `1.2` factor was tuned empirically against ~200 hand-counted reference steps. Lower → more sensitive (more false positives), higher → less sensitive (more missed steps).

## Refractory period

A 250 ms inter-step minimum corresponds to a cadence ceiling of 240 SPM, well above the fastest sustained human running cadence (~210 SPM). This excludes the small overshoot peaks that appear immediately after a strong footstrike.

## Pitfalls observed during testing

* **Arm swinging without walking** — driving, typing — produces sub-threshold oscillations and is correctly rejected.
* **Tapping the wrist** registers as false steps; future revisions could cross-check the gyro for the consistent angular-velocity pattern of a real gait cycle.
* **Slow walks** (< 80 SPM) under-trigger because the smoothed magnitude no longer crosses the adaptive band; consider a separate low-cadence detector for accessibility.
