"""Regression checks for erroneous flat-bottom inland DEM patches."""

import unittest
import numpy as np

import osm_terrain_quality as quality
import osm_outline


class TerrainQualityTests(unittest.TestCase):
    def test_zone_generation_uncrosses_edges_without_dropping_points(self):
        ring = [[0, 0], [4, 4], [0, 4], [4, 0]]
        result = osm_outline.zone_json("fairway", ring)["outline"]
        self.assertIsNone(osm_outline.crossing_edges(result))
        self.assertCountEqual(result, ring)
        self.assertEqual(abs(osm_outline.polygon_area(result)), 16)

    def test_concave_outline_keeps_its_shape(self):
        ring = [[0, 0], [4, 0], [4, 4], [2, 2], [0, 4]]
        self.assertEqual(osm_outline.untangle_ring(ring), ring)

    def test_repairs_plateau_and_interpolated_bad_edge_preserving_land(self):
        z, x = np.mgrid[:51, :51]
        land = 40 + x * .02 + z * .01
        bad = land.copy()
        bad[20:31, 20:31] = 5
        bad[19, 20:31] = 22
        reviewed = [{"position": [50, 50], "reach_m": 5, "reason": "Confirmed DEM error"}]
        repaired = np.array(quality.repair_dropouts(bad.ravel(), 51, 2, reviewed=reviewed)).reshape(51, 51)
        np.testing.assert_allclose(repaired, land, atol=1e-9)

    def test_unreviewed_pits_are_flagged_but_preserved(self):
        land = np.ones((51, 51)) * 40
        land[20:31, 20:31] = 5
        self.assertTrue(list(quality.dropout_masks(land, 2)))
        np.testing.assert_array_equal(quality.repair_dropouts(land.ravel(), 51, 2), land.ravel())

    def test_preserves_gentle_basin_and_real_bank(self):
        z, x = np.mgrid[:51, :51]
        for land in [np.maximum(0, np.hypot(x - 25, z - 25) - 10),
                     np.where(x < 25, 0., 10.), np.zeros((51, 51))]:
            repaired = quality.repair_dropouts(land.ravel(), 51, 2)
            np.testing.assert_array_equal(repaired, land.ravel())

    def test_large_cells_are_not_repaired(self):
        land = np.ones((51, 51)) * 40
        land[20:31, 20:31] = 5
        np.testing.assert_array_equal(quality.repair_dropouts(land.ravel(), 51, 20), land.ravel())


if __name__ == "__main__":
    unittest.main()
