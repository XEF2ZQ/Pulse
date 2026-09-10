"""Compare an explicitly selected component sensor with reported battery recovery.

Never identifies CPU package power from a guessed label, substitutes power limits
for measured power, or treats absent/stale readings as evidence of recovery.
"""
import argparse
import csv
import json
import math
import statistics
from pathlib import Path

from analyze_tail import analyze


def read_csv(path):
    with path.open(encoding="utf-8-sig", newline="") as stream:
        return list(csv.DictReader(stream))


def sensor_key(row):
    return tuple(int(row[k]) for k in ("sensor_id", "instance", "reading_id"))


def recovery_delay(points, end, threshold, max_gap):
    """Require three seconds supported by actual fresh samples, without filling gaps."""
    tail = [p for p in points if p[0] >= end]
    for index, point in enumerate(tail):
        if point[1] > threshold:
            continue
        last = point[0]
        for later in tail[index + 1:]:
            if later[0] - last > max_gap or later[1] > threshold:
                break
            last = later[0]
            if last - point[0] >= 3:
                return point[0] - end
    return None


def compare(folder, identity, margin=1.0, ceiling=12.0):
    if not math.isfinite(margin) or margin <= 0 or not math.isfinite(ceiling) or ceiling <= 0:
        raise ValueError("Margin and ceiling must be positive finite watts")
    catalog = [r for r in read_csv(folder / "sensor-catalog.csv") if sensor_key(r) == identity]
    if len(catalog) != 1 or catalog[0]["unit"] != "W":
        raise ValueError("Select exactly one catalog sensor with unit W")
    # A caller still must verify the sensor's meaning against the live Sensors table.
    battery, rows, start, end = analyze(folder)
    if battery["source_changed"]:
        raise ValueError("Power source changed or was unknown during capture")
    metadata = dict(line.split("=", 1) for line in
                    (folder / "metadata.txt").read_text().splitlines() if "=" in line)
    producer_ms = float(metadata.get("hwinfo_period_ms", "0"))
    if not math.isfinite(producer_ms) or producer_ms <= 0 or producer_ms > 5000:
        raise ValueError("Producer polling interval is unavailable or outside comparison bounds")
    # Includes timestamp's one-second quantization. This is an evidence tolerance,
    # not a promised hardware refresh period or a request to poll more frequently.
    max_gap = max(2.1, producer_ms / 1000 * 2 + 1)
    by_elapsed = {r["elapsed_s"]: r for r in rows}
    points = []
    previous_poll = None
    for r in read_csv(folder / "hwinfo.csv"):
        if sensor_key(r) != identity:
            continue
        t, value, poll = float(r["elapsed_s"]), float(r["value"]), int(r["poll_time"])
        sample = by_elapsed.get(t)
        if not math.isfinite(t) or not math.isfinite(value) or value < 0 or r["unit"] != "W":
            raise ValueError("Invalid selected sensor value or unit")
        if sample is None or sample["hwinfo_error"] != "0":
            raise ValueError("Selected frame has no successful aligned acquisition")
        age = float(sample["hwinfo_poll_age_s"])
        if not math.isfinite(age) or not 0 <= age <= max_gap:
            raise ValueError("Selected frame is stale or has inconsistent wall-clock time")
        if int(sample["hwinfo_poll_time"]) != poll or (previous_poll is not None and poll <= previous_poll):
            raise ValueError("Producer timestamp repeated, reversed or disagreed with acquisition")
        if points and t <= points[-1][0]:
            raise ValueError("Capture time is not strictly increasing")
        previous_poll = poll
        points.append((t, value))
    relevant = [p for p in points if start - 15 <= p[0] <= end + 30]
    if not relevant or relevant[0][0] > start - 15 + max_gap or relevant[-1][0] < end + 30 - max_gap:
        raise ValueError("Insufficient component coverage before/after work")
    if any(b[0] - a[0] > max_gap for a, b in zip(relevant, relevant[1:])):
        raise ValueError("Component capture contains a gap; recovery is not established")
    baseline = [v for t, v in relevant if t < start]
    load = [v for t, v in relevant if start <= t < end]
    recovery = [(t, v) for t, v in relevant if t >= end]
    if len(baseline) < 3 or len(load) < 2 or len(recovery) < 4:
        raise ValueError("Insufficient independent producer frames")
    reference = statistics.median(baseline)
    threshold = reference + margin
    excited = max(load) > threshold
    settled = recovery_delay(relevant, end, threshold, max_gap) if excited else None
    return {
        "sensor": catalog[0],
        "scope": "Selected component sensor, not instantaneous whole-system power",
        "component_baseline_median_w": reference,
        "component_load_peak_w": max(load),
        "recovery_threshold_w": threshold,
        "component_excited_above_threshold": excited,
        "component_sustained_recovery_after_end_s": settled,
        "component_recovery_peak_w": max(v for _, v in recovery),
        "candidate_ceiling_w": ceiling,
        "recovery_frames_above_ceiling": sum(v > ceiling for _, v in recovery),
        "recovery_frames": len(recovery),
        "reported_battery_below_8w_after_end_s": battery["reported_discharge_below_8w_s"],
        "freshness_tolerance_s": max_gap,
        "interpretation": (
            "Counts are sensor observations, not saved watts or time-weighted energy. "
            "A ceiling above observed power does not establish an opportunity to reduce it. "
            "This report does not authorize or enable a hardware actuator."
        ),
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path)
    parser.add_argument("--sensor", nargs=3, type=int, required=True,
                        metavar=("SENSOR_ID", "INSTANCE", "READING_ID"))
    parser.add_argument("--margin-w", type=float, default=1.0)
    parser.add_argument("--ceiling-w", type=float, default=12.0)
    args = parser.parse_args()
    try:
        result = compare(args.capture, tuple(args.sensor), args.margin_w, args.ceiling_w)
    except (ValueError, KeyError, OSError) as error:
        parser.exit(2, f"Component comparison unavailable: {error}\n")
    destination = args.capture / "component-analysis.json"
    destination.write_text(json.dumps(result, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2, allow_nan=False))
