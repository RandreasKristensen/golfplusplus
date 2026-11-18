"""Regression checks for suspicious pond carving in imported elevation data."""

import unittest

import osm_checks
import osm_ground


class WaterShoreTests(unittest.TestCase):
    def check(self, rise=10.0, reviews=None, outline=None):
        world = {"ground": {"origin": [0, 0], "cell_size": 1,
                            "columns": 5, "rows": 5,
                            "heights_cm": osm_ground.height_rows_cm(
                                [x * rise / 4 for z in range(5) for x in range(5)], 5)}}
        placed = [{"number": 1, "zones": [{"type": "water", "center": (2, 2),
                    "outline": outline or [[0, 0], [4, 0], [4, 4], [0, 4]]}]}]
        findings = osm_checks.Findings()
        tol = {**osm_checks.DEFAULT_TOLERANCES, "reviewed_water_shores": reviews or {}}
        osm_checks._check_water_shores(findings, placed, world, tol)
        return findings

    def test_crater_warns_with_measurement_and_location(self):
        item, = self.check().items
        self.assertEqual(item["code"], "WATER_SHORE_RELIEF")
        self.assertEqual(item["level"], "warn")
        self.assertEqual(item["hole"], 1)
        self.assertIn("10.0 m", item["message"])
        self.assertIn("(2.0, 2.0)", item["message"])

    def test_level_and_gently_sloping_shores_pass(self):
        self.assertFalse(self.check(0).items)
        self.assertFalse(self.check(1).items)

    def test_review_requires_reason_and_expires_when_elevations_change(self):
        key = self.check().items[0]["message"].split("review id ")[1]
        self.assertEqual(self.check(reviews={key: "Verified retaining wall"}).count("warn"), 0)
        self.assertEqual(self.check(reviews={key: " "}).count("warn"), 1)
        self.assertEqual(self.check(12, {key: "Verified retaining wall"}).count("warn"), 1)

    def test_outside_grid_is_flagged(self):
        item, = self.check(outline=[[-1, 0], [4, 0], [4, 4]]).items
        self.assertEqual(item["code"], "WATER_SHORE_COVERAGE")


if __name__ == "__main__":
    unittest.main()
