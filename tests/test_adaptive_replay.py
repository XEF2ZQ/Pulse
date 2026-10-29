"""Deterministic candidate-replay checks; no machine policy changes."""
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location(
    "replay", Path(__file__).resolve().parents[1] / "tools/replay_adaptive.py")
replay = importlib.util.module_from_spec(spec)
spec.loader.exec_module(replay)


def trace(interval=250):
    return [{"time_ms": str(t), "cpu_one_lp_percent": str(95 if 2000 <= t < 5000 else 0),
             "compute_label": str(int(2000 <= t < 5000))}
            for t in range(interval, 10001, interval)]


class ReplayTests(unittest.TestCase):
    def test_elapsed_evidence_and_release(self):
        for interval in (50, 100, 250, 500, 1000):
            rows = trace(interval)
            result = replay.replay(rows, "baseline")
            self.assertEqual(result["transitions"], 2)
            self.assertEqual(result["samples"], len(rows))
            self.assertEqual(sum(result["confusion"].values()), len(rows))
            self.assertEqual(len(result["compute_onset_ms"]), 1)
            self.assertGreaterEqual(result["compute_onset_ms"][0], 500)
            self.assertLessEqual(result["compute_onset_ms"][0], 2 * interval + 500)

    def test_candidates_are_deterministic(self):
        for model in ("baseline", "ewma", "cusum"):
            self.assertEqual(replay.replay(trace(), model), replay.replay(trace(), model))
            self.assertEqual(replay.replay([], model)["transitions"], 0)

    def test_rejects_bad_trace(self):
        for value in ("nan", "inf", "-1"):
            rows = trace()
            rows[2]["cpu_one_lp_percent"] = value
            with self.assertRaises(ValueError):
                replay.replay(rows, "baseline")
        for value in ("nan", "inf", "0", "250"):
            rows = trace()
            rows[2]["time_ms"] = value
            with self.assertRaises(ValueError):
                replay.replay(rows, "baseline")
        with self.assertRaises(ValueError):
            replay.replay(trace(), "typo")


if __name__ == "__main__":
    unittest.main()
