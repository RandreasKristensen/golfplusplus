import argparse
import math
import sys
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))
import osm_golf_convert as conv


def way(osm_id, tags, coords):
    return {
        "type": "way",
        "id": osm_id,
        "tags": tags,
        "geometry": [{"lat": lat, "lon": lon} for lat, lon in coords],
    }


def node(osm_id, tags, lat, lon):
    return {"type": "node", "id": osm_id, "tags": tags, "lat": lat, "lon": lon}


class OsmGolfConvertTests(unittest.TestCase):
    def test_name_search_ranks_exact_relation_over_first_result(self):
        args = argparse.Namespace(name="Pine Hills Golf Club", lat=None, lon=None)
        candidates = [
            way(2, {"leisure": "golf_course", "name": "Pine Hills Disc Golf"}, [(0, 0), (0, 0.01), (0.01, 0.01), (0.01, 0)]),
            {"type": "relation", "id": 1, "tags": {"leisure": "golf_course", "name": "Pine Hills Golf Club"},
             "members": [{"role": "outer", "geometry": [{"lat": 0, "lon": 0}, {"lat": 0, "lon": 0.02}, {"lat": 0.02, "lon": 0.02}, {"lat": 0.02, "lon": 0}]}]},
        ]

        ranked = conv._rank_course_candidates(candidates, args)

        self.assertEqual(("relation", 1), conv._element_key(ranked[0]))

    def test_lat_lon_zero_is_valid_and_ranks_containing_course(self):
        args = argparse.Namespace(name=None, lat=0.0, lon=0.0)
        far = way(2, {"leisure": "golf_course", "name": "Far"}, [(1, 1), (1, 1.01), (1.01, 1.01), (1.01, 1)])
        near = way(1, {"leisure": "golf_course", "name": "Near"}, [(-0.01, -0.01), (-0.01, 0.01), (0.01, 0.01), (0.01, -0.01)])

        ranked = conv._rank_course_candidates([far, near], args)

        self.assertEqual(1, ranked[0]["id"])

    def test_longitude_distance_uses_latitude_cosine_scale(self):
        equator = conv._latlon_distance_m(0.0, 0.0, 0.0, 0.001)
        latitude_60 = conv._latlon_distance_m(60.0, 0.0, 60.0, 0.001)

        self.assertAlmostEqual(equator, 111.32, delta=1.0)
        self.assertAlmostEqual(latitude_60, equator * 0.5, delta=1.0)

    def test_course_footprint_filter_rejects_neighboring_bbox_contamination(self):
        course = way(1, {"leisure": "golf_course", "name": "Selected"}, [(0, 0), (0, 0.01), (0.01, 0.01), (0.01, 0)])
        own_hole = way(10, {"golf": "hole", "ref": "1"}, [(0.002, 0.002), (0.008, 0.008)])
        neighbor_hole = way(20, {"golf": "hole", "ref": "1"}, [(0.04, 0.04), (0.05, 0.05)])

        self.assertTrue(conv._element_in_course_footprint(own_hole, course))
        self.assertFalse(conv._element_in_course_footprint(neighbor_hole, course))

    def test_fetch_elements_splits_trees_and_filters_mocked_overpass_payload(self):
        course = way(1, {"leisure": "golf_course", "name": "Selected"}, [(0, 0), (0, 0.01), (0.01, 0.01), (0.01, 0)])
        own_hole = way(10, {"golf": "hole", "ref": "1"}, [(0.002, 0.002), (0.008, 0.008)])
        neighbor_hole = way(20, {"golf": "hole", "ref": "1"}, [(0.04, 0.04), (0.05, 0.05)])
        tree = node(30, {"natural": "tree"}, 0.005, 0.005)

        with mock.patch.object(conv, "_query", return_value={"elements": [own_hole, neighbor_hole, tree]}):
            golf, trees, paths = conv._fetch_elements(course)

        self.assertEqual([10], [el["id"] for el in golf])
        self.assertEqual([30], [el["id"] for el in trees])
        self.assertEqual([], paths)

    def test_fetch_elements_includes_course_scoped_paths(self):
        course = way(1, {"leisure": "golf_course", "name": "Selected"}, [(0, 0), (0, 0.01), (0.01, 0.01), (0.01, 0)])
        cartpath = way(40, {"golf": "cartpath"}, [(0.002, 0.002), (0.003, 0.003)])
        service = way(41, {"highway": "service"}, [(0.004, 0.004), (0.005, 0.005)])
        neighbor = way(42, {"highway": "path"}, [(0.04, 0.04), (0.05, 0.05)])

        with mock.patch.object(conv, "_query", return_value={"elements": [cartpath, service, neighbor]}):
            golf, trees, paths = conv._fetch_elements(course)

        self.assertEqual([40], [el["id"] for el in golf])
        self.assertEqual([], trees)
        self.assertEqual([40, 41], [el["id"] for el in paths])

    def test_tree_assignment_excludes_fairway_core_and_keeps_side_trees(self):
        origin_lat = 56.0
        origin_lon = 10.0
        hole_line = way(1, {"golf": "hole", "ref": "1"}, [(56.0, 10.0), (56.001, 10.0)])
        holes = conv.group_holes([hole_line])
        side_lat = origin_lat + 40.0 / conv.EARTH_METERS_PER_DEGREE_LAT
        core_lat = origin_lat + 50.0 / conv.EARTH_METERS_PER_DEGREE_LAT
        side_lon = origin_lon + 30.0 / (conv.EARTH_METERS_PER_DEGREE_LAT * math.cos(math.radians(origin_lat)))
        trees = [
            node(100, {"natural": "tree"}, core_lat, origin_lon),
            node(101, {"natural": "tree"}, side_lat, side_lon),
        ]

        conv.assign_trees_to_holes(holes, trees, origin_lat, origin_lon, "course:1")

        self.assertEqual(1, len(holes[1]["trees_abs"]))
        self.assertGreater(abs(holes[1]["trees_abs"][0][0]), 20.0)

    def test_wooded_polygon_sampling_is_deterministic(self):
        wood = way(30, {"natural": "wood"}, [(56.0, 10.0), (56.0, 10.001), (56.001, 10.001), (56.001, 10.0)])

        first = conv._sample_wooded_polygon_trees(wood, 56.0, 10.0, 1234)
        second = conv._sample_wooded_polygon_trees(wood, 56.0, 10.0, 1234)

        self.assertEqual(first, second)
        self.assertGreater(len(first), 0)

    def test_scale_warnings_flag_implausible_hole(self):
        h_json = {
            "pin": [0.0, 0.0, 20.0],
            "spline": {"control_points": [[0, 0, 0], [0, 0, 20]], "width": 2.0},
        }

        warnings = conv._scale_warnings(h_json)

        self.assertGreaterEqual(len(warnings), 2)

    def test_course_world_uses_shared_coordinates_and_osm_paths(self):
        origin_lat = 56.0
        origin_lon = 10.0
        hole_1 = way(1, {"golf": "hole", "ref": "1"}, [(56.0, 10.0), (56.001, 10.0)])
        tee_1 = node(2, {"golf": "tee", "ref": "1"}, 56.0, 10.0)
        pin_1 = node(3, {"golf": "pin", "ref": "1"}, 56.001, 10.0)
        hole_2 = way(4, {"golf": "hole", "ref": "2"}, [(56.001, 10.001), (56.002, 10.001)])
        service = way(5, {"highway": "service"}, [(56.0, 10.0), (56.001, 10.001)])
        footway = way(6, {"highway": "footway"}, [(56.001, 10.0), (56.001, 10.001)])
        holes = conv.group_holes([hole_1, tee_1, pin_1, hole_2])

        world = conv.course_world_to_json("test_course",
                                          "Test Course",
                                          way(9, {"leisure": "golf_course"}, [(56.0, 10.0), (56.002, 10.002)]),
                                          holes,
                                          [service, footway],
                                          origin_lat,
                                          origin_lon)

        self.assertEqual(2, len(world["hole_starts"]))
        self.assertEqual(0, world["hole_starts"][0]["hole_index"])
        self.assertNotEqual(world["hole_starts"][0]["position"], world["hole_starts"][1]["position"])
        self.assertEqual(1, len(world["cart_roads"]))
        self.assertEqual(1, len(world["walking_shortcuts"]))
        self.assertEqual("fitness", world["walking_shortcuts"][0]["required_skill_id"])
        self.assertGreaterEqual(len(world["collectibles"]), 2)
        self.assertTrue(any(item.get("repeatable") for item in world["collectibles"]))
        self.assertTrue(any(item.get("requirement", {}).get("skill_id") == "fitness" for item in world["collectibles"]))

    def test_course_world_generates_fallback_roads_when_osm_roads_are_absent(self):
        origin_lat = 56.0
        origin_lon = 10.0
        holes = conv.group_holes([
            way(1, {"golf": "hole", "ref": "1"}, [(56.0, 10.0), (56.001, 10.0)]),
            way(2, {"golf": "hole", "ref": "2"}, [(56.001, 10.001), (56.002, 10.001)]),
        ])

        world = conv.course_world_to_json("test_course",
                                          "Test Course",
                                          way(9, {"leisure": "golf_course"}, [(56.0, 10.0), (56.002, 10.002)]),
                                          holes,
                                          [],
                                          origin_lat,
                                          origin_lon)

        self.assertEqual(2, len(world["hole_starts"]))
        self.assertEqual(1, len(world["cart_roads"]))
        self.assertEqual("generated_fallback", world["cart_roads"][0]["source"])
        self.assertGreaterEqual(len(world["cart_roads"][0]["polyline"]), 5)


if __name__ == "__main__":
    unittest.main()
