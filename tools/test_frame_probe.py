import unittest

import frame_probe


def frame(value, width=8, height=8):
    return [[(value, value, value)] * width for _ in range(height)]


def patch(rows, x0, y0, x1, y1, value):
    for y in range(y0, y1):
        for x in range(x0, x1):
            rows[y][x] = (value, value, value)
    return rows


class TestFrameProbe(unittest.TestCase):
    def test_ink_centroid_follows_the_patch(self):
        rows = patch(frame(0), 3, 2, 5, 4, 40)
        background, mass, x, y = frame_probe.ink(rows, (0, 0, 8, 8))
        self.assertEqual(background, (0, 0, 0))
        self.assertAlmostEqual(mass, 160)
        self.assertAlmostEqual(x, 3.5)
        self.assertAlmostEqual(y, 2.5)

    def test_ink_mass_doubles_with_coverage(self):
        light = patch(frame(0), 3, 2, 5, 4, 40)
        heavy = patch(frame(0), 3, 2, 5, 4, 80)
        self.assertAlmostEqual(
            frame_probe.ink(heavy, (0, 0, 8, 8))[1],
            2 * frame_probe.ink(light, (0, 0, 8, 8))[1],
        )

    def test_contribution_removes_the_shared_background(self):
        full = patch(frame(20), 3, 2, 5, 4, 60)
        skip = patch(frame(20), 3, 2, 5, 4, 20)
        rows = frame_probe.contribution(full, skip, (0, 0, 8, 8))
        self.assertEqual(rows[2][3], (40, 40, 40))
        self.assertEqual(rows[0][0], (0, 0, 0))

    def test_diff_ratio_names_a_doubled_contribution(self):
        a_full = frame(20)
        b_full = frame(20)
        for step, (x, y) in enumerate(((3, 2), (4, 2), (3, 3), (4, 3))):
            value = 40 + 10 * step
            a_full[y][x] = (value + 20, value + 20, value + 20)
            b_full[y][x] = (2 * value + 20, 2 * value + 20, 2 * value + 20)
        a = frame_probe.contribution(a_full, frame(20), (0, 0, 8, 8))
        b = frame_probe.contribution(b_full, frame(20), (0, 0, 8, 8))
        count, ratios, corr = frame_probe.diff_ratio(a, b)
        self.assertEqual(count, 4)
        self.assertAlmostEqual(ratios[0], 2.0, places=3)
        self.assertAlmostEqual(corr, 1.0, places=3)

    def test_diff_ratio_is_one_for_equal_contributions(self):
        full = frame(20)
        for step, (x, y) in enumerate(((3, 2), (4, 2), (3, 3), (4, 3))):
            value = 40 + 10 * step
            full[y][x] = (value + 20, value + 20, value + 20)
        rows = frame_probe.contribution(full, frame(20), (0, 0, 8, 8))
        count, ratios, corr = frame_probe.diff_ratio(rows, rows)
        self.assertEqual(count, 4)
        self.assertAlmostEqual(ratios[0], 1.0, places=3)
        self.assertAlmostEqual(corr, 1.0, places=3)


if __name__ == "__main__":
    unittest.main()
