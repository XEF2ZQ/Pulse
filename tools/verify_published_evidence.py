"""Recompute the checked-in sanitized traces without changing repository files."""
import json
import math
import shutil
import tempfile
from pathlib import Path

from analyze_tail import analyze


def main():
    root = Path(__file__).resolve().parents[1] / "docs/evidence/2026-09-09"
    for name in ("cpu-raw-01", "cpu-raw-02"):
        source = root / name
        expected = json.loads((source / "analysis.json").read_text(encoding="utf-8"))
        with tempfile.TemporaryDirectory() as temporary:
            capture = Path(temporary) / name
            capture.mkdir()
            for filename in ("samples.csv", "events.csv"):
                shutil.copyfile(source / filename, capture / filename)
            actual, _, _, _ = analyze(capture)
        if expected.keys() != actual.keys():
            raise ValueError(f"{name}: result schema differs")
        for key, wanted in expected.items():
            observed = actual[key]
            matches = (math.isclose(observed, wanted, rel_tol=1e-9, abs_tol=1e-9)
                       if isinstance(wanted, float) else observed == wanted)
            if not matches:
                raise ValueError(f"{name}: {key}: expected {wanted!r}, got {observed!r}")
        print(f"PASS {name}: all published summary values reproduced")


if __name__ == "__main__":
    main()
