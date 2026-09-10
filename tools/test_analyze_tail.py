"""Analysis validity checks; no live telemetry or third-party dependencies."""
import math
import unittest

from analyze_tail import sustained_below


class RecoveryWindowTests(unittest.TestCase):
    def rows(self):
        return [dict(elapsed_s=i / 2, discharge_w=7.0, battery_error='0', ac_status='0')
                for i in range(7)]

    def test_continuous_recovery(self):
        self.assertEqual(sustained_below(self.rows(), 0, 8), 0)

    def test_does_not_bridge_invalid_or_ac_samples(self):
        for key, value in [('battery_error', '5'), ('ac_status', '1'),
                           ('discharge_w', math.nan), ('discharge_w', 12.0)]:
            with self.subTest(key=key, value=value):
                rows = self.rows()
                rows[3][key] = value
                self.assertIsNone(sustained_below(rows, 0, 8))

    def test_does_not_bridge_gaps(self):
        rows = self.rows()
        self.assertIsNone(sustained_below([rows[0], rows[-1]], 0, 8))

    def test_insufficient_recovery(self):
        self.assertIsNone(sustained_below(self.rows()[:4], 0, 8))

    def test_strict_threshold(self):
        self.assertIsNone(sustained_below(self.rows(), 0, 7))


if __name__ == '__main__':
    unittest.main()
