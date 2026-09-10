"""Synthetic component evidence tests; never communicates with hardware."""
import csv
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from component_tail import compare, recovery_delay


class ComponentTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.folder = Path(self.temp.name)
        self.identity = (1, 0, 2)
        self.rows = [dict(elapsed_s=float(t), hwinfo_error="0", hwinfo_poll_age_s="0.25",
                          hwinfo_poll_time=str(1000 + t)) for t in range(80)]
        self.frames = [dict(elapsed_s=str(t), sensor_id="1", instance="0", reading_id="2",
                            unit="W", value=str(20 if 30 <= t < 39 else 5),
                            poll_time=str(1000 + t)) for t in range(80)]
        self.catalog = [dict(sensor_id="1", instance="0", reading_id="2", unit="W",
                             label="CPU Package Power", sensor="Synthetic CPU", current="5")]
        self.battery = dict(source_changed=False, reported_discharge_below_8w_s=28.5)
        self.mock = patch("component_tail.analyze", return_value=(self.battery, self.rows, 30, 38))
        self.mock.start()
        self.addCleanup(self.mock.stop)
        (self.folder / "metadata.txt").write_text("hwinfo_period_ms=1000\n")

    def run_compare(self):
        for name, rows in [("sensor-catalog.csv", self.catalog), ("hwinfo.csv", self.frames)]:
            with (self.folder / name).open("w", newline="") as stream:
                writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
                writer.writeheader()
                writer.writerows(rows)
        return compare(self.folder, self.identity)

    def test_prompt_component_recovery_separate_from_battery(self):
        result = self.run_compare()
        self.assertEqual(result["component_sustained_recovery_after_end_s"], 1)
        self.assertEqual(result["reported_battery_below_8w_after_end_s"], 28.5)
        self.assertEqual(result["recovery_frames_above_ceiling"], 1)

    def test_wrong_identity_or_unit(self):
        for key, value in [("sensor_id", "7"), ("unit", "%")]:
            original = self.catalog[0][key]
            self.catalog[0][key] = value
            with self.assertRaises(ValueError):
                self.run_compare()
            self.catalog[0][key] = original

    def test_stale_reading(self):
        self.rows[40]["hwinfo_poll_age_s"] = "100"
        with self.assertRaises(ValueError):
            self.run_compare()

    def test_producer_timestamp_disagreement(self):
        self.frames[40]["poll_time"] = "1001"
        with self.assertRaises(ValueError):
            self.run_compare()

    def test_gap_rejected(self):
        self.frames = [r for r in self.frames if not 39 <= int(r["elapsed_s"]) < 48]
        with self.assertRaises(ValueError):
            self.run_compare()

    def test_ac_invalidates_comparison(self):
        self.battery["source_changed"] = True
        with self.assertRaises(ValueError):
            self.run_compare()

    def test_flat_sensor_is_not_proof_of_fast_recovery(self):
        for r in self.frames:
            r["value"] = "5"
        result = self.run_compare()
        self.assertFalse(result["component_excited_above_threshold"])
        self.assertIsNone(result["component_sustained_recovery_after_end_s"])

    def test_missing_end_coverage(self):
        self.frames = self.frames[:50]
        with self.assertRaises(ValueError):
            self.run_compare()

    def test_nonfinite_value(self):
        self.frames[40]["value"] = "nan"
        with self.assertRaises(ValueError):
            self.run_compare()

    def test_recovery_needs_continuous_evidence(self):
        self.assertIsNone(recovery_delay([(38, 5), (60, 5)], 38, 6, 3))
        self.assertIsNone(recovery_delay([(38, 5), (39, 5)], 38, 6, 3))


if __name__ == "__main__":
    unittest.main()
