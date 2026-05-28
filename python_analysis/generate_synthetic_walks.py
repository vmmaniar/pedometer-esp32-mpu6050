"""Synthesize labelled accelerometer traces for offline algorithm tuning.

Each generated trace is a tuple `(samples_xyz, true_step_count, metadata)`,
where `samples_xyz` is a numpy array of shape (N, 3) of (ax, ay, az) in g.

The synthesis is intentionally simple: a periodic "footstrike" impulse plus
sinusoidal arm-swing plus Gaussian sensor noise plus gravity in the +z
direction. It is *not* a substitute for real recordings (which you should
collect in Phase 0 of BUILD_PLAN.md), but it makes the test suite hermetic
and gives the algorithm something concrete to chew on in CI.
"""

from __future__ import annotations

import dataclasses
from typing import Tuple

import numpy as np


@dataclasses.dataclass
class WalkParams:
    duration_s: float = 60.0
    sample_rate_hz: float = 100.0
    cadence_spm: float = 110.0       # steps per minute
    footstrike_amp_g: float = 0.6    # vertical impulse amplitude
    armswing_amp_g: float = 0.1      # baseline AC modulation
    noise_g: float = 0.02
    seed: int | None = 42


def synthesize_walk(p: WalkParams) -> Tuple[np.ndarray, int]:
    """Return (samples_xyz, true_step_count)."""
    rng = np.random.default_rng(p.seed)
    n = int(p.duration_s * p.sample_rate_hz)
    t = np.arange(n) / p.sample_rate_hz
    step_period = 60.0 / p.cadence_spm
    true_step_count = int(p.duration_s / step_period)

    # Footstrike: narrow Gaussian impulses spaced at the cadence interval.
    az = np.ones(n)  # gravity baseline
    step_sigma = step_period / 12.0  # narrow pulse, ~50 ms at 110 SPM
    for k in range(true_step_count):
        t_step = (k + 0.5) * step_period
        az += p.footstrike_amp_g * np.exp(-0.5 * ((t - t_step) / step_sigma) ** 2)

    # Arm-swing: ±10 % vertical at half the cadence (alternating arms).
    az += p.armswing_amp_g * np.sin(2 * np.pi * (p.cadence_spm / 120.0) * t)

    # Lateral axes: smaller arm-swing component, mostly noise.
    ax = p.armswing_amp_g * 0.5 * np.cos(2 * np.pi * (p.cadence_spm / 120.0) * t)
    ay = p.armswing_amp_g * 0.3 * np.sin(2 * np.pi * (p.cadence_spm / 60.0) * t)

    # Noise
    ax += rng.normal(0.0, p.noise_g, size=n)
    ay += rng.normal(0.0, p.noise_g, size=n)
    az += rng.normal(0.0, p.noise_g, size=n)

    samples = np.column_stack([ax, ay, az])
    return samples, true_step_count


def synthesize_stationary(duration_s: float = 60.0,
                          sample_rate_hz: float = 100.0,
                          noise_g: float = 0.02,
                          seed: int | None = 0) -> Tuple[np.ndarray, int]:
    """Sitting still — pure noise + gravity. True step count is 0."""
    rng = np.random.default_rng(seed)
    n = int(duration_s * sample_rate_hz)
    ax = rng.normal(0.0, noise_g, size=n)
    ay = rng.normal(0.0, noise_g, size=n)
    az = 1.0 + rng.normal(0.0, noise_g, size=n)
    return np.column_stack([ax, ay, az]), 0


def synthesize_typing(duration_s: float = 60.0,
                      sample_rate_hz: float = 100.0,
                      tap_rate_hz: float = 4.0,
                      tap_amp_g: float = 0.15,
                      noise_g: float = 0.02,
                      seed: int | None = 1) -> Tuple[np.ndarray, int]:
    """Wrist typing motion — small periodic taps, no real steps.

    True step count is 0; this is the algorithm's false-positive test.
    """
    rng = np.random.default_rng(seed)
    n = int(duration_s * sample_rate_hz)
    t = np.arange(n) / sample_rate_hz
    tap_signal = tap_amp_g * np.sin(2 * np.pi * tap_rate_hz * t)
    ax = tap_signal * 0.5 + rng.normal(0.0, noise_g, size=n)
    ay = tap_signal * 0.3 + rng.normal(0.0, noise_g, size=n)
    az = 1.0 + tap_signal * 0.2 + rng.normal(0.0, noise_g, size=n)
    return np.column_stack([ax, ay, az]), 0
