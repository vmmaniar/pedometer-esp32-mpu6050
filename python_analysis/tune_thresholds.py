"""Sweep `k_threshold` against synthesized + recorded walks and find the optimum.

This script is the BUILD_PLAN.md Phase 0 / Phase 2 deliverable. Run it to
re-tune the algorithm when you have new recordings. Outputs a per-recording
error table to stdout and (optionally) a matplotlib plot.

Usage:
    python tune_thresholds.py                       # uses synthesized walks
    python tune_thresholds.py --recordings *.csv    # uses real CSVs (t,ax,ay,az)
    python tune_thresholds.py --plot                # save plot to threshold_sweep.png
"""

from __future__ import annotations

import argparse
import csv
import dataclasses
from pathlib import Path
from typing import List, Tuple

import numpy as np

from step_detector import StepConfig, count_steps
from generate_synthetic_walks import (
    WalkParams,
    synthesize_walk,
    synthesize_stationary,
    synthesize_typing,
)


@dataclasses.dataclass
class Recording:
    name: str
    samples: np.ndarray   # (N, 3)
    true_count: int


def _synthetic_recordings() -> List[Recording]:
    recs = []
    for cadence, name in [(80, "slow_walk"), (110, "normal_walk"),
                          (140, "brisk_walk"), (170, "jog")]:
        samples, true_count = synthesize_walk(WalkParams(duration_s=60.0, cadence_spm=cadence))
        recs.append(Recording(name=name, samples=samples, true_count=true_count))
    stat, _ = synthesize_stationary(duration_s=60.0)
    recs.append(Recording(name="stationary", samples=stat, true_count=0))
    typ, _ = synthesize_typing(duration_s=60.0)
    recs.append(Recording(name="typing", samples=typ, true_count=0))
    return recs


def _load_csv_recording(path: Path) -> Recording:
    """Load a CSV with columns t,ax,ay,az and an optional 'true_count' in the
    filename (e.g. walk_1000steps.csv → true_count = 1000)."""
    samples = []
    with path.open() as f:
        reader = csv.DictReader(f)
        for row in reader:
            samples.append((float(row["ax"]), float(row["ay"]), float(row["az"])))
    samples = np.array(samples)
    # Extract the integer just before "steps" from the filename, if present.
    stem = path.stem.lower()
    true_count = 0
    for tok in stem.replace("_", " ").split():
        if tok.endswith("steps") and tok[:-5].isdigit():
            true_count = int(tok[:-5])
            break
    return Recording(name=path.stem, samples=samples, true_count=true_count)


def evaluate(rec: Recording, k_threshold: float) -> Tuple[int, float]:
    cfg = StepConfig(k_threshold=k_threshold)
    counted = count_steps(((x, y, z) for x, y, z in rec.samples), cfg)
    if rec.true_count > 0:
        err_pct = 100.0 * abs(counted - rec.true_count) / rec.true_count
    else:
        # For zero-true-count recordings, "error" is the count itself
        # expressed as a percentage of 1000-step target (just for plot scale).
        err_pct = counted * 100.0 / 1000.0
    return counted, err_pct


def main() -> None:
    p = argparse.ArgumentParser()
    p.add_argument("--recordings", nargs="*", type=Path, help="CSV recordings to load")
    p.add_argument("--plot", action="store_true")
    p.add_argument("--ks", nargs="*", type=float, default=None,
                   help="k_threshold values to sweep (default: 0.5..2.5 step 0.1)")
    args = p.parse_args()

    if args.recordings:
        recs = [_load_csv_recording(path) for path in args.recordings]
    else:
        recs = _synthetic_recordings()

    ks = args.ks or np.arange(0.5, 2.51, 0.1).round(2).tolist()
    rows: List[List[str]] = []
    header = ["k"] + [r.name for r in recs] + ["mean_err_pct"]
    rows.append(header)
    all_errs: List[List[float]] = []
    for k in ks:
        line = [f"{k:.2f}"]
        errs = []
        for r in recs:
            counted, err_pct = evaluate(r, k_threshold=k)
            line.append(f"{counted}/{r.true_count}({err_pct:.1f}%)")
            errs.append(err_pct)
        mean_err = sum(errs) / len(errs)
        line.append(f"{mean_err:.1f}")
        rows.append(line)
        all_errs.append(errs)

    # Print as a TSV
    col_widths = [max(len(str(row[i])) for row in rows) for i in range(len(rows[0]))]
    for row in rows:
        print("  ".join(str(row[i]).ljust(col_widths[i]) for i in range(len(row))))

    # Find the best k by mean error
    means = [sum(errs) / len(errs) for errs in all_errs]
    best_idx = means.index(min(means))
    print(f"\nBest k_threshold = {ks[best_idx]:.2f}  (mean err {means[best_idx]:.1f}%)")

    if args.plot:
        try:
            import matplotlib.pyplot as plt
        except ImportError:
            print("matplotlib not installed; skip --plot")
            return
        all_errs_a = np.array(all_errs)  # shape (n_ks, n_recs)
        fig, ax = plt.subplots(figsize=(8, 4))
        for i, r in enumerate(recs):
            ax.plot(ks, all_errs_a[:, i], label=r.name, marker=".")
        ax.set_xlabel("k_threshold")
        ax.set_ylabel("count error (%)")
        ax.set_title("Step-detector threshold sweep")
        ax.legend(loc="best", fontsize="small")
        ax.grid(True, alpha=0.3)
        out = "threshold_sweep.png"
        fig.tight_layout()
        fig.savefig(out, dpi=120)
        print(f"Saved plot to {out}")


if __name__ == "__main__":
    main()
