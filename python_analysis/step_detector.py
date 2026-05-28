"""Reference Python implementation of the step detector.

This is a line-for-line port of the firmware's `firmware/main/step_detector.c`
so that algorithm changes can be prototyped in Python (against recorded IMU
data) and copied back to C with confidence that the behaviour matches. The
parity is locked in by `test_step_detector.py::test_c_python_parity_*`.

Algorithm (mirrors `step_detector.c`):
    1. mag    = sqrt(ax² + ay² + az²) − 1   (gravity-removed magnitude in g)
    2. lp     = first-order IIR low-pass at `lp_cutoff_hz`
    3. mean, std over a sliding window of `hist_seconds` of past `lp` samples
    4. threshold = mean + k_threshold * std
    5. detect a step when:
         lp > threshold
         AND time since last step ≥ min_step_interval_ms
         AND history buffer is at least half-full
    6. cadence = 16 * 60 / (now − oldest_of_last_16_step_times)
"""

from __future__ import annotations

import dataclasses
import math
from typing import Iterable, List, Tuple


@dataclasses.dataclass
class StepConfig:
    sample_rate_hz: float = 100.0
    lp_cutoff_hz: float = 5.0
    min_step_interval_ms: int = 250
    k_threshold: float = 1.2
    hist_seconds: float = 2.0
    # Minimum-energy gate. If the rolling std of the low-passed magnitude is
    # below this value the device is considered stationary and no step is
    # emitted. Defaults to ~0.05 g which is well above the IMU noise floor
    # (~4 mg) but well below a real footstrike (~0.3-0.6 g). See
    # BUILD_PLAN.md §3 Phase 2 and §7 risk #2.
    min_std_g: float = 0.05


@dataclasses.dataclass
class StepState:
    step_count: int = 0
    cadence_spm: float = 0.0


class StepDetector:
    def __init__(self, cfg: StepConfig):
        self.cfg = cfg
        dt = 1.0 / cfg.sample_rate_hz
        rc = 1.0 / (2.0 * math.pi * cfg.lp_cutoff_hz)
        self._lp_alpha = dt / (rc + dt)
        self._lp_state = 0.0
        self._hist_len = max(1, int(cfg.hist_seconds * cfg.sample_rate_hz))
        self._hist: List[float] = []
        self._hist_idx = 0
        self._last_step_t = -1e9  # seconds; force first step to be allowed
        self._step_times: List[float] = []  # rolling window of last 16
        self._state = StepState()
        self._t = 0.0

    def reset(self) -> None:
        self._lp_state = 0.0
        self._hist = []
        self._hist_idx = 0
        self._last_step_t = -1e9
        self._step_times = []
        self._state = StepState()
        self._t = 0.0

    def _push_hist(self, v: float) -> None:
        if len(self._hist) < self._hist_len:
            self._hist.append(v)
        else:
            self._hist[self._hist_idx] = v
            self._hist_idx = (self._hist_idx + 1) % self._hist_len

    def _stats(self) -> Tuple[float, float]:
        n = len(self._hist)
        if n == 0:
            return 0.0, 0.0
        s = sum(self._hist)
        m = s / n
        ss = sum((v - m) ** 2 for v in self._hist)
        return m, math.sqrt(ss / n)

    def update(self, ax: float, ay: float, az: float) -> bool:
        """Feed one accelerometer sample (in g). Returns True on a detected step."""
        mag = math.sqrt(ax * ax + ay * ay + az * az) - 1.0
        self._lp_state += self._lp_alpha * (mag - self._lp_state)

        mean, std = self._stats()
        self._push_hist(self._lp_state)

        threshold = mean + self.cfg.k_threshold * std
        dt = self._t - self._last_step_t
        dt_required = self.cfg.min_step_interval_ms / 1000.0

        step = False
        if (
            self._lp_state > threshold
            and std > self.cfg.min_std_g
            and dt > dt_required
            and len(self._hist) > self._hist_len // 2
        ):
            step = True
            self._state.step_count += 1
            self._last_step_t = self._t
            self._step_times.append(self._t)
            if len(self._step_times) > 16:
                self._step_times.pop(0)

        if len(self._step_times) >= 16:
            window = self._t - self._step_times[0]
            if window > 0:
                self._state.cadence_spm = 16.0 * 60.0 / window
        else:
            self._state.cadence_spm = 0.0

        self._t += 1.0 / self.cfg.sample_rate_hz
        return step

    @property
    def state(self) -> StepState:
        return self._state


def count_steps(
    accel_xyz: Iterable[Tuple[float, float, float]],
    cfg: StepConfig | None = None,
) -> int:
    """Convenience: run the detector across an iterable of (ax, ay, az) samples
    and return the total step count."""
    det = StepDetector(cfg or StepConfig())
    for ax, ay, az in accel_xyz:
        det.update(ax, ay, az)
    return det.state.step_count
