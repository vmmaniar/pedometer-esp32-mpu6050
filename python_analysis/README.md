# Python step-detector analysis

A reference Python port of the firmware step detector, plus a synthetic walk
generator and a `pytest` suite. The Python port mirrors
[`firmware/main/step_detector.c`](../firmware/main/step_detector.c) line for line so
you can prototype algorithm changes here and copy them back to C with confidence.

## Quick start

```bash
pip install -r requirements.txt
pytest -v                              # algorithm tests
python tune_thresholds.py              # sweep k_threshold against synth walks
python tune_thresholds.py --plot       # also save threshold_sweep.png
```

## Files

| File                              | Purpose |
|-----------------------------------|---------|
| `step_detector.py`                | Python port of the C step detector (parity with `firmware/main/step_detector.c`) |
| `generate_synthetic_walks.py`     | Labelled trace generator: walk @ N SPM, stationary, typing |
| `tune_thresholds.py`              | Sweep `k_threshold`, print error table, optional plot |
| `test_step_detector.py`           | `pytest` suite — counting accuracy + false-positive rejection |
| `recordings/`                     | Drop real CSV recordings (`t,ax,ay,az`) here |

## Running against your own recordings

Capture a labelled IMU trace with [SensorLogger] or [Phyphox] (BUILD_PLAN.md Phase 0)
and save as `recordings/walk_1000steps.csv`. The filename's `<N>steps` token is
parsed for the ground-truth count. Then:

```bash
python tune_thresholds.py --recordings recordings/*.csv --plot
```

[SensorLogger]: https://play.google.com/store/apps/details?id=com.kelvin.sensorapp
[Phyphox]: https://phyphox.org/
