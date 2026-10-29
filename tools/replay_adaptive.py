"""Offline candidate comparison; no Windows calls or claims of energy savings.

Input is PulseAdaptiveProbe's known-worker phase trace. Candidate thresholds
are exploratory; held-out browser/media/render traces are required before
using a new filter to impose restrictions in the application.
"""
import csv
import json
import math
import statistics
import sys
from pathlib import Path


def replay(rows, model):
    if model not in ("baseline", "ewma", "cusum"):
        raise ValueError("Unknown model")
    fast = cumulative = 0.0
    high = quiet = None
    active = False
    previous = 0.0
    transitions = 0
    confusion = {"tp": 0, "fp": 0, "tn": 0, "fn": 0}
    delays = []
    phase_start = 0.0
    previous_label = False
    detected = False
    for row in rows:
        now, x = float(row["time_ms"]), float(row["cpu_one_lp_percent"])
        label = row["compute_label"] == "1"
        dt = now - previous
        if not math.isfinite(now) or dt <= 0 or not math.isfinite(x) or x < 0:
            raise ValueError("Invalid recorded sample")
        previous = now
        fast += -math.expm1(-dt / 250) * (x - fast)
        cumulative = min(500, max(0, cumulative + (x - 50) * dt / 1000))
        busy = x >= 80 if model == "baseline" else fast >= 75 if model == "ewma" else cumulative >= 20
        old = active
        if busy:
            quiet = None
            if high is None:
                high = now
            if now - high >= 500:
                active = True
        else:
            high = None
            release = x < 20 if model == "baseline" else fast < 20 if model == "ewma" else cumulative < 5
            if release:
                if quiet is None:
                    quiet = now
                if now - quiet >= 2000:
                    active = False
            else:
                quiet = None
        transitions += old != active
        if label and not previous_label:
            phase_start, detected = now - dt, False
        if label and active and not detected:
            delays.append(now - phase_start)
            detected = True
        previous_label = label
        confusion[("t" if active == label else "f") + ("p" if active else "n")] += 1
    return {"model": model, "confusion": confusion, "transitions": transitions,
            "compute_onset_ms": delays, "samples": len(rows)}


def summarize(folder):
    rows = list(csv.DictReader((folder / "workload-trace.csv").open()))
    result = {"scope": "controlled worker only; not calibrated for production workload mix",
              "models": [replay(rows, model) for model in ("baseline", "ewma", "cusum")],
              "timers": {}, "placement_ab": {}}
    counter = list(csv.DictReader((folder / "counter-samples.csv").open()))
    for interval in sorted({int(r["requested_ms"]) for r in counter}):
        subset = [r for r in counter if int(r["requested_ms"]) == interval]
        values = [float(r["elapsed_ms"]) - interval for r in subset]
        result["timers"][interval] = {"n": len(values), "mean_lateness_ms": statistics.mean(values),
                                      "stdev_ms": statistics.stdev(values) if len(values) > 1 else 0,
                                      "max_lateness_ms": max(values),
                                      "zero_cpu_samples": sum(float(r["process_cpu_ms"]) == 0 for r in subset)}
    ab = list(csv.DictReader((folder / "placement-ab.csv").open()))
    for mode in sorted({r["mode"] for r in ab}):
        values = [float(r["wall_ms"]) for r in ab if r["mode"] == mode]
        result["placement_ab"][mode] = {"n": len(values), "mean_ms": statistics.mean(values),
                                        "stdev_ms": statistics.stdev(values) if len(values) > 1 else 0}
    result["decision"] = "Retain existing threshold baseline; new restriction controls stay opt-in. These trials cannot satisfy energy/media/interactive acceptance gates."
    return result


if __name__ == "__main__":
    folder = Path(sys.argv[1])
    result = summarize(folder)
    (folder / "analysis.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))
