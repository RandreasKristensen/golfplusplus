import argparse
import math
import sys
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))
import osm_golf_convert as conv
import osm_ground as ground
import osm_elevation as elev
import osm_checks
import osm_contact_sheet
import osm_ellipse


def way(osm_id, tags, coords):
    return {
        "type": "way",
        "id": osm_id,
        "tags": tags,
        "geometry": [{"lat": lat, "lon": lon} for lat, lon in coords],
    }


def node(osm_id, tags, lat, lon):
    return {"type": "node", "id": osm_id, "tags": tags, "lat": lat, "lon": lon}


def hole_jsons_for(holes, origin_lat, origin_lon):
    return {num: conv.hole_to_json(num, h, origin_lat, origin_lon, "test_course") for num, h in holes.items()}


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

        conv.assign_trees_to_holes(holes, trees, origin_lat, origin_lon, "course:1", conv.DEFAULT_CONFIG["tree"])

        self.assertEqual(1, len(holes[1]["trees_abs"]))
        self.assertGreater(abs(holes[1]["trees_abs"][0][0]), 20.0)

    def test_wooded_polygon_sampling_is_deterministic(self):
        wood = way(30, {"natural": "wood"}, [(56.0, 10.0), (56.0, 10.001), (56.001, 10.001), (56.001, 10.0)])

        first = conv._sample_wooded_polygon_trees(wood, 56.0, 10.0, 1234, 150.0)
        second = conv._sample_wooded_polygon_trees(wood, 56.0, 10.0, 1234, 150.0)

        self.assertEqual(first, second)
        self.assertGreater(len(first), 0)

    def test_a_wood_is_planted_at_its_configured_density(self):
        # About 111 m x 62 m: 6900 m2, so about 46 trees at one per 150 m2.
        wood = way(30, {"natural": "wood"}, [(56.0, 10.0), (56.0, 10.001), (56.001, 10.001), (56.001, 10.0), (56.0, 10.0)])
        trees = conv._sample_wooded_polygon_trees(wood, 56.0, 10.0, 7, 150.0)
        self.assertTrue(35 <= len(trees) <= 60, len(trees))
        west_half = conv._sample_wooded_polygon_trees(wood, 56.0, 10.0, 7, 150.0, near=lambda p: p[0] < 31.0)
        self.assertTrue(all(p[0] < 31.0 for p in west_half))
        self.assertLess(len(west_half), len(trees))

    def test_a_wood_between_two_holes_plants_each_tree_once(self):
        lon_b = 10.0 + 120.0 / (conv.EARTH_METERS_PER_DEGREE_LAT * math.cos(math.radians(56.0)))
        holes = conv.group_holes([way(1, {"golf": "hole", "ref": "1"}, [(56.0, 10.0), (56.002, 10.0)]),
                                  way(2, {"golf": "hole", "ref": "2"}, [(56.0, lon_b), (56.002, lon_b)])])
        mid = (10.0 + lon_b) / 2
        wood = way(30, {"natural": "wood"}, [(56.0005, mid - 0.0003), (56.0005, mid + 0.0003),
                                             (56.0015, mid + 0.0003), (56.0015, mid - 0.0003), (56.0005, mid - 0.0003)])

        conv.assign_trees_to_holes(holes, [wood], 56.0, 10.0, "course:1", conv.DEFAULT_CONFIG["tree"])

        one, two = holes[1]["trees_abs"], holes[2]["trees_abs"]
        self.assertGreater(len(one), 0)
        self.assertGreater(len(two), 0)
        self.assertEqual(len(set(one) | set(two)), len(one) + len(two))

    def test_vegetation_is_fetched_beyond_the_course_boundary(self):
        course = way(9, {"leisure": "golf_course"}, [(56.0, 10.0), (56.0, 10.01), (56.01, 10.01), (56.0, 10.0)])
        with mock.patch.object(conv, "_query", return_value={"elements": []}) as query:
            conv._near_course_query(course, conv.VEGETATION_SELECTORS, 80.0)
        text = query.call_args[0][0]
        self.assertIn('node["natural"="tree"](area.courseArea);', text)
        self.assertIn('node["natural"="tree"](around.course:80);', text)

    def test_scale_warnings_flag_implausible_hole(self):
        h_json = {
            "pin": [0.0, 0.0, 20.0],
            "spline": {"control_points": [[0, 0, 0], [0, 0, 20]], "width": 2.0},
        }

        warnings = conv._scale_warnings(h_json)

        self.assertGreaterEqual(len(warnings), 2)

    def test_hole_generation_uses_configured_tree_sizes(self):
        origin_lat = 56.0
        origin_lon = 10.0
        hole_line = way(1, {"golf": "hole", "ref": "1"}, [(56.0, 10.0), (56.001, 10.0)])
        holes = conv.group_holes([hole_line])
        holes[1]["trees_abs"] = [(5.0, 8.0)]
        config = conv.load_generation_config(None)
        config["tree"] = {
            "trunk_radius": 0.9,
            "trunk_height": 7.0,
            "leaf_radius": 5.5,
            "leaf_height": 8.5,
            "max_per_hole": 20,
        }

        h_json = conv.hole_to_json(1, holes[1], origin_lat, origin_lon, "test_course", config)

        self.assertEqual(0.9, h_json["trees"][0]["trunk_radius"])
        self.assertEqual(7.0, h_json["trees"][0]["trunk_height"])
        self.assertEqual(5.5, h_json["trees"][0]["leaf_radius"])
        self.assertEqual(8.5, h_json["trees"][0]["leaf_height"])

    def test_course_world_uses_shared_coordinates_and_osm_paths(self):
        origin_lat = 56.0
        origin_lon = 10.0
        hole_1 = way(1, {"golf": "hole", "ref": "1"}, [(56.0, 10.0), (56.001, 10.0)])
        tee_1 = node(2, {"golf": "tee", "ref": "1"}, 56.0, 10.0)
        pin_1 = node(3, {"golf": "pin", "ref": "1"}, 56.001, 10.0)
        hole_2 = way(4, {"golf": "hole", "ref": "2"}, [(56.001, 10.001), (56.002, 10.001)])
        service = way(5, {"highway": "service"}, [(56.0, 10.0005), (56.001, 10.0005)])
        footway = way(6, {"highway": "footway"}, [(56.001, 10.0015), (56.002, 10.0015)])
        holes = conv.group_holes([hole_1, tee_1, pin_1, hole_2])

        world = conv.course_world_to_json("test_course",
                                          "Test Course",
                                          way(9, {"leisure": "golf_course"}, [(56.0, 10.0), (56.002, 10.002)]),
                                          holes,
                                          [service, footway],
                                          origin_lat,
                                          origin_lon,
                                          hole_jsons_for(holes, origin_lat, origin_lon))

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
                                          origin_lon,
                                          hole_jsons_for(holes, origin_lat, origin_lon))

        self.assertEqual(2, len(world["hole_starts"]))
        self.assertEqual(1, len(world["cart_roads"]))
        self.assertEqual("generated_fallback", world["cart_roads"][0]["source"])
        self.assertGreaterEqual(len(world["cart_roads"][0]["polyline"]), 5)

        corridors = []
        for num in sorted(holes.keys()):
            tee_xz, pin_xz = conv._hole_world_anchors(hole_jsons_for(holes, origin_lat, origin_lon)[num],
                                                      origin_lat, origin_lon)
            corridors.append(conv._hole_fairway_corridor(holes[num], tee_xz, pin_xz, origin_lat, origin_lon, conv.load_generation_config(None)))
        road_xz = [(p[0], p[2]) for p in world["cart_roads"][0]["polyline"]]
        self.assertFalse(conv._route_overlaps_fairway(road_xz, corridors))

    def test_course_world_rejects_paths_crossing_fairways(self):
        origin_lat = 56.0
        origin_lon = 10.0
        holes = conv.group_holes([
            way(1, {"golf": "hole", "ref": "1"}, [(56.0, 10.0), (56.001, 10.0)]),
        ])
        crossing_service = way(5, {"highway": "service"}, [(56.0, 10.0), (56.001, 10.0)])
        side_service = way(6, {"highway": "service"}, [(56.0, 10.0005), (56.001, 10.0005)])

        world = conv.course_world_to_json("test_course",
                                          "Test Course",
                                          way(9, {"leisure": "golf_course"}, [(56.0, 10.0), (56.002, 10.002)]),
                                          holes,
                                          [crossing_service, side_service],
                                          origin_lat,
                                          origin_lon,
                                          hole_jsons_for(holes, origin_lat, origin_lon))

        self.assertEqual(["cart_way_6"], [route["id"] for route in world["cart_roads"]])

    def test_course_world_places_hole_starts_at_tee_heights_relative_to_hole_one(self):
        holes = conv.group_holes([
            way(1, {"golf": "hole", "ref": "1"}, [(56.0, 10.0), (56.001, 10.0)]),
            way(2, {"golf": "hole", "ref": "2"}, [(56.001, 10.001), (56.002, 10.001)]),
            way(3, {"golf": "hole", "ref": "3"}, [(56.002, 10.002), (56.003, 10.002)]),
        ])
        hole_jsons = hole_jsons_for(holes, 56.0, 10.0)
        for num, height in ((1, 40.0), (2, 52.5), (3, None)):
            hole_jsons[num]["source"]["tee_elevation"] = height

        world = conv.course_world_to_json("test_course",
                                          "Test Course",
                                          way(9, {"leisure": "golf_course"}, [(56.0, 10.0), (56.003, 10.003)]),
                                          holes,
                                          [],
                                          56.0,
                                          10.0,
                                          hole_jsons)

        self.assertEqual([0.0, 12.5, 0.0], [start["position"][1] for start in world["hole_starts"]])

    def test_course_world_filters_long_outlying_osm_paths(self):
        origin_lat = 56.0
        origin_lon = 10.0
        holes = conv.group_holes([
            way(1, {"golf": "hole", "ref": "1"}, [(56.0, 10.0), (56.001, 10.0)]),
        ])
        near_short = way(5, {"highway": "footway"}, [(56.0001, 10.0005), (56.0002, 10.0005)])
        near_too_long = way(6, {"highway": "footway"}, [(56.0, 10.0005), (56.004, 10.0005)])
        far_short = way(7, {"highway": "footway"}, [(56.01, 10.01), (56.0102, 10.01)])
        config = conv.load_generation_config(None)
        config["world"]["max_shortcut_length"] = 80.0
        config["world"]["max_path_distance_from_holes"] = 60.0

        world = conv.course_world_to_json("test_course",
                                          "Test Course",
                                          way(9, {"leisure": "golf_course"}, [(56.0, 10.0), (56.002, 10.002)]),
                                          holes,
                                          [near_short, near_too_long, far_short],
                                          origin_lat,
                                          origin_lon,
                                          hole_jsons_for(holes, origin_lat, origin_lon),
                                          config)

        self.assertEqual(["shortcut_way_5"], [route["id"] for route in world["walking_shortcuts"]])


class CenterlineTests(unittest.TestCase):
    """
    The centreline is what makes a hole "to scale": it is the line a scorecard
    measures along, so getting its source and its endpoints right matters more
    than any other single decision in the converter.
    """

    def test_hole_way_is_preferred_over_the_fairway_polygon(self):
        # A dogleg. The hole way turns; slicing the fairway polygon along the
        # straight tee->pin axis would cut the corner off.
        line = [(0.0, 0.0), (0.0, 120.0), (60.0, 200.0)]
        fairway = [(-20.0, 0.0), (20.0, 0.0), (80.0, 200.0), (40.0, 220.0), (-20.0, 120.0)]
        tee, pin = (0.0, 0.0), (60.0, 200.0)

        ctrl, width, _rough, source = conv._hole_centerline(
            conv._empty_hole(), line, fairway, tee, pin, {})

        self.assertEqual("hole_way", source)
        # The dogleg must survive: some control point well off the tee->pin chord.
        chord_distance = max(conv._point_segment_distance(p, tee, pin) for p in ctrl)
        self.assertGreater(chord_distance, 15.0)
        # Width still comes from the polygon even though the line shaped the spline.
        self.assertGreater(width, 12.0)

    def test_centerline_endpoints_snap_to_tee_and_pin(self):
        line = [(3.0, 4.0), (1.0, 150.0), (2.0, 290.0)]
        tee, pin = (0.0, 0.0), (0.0, 300.0)

        ctrl, _w, _r, _src = conv._hole_centerline(conv._empty_hole(), line, [], tee, pin, {})

        self.assertAlmostEqual(tee[0], ctrl[0][0], places=3)
        self.assertAlmostEqual(tee[1], ctrl[0][1], places=3)
        self.assertAlmostEqual(pin[0], ctrl[-1][0], places=3)
        self.assertAlmostEqual(pin[1], ctrl[-1][1], places=3)

    def test_mismatched_hole_way_is_rejected_in_favour_of_the_fairway(self):
        # A way grouped onto the wrong hole: it goes nowhere near this pin.
        stray = [(500.0, 500.0), (520.0, 560.0)]
        fairway = [(-15.0, 0.0), (15.0, 0.0), (15.0, 300.0), (-15.0, 300.0)]

        _ctrl, _w, _r, source = conv._hole_centerline(
            conv._empty_hole(), stray, fairway, (0.0, 0.0), (0.0, 300.0), {})

        self.assertEqual("fairway", source)

    def test_control_point_count_scales_with_hole_length(self):
        short_hole = conv._control_point_count(140.0)
        long_hole = conv._control_point_count(550.0)

        self.assertLess(short_hole, long_hole)
        self.assertGreaterEqual(short_hole, 4)
        self.assertLessEqual(long_hole, 14)

    def test_fairway_width_survives_a_hole_way_centerline(self):
        line = [(0.0, 0.0), (0.0, 150.0), (0.0, 300.0)]
        fairway = [(-25.0, 0.0), (25.0, 0.0), (25.0, 300.0), (-25.0, 300.0)]
        config = {"fallback_width": 20.0, "rough_width_multiplier": 1.55}

        _ctrl, width, rough, source = conv._hole_centerline(
            conv._empty_hole(), line, fairway, (0.0, 0.0), (0.0, 300.0), config)

        self.assertEqual("hole_way", source)
        self.assertNotAlmostEqual(20.0, width, places=1)
        self.assertAlmostEqual(width * 1.55, rough, places=1)

    def test_two_point_hole_way_borrows_shape_from_the_fairway(self):
        """
        A bare tee->green segment has the right length but no shape. The
        fairway polygon knows where the hole bends, so it supplies the curve
        while the hole way keeps the endpoints.
        """
        line = [(0.0, 0.0), (60.0, 200.0)]
        bent_fairway = [(-15.0, 0.0), (15.0, 0.0), (35.0, 110.0),
                        (75.0, 200.0), (45.0, 210.0), (5.0, 120.0)]
        tee, pin = (0.0, 0.0), (60.0, 200.0)

        ctrl, _w, _r, source = conv._hole_centerline(
            conv._empty_hole(), line, bent_fairway, tee, pin, {})

        self.assertEqual("fairway_shape", source)
        self.assertAlmostEqual(tee[0], ctrl[0][0], places=3)
        self.assertAlmostEqual(pin[1], ctrl[-1][1], places=3)

    def test_shared_fairway_polygon_does_not_supply_shape(self):
        """
        A links double fairway is far too wide to be one hole's. Its centre
        runs down the gap between two holes, so it must not bend the spline.
        """
        line = [(0.0, 0.0), (0.0, 300.0)]
        double_fairway = [(-120.0, 0.0), (120.0, 0.0), (120.0, 300.0), (-120.0, 300.0)]

        _ctrl, width, _r, source = conv._hole_centerline(
            conv._empty_hole(), line, double_fairway, (0.0, 0.0), (0.0, 300.0),
            {"fallback_width": 20.0})

        self.assertEqual("hole_way", source)
        self.assertEqual(20.0, width)

    def test_straight_fallback_spans_tee_to_pin(self):
        ctrl, _w, _r, source = conv._hole_centerline(
            conv._empty_hole(), [], [], (0.0, 0.0), (10.0, 160.0), {})

        self.assertEqual("straight", source)
        self.assertEqual((0.0, 0.0), ctrl[0])
        self.assertAlmostEqual(10.0, ctrl[-1][0], places=3)
        self.assertAlmostEqual(160.0, ctrl[-1][1], places=3)


class ElevationTests(unittest.TestCase):
    def test_relative_profile_puts_the_tee_at_zero(self):
        absolute = elev.cleaned_profile([42.0, 45.0, 48.0], [0.0, 100.0, 200.0])
        profile = elev.relative_profile(absolute)

        self.assertGreater(absolute[0], 40.0)
        self.assertEqual(0.0, profile[0])
        self.assertGreater(profile[-1], 0.0)

    def test_gaps_are_interpolated_not_zeroed(self):
        self.assertEqual([10.0, 20.0, 30.0, 40.0], elev.fill_gaps([10.0, None, None, 40.0]))

    def test_all_missing_elevation_degrades_to_flat(self):
        self.assertEqual([0.0, 0.0, 0.0], elev.fill_gaps([None, None, None]))

    def test_slope_limit_rejects_a_dem_spike(self):
        """A building or tree canopy beside a fairway reads as a cliff."""
        limited = elev.limit_slope([0.0, 0.0, 60.0, 0.0], [0.0, 50.0, 100.0, 150.0],
                                   max_grade=0.25)

        self.assertLessEqual(max(limited), 0.25 * 50.0 + 0.01)

    def test_png_decoding_undoes_row_filters(self):
        import struct
        import zlib

        rows = [bytes([10, 20, 30, 40, 50, 60]), bytes([11, 21, 31, 41, 51, 61])]
        sub = bytes([10, 20, 30, 30, 30, 30])  # row 0, filter 1 (Sub)
        up = bytes([1, 1, 1, 1, 1, 1])         # row 1, filter 2 (Up)
        payload = zlib.compress(b"\x01" + sub + b"\x02" + up)

        def chunk(kind, body):
            return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))

        png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 2, 2, 8, 2, 0, 0, 0)) +
               chunk(b"IDAT", payload) + chunk(b"IEND", b""))
        width, height, decoded = elev.decode_png_rgb(png)

        self.assertEqual((2, 2), (width, height))
        self.assertEqual(rows, decoded)

    def test_bank_rises_towards_the_lateral_side_and_is_clamped(self):
        # Heading +z, the lateral side (-dz, dx) is -x.
        sides = elev.lateral_offsets([(0.0, 0.0), (0.0, 100.0)], 10.0)
        self.assertEqual(((-10.0, 0.0), (10.0, 0.0)), sides[0])

        bank = elev.bank_profile([12.0, 30.0, None], [10.0, 0.0, 5.0], 20.0, max_bank=0.2, smooth_window=1)
        self.assertEqual([0.1, 0.2, 0.0], bank)

    def test_terrarium_pixels_decode_to_metres(self):
        # 32768 + 76.5 m = 128 * 256 + 76 + 128 / 256
        self.assertEqual([[76.5]], elev.terrarium_heights([bytes([128, 76, 128])]))

    def test_sampler_reads_cached_tiles_without_the_network(self):
        import tempfile
        from pathlib import Path

        with tempfile.TemporaryDirectory() as cache:
            sampler = elev.ElevationSampler(cache_dir=Path(cache), zoom=14, verbose=False)
            fx, fy = elev.tile_pixel(56.0, 10.0, 14)
            tile = Path(cache) / "terrarium" / "14" / str(int(fx) // 256) / f"{int(fy) // 256}.png"
            tile.parent.mkdir(parents=True)
            tile.write_bytes(b"placeholder")
            # Every pixel of the cached tile is 12 m high.
            with mock.patch.object(elev, "decode_png_rgb", return_value=(256, 256, [bytes([128, 12, 0]) * 256] * 256)), \
                 mock.patch.object(sampler, "_download", side_effect=AssertionError("no network")):
                # (56, 10) is well inside its tile, so all four bilinear corners are in it.
                self.assertAlmostEqual(12.0, sampler.elevation(56.0, 10.0))
            self.assertEqual(0, sampler.tiles_downloaded)
            self.assertEqual(1, sampler.tiles_from_cache)

    def test_hole_json_carries_elevation_into_control_points(self):
        holes = conv.group_holes([
            way(1, {"golf": "hole", "ref": "1", "par": "4"}, [(56.0, 10.0), (56.0027, 10.0)]),
            node(2, {"golf": "tee"}, 56.0, 10.0),
            node(3, {"golf": "pin"}, 56.0027, 10.0),
        ])

        class RampSampler:
            dataset = "test"

            def elevations(self, latlons):
                # 1 m of rise per 0.0001 degrees of latitude.
                return [(lat - 56.0) * 10000.0 for lat, _lon in latlons]

        h_json = conv.hole_to_json(1, holes[1], 56.0, 10.0, "test", None, RampSampler())
        ys = [p[1] for p in h_json["spline"]["control_points"]]

        self.assertEqual(0.0, ys[0], "the tee must stay at y=0")
        self.assertGreater(ys[-1], 5.0, "an uphill hole must rise")
        self.assertEqual(sorted(ys), ys, "a monotonic ramp must stay monotonic")

    def test_elevation_is_optional(self):
        holes = conv.group_holes([
            way(1, {"golf": "hole", "ref": "1"}, [(56.0, 10.0), (56.0027, 10.0)]),
        ])

        h_json = conv.hole_to_json(1, holes[1], 56.0, 10.0, "test", None, None)

        self.assertTrue(all(p[1] == 0.0 for p in h_json["spline"]["control_points"]))


class LookupTests(unittest.TestCase):
    def test_nominatim_results_are_filtered_to_course_polygons(self):
        payload = [
            {"osm_type": "node", "osm_id": 1, "category": "information", "type": "board"},
            {"osm_type": "way", "osm_id": 2, "category": "amenity", "type": "restaurant"},
            {"osm_type": "way", "osm_id": 3, "category": "leisure", "type": "golf_course"},
            {"osm_type": "relation", "osm_id": 4, "category": "landuse", "type": "grass",
             "extratags": {"leisure": "golf_course"}},
        ]

        with mock.patch.object(conv, "_http_json", return_value=payload):
            refs = conv._nominatim_search("Anywhere Golf Club")

        self.assertEqual([("way", 3), ("relation", 4)], refs)

    def test_name_lookup_falls_back_to_overpass_when_geocoding_fails(self):
        args = argparse.Namespace(name="Somewhere GC", id=None, lat=None, lon=None,
                                  list_courses=False)
        course = way(7, {"leisure": "golf_course", "name": "Somewhere GC"},
                     [(0, 0), (0, 0.01), (0.01, 0.01), (0.01, 0)])

        with mock.patch.object(conv, "_nominatim_search", return_value=[]):
            with mock.patch.object(conv, "_query", return_value={"elements": [course]}):
                el, name = conv._find_course(args)

        self.assertEqual(7, el["id"])
        self.assertEqual("Somewhere GC", name)

    def test_area_scoped_query_is_used_for_a_closed_way_course(self):
        """A bbox around one course can contain several; the course area cannot."""
        course = way(1, {"leisure": "golf_course"},
                     [(0, 0), (0, 0.01), (0.01, 0.01), (0.01, 0)])

        prelude = conv._course_area_prelude(course)

        self.assertIn("map_to_area", prelude)
        self.assertIn("way(1)", prelude)


class SharedSiteTests(unittest.TestCase):
    """
    One `leisure=golf_course` polygon often holds two layouts. Augusta
    National's contains the Par 3 Course, so refs 1-9 appear twice and the two
    sets merged into single impossible holes.
    """

    @staticmethod
    def _main_course_line(ref, lat0=33.50):
        # ~400 m holes, laid out west to east.
        lon = -82.02 + ref * 0.004
        return way(1000 + ref, {"golf": "hole", "ref": str(ref), "par": "4",
                                "name": f"Main {ref}"},
                   [(lat0, lon), (lat0 + 0.0036, lon)])

    @staticmethod
    def _par3_line(ref, lat0=33.515):
        # ~110 m holes, a kilometre north of the main course.
        lon = -82.02 + ref * 0.001
        return way(2000 + ref, {"golf": "hole", "ref": str(ref), "par": "3"},
                   [(lat0, lon), (lat0 + 0.001, lon)])

    def _site(self):
        elements = [self._main_course_line(r) for r in range(1, 19)]
        elements += [self._par3_line(r) for r in range(1, 10)]
        return elements

    def test_second_layout_is_dropped(self):
        kept = conv._resolve_duplicate_hole_lines(self._site())
        kept_ids = {el["id"] for el in kept}

        self.assertEqual(18, len(kept_ids))
        self.assertTrue(all(i >= 1000 for i in kept_ids),
                        "the short par-3 layout should not survive")

    def test_every_hole_keeps_its_own_par(self):
        holes = conv.group_holes(self._site())

        self.assertEqual(18, len(holes))
        for num, h in holes.items():
            pars = {el["tags"]["par"] for el in h["lines"]}
            self.assertEqual({"4"}, pars, f"hole {num} picked up the other layout's par")

    def test_foreign_features_sharing_a_ref_are_dropped(self):
        # A green from the other layout, carrying ref=3 but a kilometre away.
        foreign_green = way(3003, {"golf": "green", "ref": "3"},
                            [(33.515, -82.019), (33.5151, -82.019),
                             (33.5151, -82.0189), (33.515, -82.0189)])
        own_green = way(3103, {"golf": "green", "ref": "3"},
                        [(33.5036, -82.008), (33.5037, -82.008),
                         (33.5037, -82.0079), (33.5036, -82.0079)])

        kept = conv._resolve_duplicate_hole_lines(self._site() + [foreign_green, own_green])
        kept_ids = {el["id"] for el in kept}

        self.assertIn(3103, kept_ids)
        self.assertNotIn(3003, kept_ids)

    def test_a_single_course_is_left_completely_alone(self):
        elements = [self._main_course_line(r) for r in range(1, 19)]

        kept = conv._resolve_duplicate_hole_lines(elements)

        self.assertEqual(len(elements), len(kept))


class TeeSelectionTests(unittest.TestCase):
    def test_the_tee_matching_the_surveyed_length_is_chosen(self):
        """The tee furthest from the pin can be a neighbouring hole's."""
        h = conv._empty_hole()
        line = [(0.0, 0.0), (0.0, 500.0)]
        pin = (0.0, 500.0)
        # Three tee boxes at the start of the hole, plus one 55 m behind them
        # that belongs to the hole before.
        h["tees"] = [
            node(1, {"golf": "tee"}, 0.0, 0.0),
            node(2, {"golf": "tee"}, 0.0, 0.0),
            node(3, {"golf": "tee"}, 0.0, 0.0),
        ]

        def fake_to_xz(elements, *_args):
            return {1: [(0.0, 30.0)], 2: [(0.0, 0.0)], 3: [(0.0, -55.0)]}[elements[0]["id"]]

        with mock.patch.object(conv, "_to_xz_list", side_effect=fake_to_xz):
            chosen = conv._select_tee_xz(h, line, pin, 0.0, 0.0)

        # 500 m target: the tee at z=0 gives exactly 500, the one at -55 gives 555.
        self.assertEqual((0.0, 0.0), chosen)

    def test_a_neighbouring_holes_tee_is_excluded(self):
        h = conv._empty_hole()
        h["tees"] = [node(1, {"golf": "tee"}, 0.0, 0.0), node(2, {"golf": "tee"}, 0.0, 0.0)]
        line = [(0.0, 0.0), (0.0, 300.0)]

        def fake_to_xz(elements, *_args):
            # Tee 2 sits beside this hole's green - it is the next hole's tee.
            return {1: [(0.0, 0.0)], 2: [(10.0, 305.0)]}[elements[0]["id"]]

        with mock.patch.object(conv, "_to_xz_list", side_effect=fake_to_xz):
            chosen = conv._select_tee_xz(h, line, (0.0, 300.0), 0.0, 0.0)

        self.assertEqual((0.0, 0.0), chosen)


class HoleTagTests(unittest.TestCase):
    def test_shared_feature_labels_resolve_to_several_holes(self):
        self.assertEqual([3, 15], conv._parse_hole_numbers({"name": "3/15"}))
        self.assertEqual([2, 16], conv._parse_hole_numbers({"ref": "2;16"}))
        self.assertEqual([8, 10], conv._parse_hole_numbers({"name": "8/10"}))
        self.assertEqual([7], conv._parse_hole_numbers({"ref": "7"}))

    def test_descriptive_names_are_not_hole_numbers(self):
        self.assertEqual([], conv._parse_hole_numbers({"name": "Hell Bunker"}))
        self.assertEqual([], conv._parse_hole_numbers({"name": "Practice Green"}))
        self.assertEqual([], conv._parse_hole_numbers({"name": "Road"}))

    def test_par_and_name_come_from_the_hole_way_not_the_merged_tags(self):
        holes = conv.group_holes([
            way(1, {"golf": "hole", "ref": "1", "par": "5", "name": "Long"},
                [(56.0, 10.0), (56.0045, 10.0)]),
            way(2, {"golf": "bunker", "ref": "1", "name": "Hell Bunker", "par": "3"},
                [(56.002, 10.0), (56.0021, 10.0), (56.0021, 10.0001), (56.002, 10.0001)]),
        ])

        h_json = conv.hole_to_json(1, holes[1], 56.0, 10.0, "test", None, None)

        self.assertEqual("Long", h_json["name"])
        self.assertEqual(5, h_json["par"])

    def test_a_numeric_hole_name_becomes_a_readable_one(self):
        holes = conv.group_holes([
            way(1, {"golf": "hole", "ref": "17", "par": "4", "name": "#17"},
                [(56.0, 10.0), (56.0036, 10.0)]),
        ])

        h_json = conv.hole_to_json(17, holes[17], 56.0, 10.0, "test", None, None)

        self.assertEqual("Hole 17", h_json["name"])


class ZoneTests(unittest.TestCase):
    @staticmethod
    def turned(points, degrees, dx=0.0, dz=0.0):
        c, s_ = math.cos(math.radians(degrees)), math.sin(math.radians(degrees))
        return [(x * c - z * s_ + dx, x * s_ + z * c + dz) for x, z in points]

    def test_an_ellipse_keeps_the_polygons_centre_area_and_turn(self):
        # A 30 x 10 m green turned 30 degrees the way rotate_about_y turns.
        green = self.turned([(-15, -5), (15, -5), (15, 5), (-15, 5)], 30.0, 40.0, -20.0)

        fitted = osm_ellipse.ellipse_from_polygon(green)

        self.assertAlmostEqual(40.0, fitted["center"][0], places=6)
        self.assertAlmostEqual(-20.0, fitted["center"][1], places=6)
        self.assertAlmostEqual(300.0, math.pi * fitted["radii"][0] * fitted["radii"][1], places=3)
        self.assertGreater(fitted["radii"][0], fitted["radii"][1] * 2.5)
        self.assertAlmostEqual(30.0, math.degrees(fitted["rotation"]), places=6)

    def test_a_circle_stays_a_circle(self):
        ring = [(10 * math.cos(i * math.tau / 48), 10 * math.sin(i * math.tau / 48)) for i in range(48)]

        zone = osm_ellipse.zone_json("green", osm_ellipse.ellipse_from_polygon(ring))

        self.assertEqual(zone["radii"][0], zone["radii"][1])
        self.assertEqual(0.0, zone["rotation_degrees"])
        self.assertNotIn("radius", zone)

    def test_the_first_radius_points_along_the_rotation_like_rotate_about_y(self):
        # Long along +z: rotate_about_y turns local +x onto (cos r, sin r).
        strip = [(-2.0, -20.0), (2.0, -20.0), (2.0, 20.0), (-2.0, 20.0)]

        zone = osm_ellipse.zone_json("bunker", osm_ellipse.ellipse_from_polygon(strip))

        self.assertGreater(zone["radii"][0], zone["radii"][1])
        self.assertAlmostEqual(90.0, abs(zone["rotation_degrees"]), places=3)

    def test_a_bent_bunker_is_split_into_several_ellipses(self):
        l_shape = [(0, 0), (40, 0), (40, 6), (6, 6), (6, 40), (0, 40)]

        single, single_iou = osm_ellipse.fit_polygon(l_shape, max_pieces=1)
        split, split_iou = osm_ellipse.fit_polygon(l_shape, max_pieces=4)

        self.assertEqual(1, len(single))
        self.assertGreater(len(split), 1)
        self.assertGreater(split_iou, single_iou + 0.3)

    def test_a_small_green_grows_to_the_minimum_keeping_its_shape(self):
        small = {"center": (0.0, 0.0), "radii": (4.0, 2.0), "rotation": 0.3}

        grown = osm_ellipse.with_min_radius(small, 5.0)

        self.assertAlmostEqual(25.0, grown["radii"][0] * grown["radii"][1], places=6)
        self.assertAlmostEqual(2.0, grown["radii"][0] / grown["radii"][1], places=6)

    def test_hole_zones_are_ellipses_with_their_fit_recorded(self):
        lat, lon = 56.0, 10.0
        d_lat = 1.0 / conv.EARTH_METERS_PER_DEGREE_LAT
        d_lon = 1.0 / (conv.EARTH_METERS_PER_DEGREE_LAT * math.cos(math.radians(lat)))
        def box(osm_id, tags, x0, z0, x1, z1):
            return way(osm_id, tags, [(lat - z * d_lat, lon + x * d_lon) for x, z in
                                      ((x0, z0), (x1, z0), (x1, z1), (x0, z1), (x0, z0))])
        holes = conv.group_holes([
            way(1, {"golf": "hole", "ref": "1"}, [(lat, lon), (lat + 150 * d_lat, lon)]),
            box(2, {"golf": "green", "ref": "1"}, -10, -160, 10, -140),
            box(3, {"golf": "water_hazard", "ref": "1"}, 20, -100, 60, -90),
        ])

        h_json = conv.hole_to_json(1, holes[1], lat, lon, "test", conv.load_generation_config(None))

        types = sorted(z["type"] for z in h_json["material_zones"])
        self.assertEqual(["green", "water"], types)
        for zone in h_json["material_zones"]:
            self.assertEqual({"type", "center", "radii", "rotation_degrees"}, set(zone))
        self.assertEqual(["way/2", "way/3"], sorted(f["osm"] for f in h_json["source"]["zone_fit"]))
        self.assertEqual("way/1", h_json["source"]["hole_way"])


class SharedGreenTests(unittest.TestCase):
    """
    Links courses share one enormous green between two holes. Whether that has
    happened is decided by the green polygon, not by a distance threshold:
    both holes must finish *on* it.
    """

    @staticmethod
    def _rect(osm_id, tags, lat0, lon0, dlat, dlon):
        return way(osm_id, tags, [(lat0, lon0), (lat0 + dlat, lon0),
                                  (lat0 + dlat, lon0 + dlon), (lat0, lon0 + dlon)])

    def test_an_unlabelled_double_green_reaches_both_holes(self):
        # Two holes running towards each other, finishing on one wide green.
        elements = [
            way(1, {"golf": "hole", "ref": "2"}, [(56.0, 10.0), (56.0, 10.0030)]),
            way(2, {"golf": "hole", "ref": "16"}, [(56.0010, 10.0), (56.00005, 10.0030)]),
            # ~60 m x 45 m green covering both finishing points.
            self._rect(3, {"golf": "green"}, 55.99985, 10.0028, 0.0006, 0.0006),
        ]

        holes = conv.group_holes(elements)

        self.assertEqual(1, len(holes[2]["greens"]), "hole 2 should have the green")
        self.assertEqual(1, len(holes[16]["greens"]), "hole 16 should share it")

    def test_a_neighbouring_green_is_not_shared(self):
        # A compact par-3 course: two holes, each with its own small green
        # roughly 25 m apart. Neither should pick up the other's.
        elements = [
            way(1, {"golf": "hole", "ref": "1"}, [(56.0, 10.0), (56.0, 10.0012)]),
            way(2, {"golf": "hole", "ref": "2"}, [(56.0006, 10.0), (56.0006, 10.0012)]),
            self._rect(3, {"golf": "green"}, 55.99996, 10.00115, 0.00008, 0.00010),
            self._rect(4, {"golf": "green"}, 56.00056, 10.00115, 0.00008, 0.00010),
        ]

        holes = conv.group_holes(elements)

        self.assertEqual(1, len(holes[1]["greens"]))
        self.assertEqual(1, len(holes[2]["greens"]))
        self.assertNotEqual(holes[1]["greens"][0]["id"], holes[2]["greens"][0]["id"])


class CourseSelectionTests(unittest.TestCase):
    # A main course hole along latitude 56.000 and a par-3 hole along 56.010,
    # each with an unnumbered green at its far end.
    ELEMENTS = [
        way(1, {"golf": "hole", "ref": "1"}, [(56.0, 10.0), (56.0, 10.004)]),
        way(2, {"golf": "hole", "ref": "P1"}, [(56.01, 10.0), (56.01, 10.002)]),
        way(3, {"golf": "green"}, [(56.0, 10.004), (56.0001, 10.0041), (56.0, 10.0042)]),
        way(4, {"golf": "green"}, [(56.01, 10.002), (56.0101, 10.0021), (56.01, 10.0022)]),
    ]

    def test_the_par_3_course_keeps_its_holes_and_nearest_features(self):
        selected = conv.select_course_by_ref_prefix(self.ELEMENTS, "p", 56.005, 10.002)

        self.assertEqual([2, 4], [el["id"] for el in selected])
        self.assertEqual("1", selected[0]["tags"]["ref"])

    def test_the_main_course_no_longer_takes_the_par_3_course_green(self):
        selected = conv.select_course_by_ref_prefix(self.ELEMENTS, "", 56.005, 10.002)

        self.assertEqual([1, 3], [el["id"] for el in selected])

    def test_a_missing_prefix_is_an_error_naming_what_exists(self):
        with self.assertRaisesRegex(ValueError, "'P'"):
            conv.select_course_by_ref_prefix(self.ELEMENTS, "X", 56.005, 10.002)

    def test_an_unnumbered_course_is_left_to_grouping(self):
        unnumbered = [way(1, {"golf": "hole"}, [(56.0, 10.0), (56.0, 10.004)])]

        self.assertEqual(unnumbered, conv.select_course_by_ref_prefix(unnumbered, "", 56.0, 10.002))


class HoleListTests(unittest.TestCase):
    # Two unnumbered hole lines, a stray third one, and a hole mapped only as
    # a tee, a fairway and a green.
    ELEMENTS = [
        way(1, {"golf": "hole"}, [(56.0, 10.0), (56.0, 10.004)]),
        way(2, {"golf": "hole"}, [(56.001, 10.004), (56.001, 10.0)]),
        way(3, {"golf": "hole"}, [(56.002, 10.0), (56.002, 10.001)]),
        way(4, {"golf": "green"}, [(56.003, 10.002), (56.0031, 10.0021), (56.003, 10.0022)]),
        way(5, {"golf": "fairway"}, [(56.003, 10.0021), (56.003, 10.0005), (56.0031, 10.0021)]),
        way(6, {"golf": "tee"}, [(56.004, 10.0), (56.004, 10.0001), (56.0041, 10.0)]),
    ]

    def test_lines_are_numbered_in_list_order_and_others_dropped(self):
        result = conv.apply_hole_list(self.ELEMENTS, [{"line": "W2", "par": 4}, {"line": "W1"}])
        holes = conv.group_holes(result)

        self.assertEqual([1, 2], sorted(holes))
        self.assertEqual(2, holes[1]["lines"][0]["id"])
        self.assertEqual("4", holes[1]["lines"][0]["tags"]["par"])
        self.assertEqual(1, holes[2]["lines"][0]["id"])
        self.assertNotIn(3, [el["id"] for el in result])

    def test_a_fairway_tee_starts_at_its_end_furthest_from_the_green(self):
        result = conv.apply_hole_list(self.ELEMENTS, [{"tee": "W5", "green": "W4"}])
        line = [el for el in result if el["tags"].get("golf") == "hole"]

        self.assertEqual(1, len(line))
        self.assertEqual({"lat": 56.003, "lon": 10.0005}, line[0]["geometry"][0])
        self.assertEqual("1", [el for el in result if el["id"] == 4][0]["tags"]["ref"])

    def test_a_tee_element_starts_at_its_centre_and_joins_the_hole(self):
        result = conv.apply_hole_list(self.ELEMENTS, [{"tee": "W6", "green": "W4"}])
        holes = conv.group_holes(result)

        self.assertEqual([6], [el["id"] for el in holes[1]["tees"]])
        self.assertEqual([4], [el["id"] for el in holes[1]["greens"]])

    def test_an_id_outside_the_course_is_an_error(self):
        with self.assertRaisesRegex(ValueError, "W99"):
            conv.apply_hole_list(self.ELEMENTS, [{"line": "W99"}])


class OtherCourseTests(unittest.TestCase):
    MAIN = way(9, {"leisure": "golf_course"}, [(56.0, 10.002), (56.0, 10.006), (56.004, 10.006), (56.004, 10.002)])
    PITCH_AND_PUTT = way(10, {"leisure": "golf_course"},
                         [(56.0, 10.0), (56.0, 10.002), (56.002, 10.002), (56.002, 10.0)])

    def test_features_of_the_course_next_door_are_left_out(self):
        # Mollerup's Pitch and Putt lies beside the main course, inside the
        # main course's footprint buffer.
        theirs = way(1, {"golf": "green"}, [(56.001, 10.0015), (56.0011, 10.0015), (56.001, 10.0016)])
        ours = way(2, {"golf": "green"}, [(56.003, 10.004), (56.0031, 10.004), (56.003, 10.0041)])

        kept = conv._drop_other_course_features([theirs, ours], self.MAIN, [self.PITCH_AND_PUTT])

        self.assertEqual([2], [el["id"] for el in kept])

    def test_a_feature_where_two_boundaries_overlap_stays(self):
        # The Old Course's boundary overlaps the New Course's along an edge.
        neighbour = way(11, {"leisure": "golf_course"}, [(56.0, 10.005), (56.0, 10.009), (56.004, 10.009), (56.004, 10.005)])
        shared_bunker = way(3, {"golf": "bunker"}, [(56.002, 10.0055), (56.0021, 10.0055), (56.002, 10.0056)])

        kept = conv._drop_other_course_features([shared_bunker], self.MAIN, [neighbour])

        self.assertEqual([3], [el["id"] for el in kept])

    def test_features_of_a_course_drawn_inside_this_one_are_left_out(self):
        # Mollerup's Pitch and Putt is drawn inside Mollerup Golf Club's boundary.
        inner = way(12, {"leisure": "golf_course"}, [(56.001, 10.003), (56.001, 10.004), (56.002, 10.004),
                                                     (56.002, 10.003), (56.001, 10.003)])
        theirs = way(4, {"golf": "green"}, [(56.0015, 10.0035), (56.0016, 10.0035), (56.0015, 10.0036)])
        ours = way(5, {"golf": "green"}, [(56.003, 10.005), (56.0031, 10.005), (56.003, 10.0051)])

        kept = conv._drop_other_course_features([theirs, ours], self.MAIN, [inner])

        self.assertEqual([5], [el["id"] for el in kept])


class ProjectionTests(unittest.TestCase):
    def test_east_is_plus_x_and_north_is_minus_z_so_courses_are_not_mirrored(self):
        # The game puts +X on the left when facing +Z, so +Z must be south for
        # east to sit on the right of a player facing north.
        x, z = conv._latlon_to_xz(56.001, 10.001, 56.0, 10.0)
        self.assertGreater(x, 0.0)
        self.assertLess(z, 0.0)
        lat, lon = conv._xz_to_latlon(x, z, 56.0, 10.0)
        self.assertAlmostEqual(56.001, lat, places=9)
        self.assertAlmostEqual(10.001, lon, places=9)


class PolylineTests(unittest.TestCase):
    def test_a_single_resampled_point_is_the_middle_of_the_line(self):
        # A tree row shorter than the tree spacing asks for one tree.
        self.assertEqual([(5.0, 0.0)], conv._resample_polyline([(0.0, 0.0), (10.0, 0.0)], 1))
        self.assertEqual(1, len(conv._sample_polyline_points([(0.0, 0.0), (8.0, 0.0)])))


class SlugTests(unittest.TestCase):
    def test_slugs_transliterate_instead_of_dropping_letters(self):
        self.assertEqual("kalo_golf_club", conv.slugify("Kalø Golf Club"))
        self.assertEqual("aero_golfklub", conv.slugify("Ærø Golfklub"))
        self.assertEqual("haderslev_golf_club", conv.slugify("Haderslev  Golf-Club"))
        self.assertEqual("st_andrews_eden", conv.slugify("St Andrews: Éden"))


class GroundTests(unittest.TestCase):
    WORLD = {"hole_starts": [
        {"position": [100.0, 2.0, 0.0]},
        {"position": [0.0, 0.0, 300.0], "rotation_degrees": 90.0},
    ]}
    HOLES = [
        {"tee": [0.0, 0.0, 0.0], "pin": [0.0, 0.0, 200.0],
         "spline": {"control_points": [[0.0, 0.0, 0.0], [0.0, 0.0, 200.0]]}},
        {"tee": [0.0, 0.0, 0.0], "pin": [0.0, 0.0, 100.0],
         "spline": {"control_points": [[0.0, 0.0, 0.0], [0.0, 0.0, 100.0]]}},
    ]

    class HeightFromZ:
        """A DEM whose height above sea level is 50 m plus a tenth of z."""
        dataset = "fake"

        def elevations(self, latlons):
            return [50.0 + latlon[0] * 0.1 for latlon in latlons]

    def test_holes_are_placed_by_their_start_and_rotation(self):
        points = ground.placed_hole_points(self.WORLD, self.HOLES)

        self.assertIn((100.0, 200.0), points)
        x, z = points[-1]  # hole 2's pin, 100 m along -x after a 90 degree turn
        self.assertAlmostEqual(-100.0, x, places=4)
        self.assertAlmostEqual(300.0, z, places=4)

    def test_grid_covers_every_hole_plus_the_margin(self):
        grid = ground.ground_grid(self.WORLD, self.HOLES, None, lambda x, z: (z, x), 20.0, 50.0)

        self.assertEqual([-150.0, -50.0], grid["origin"])
        self.assertGreaterEqual(grid["origin"][0] + (grid["columns"] - 1) * 20.0, 150.0)
        self.assertGreaterEqual(grid["origin"][1] + (grid["rows"] - 1) * 20.0, 350.0)
        self.assertEqual(grid["columns"] * grid["rows"], len(grid["heights"]))
        self.assertEqual({0.0}, set(grid["heights"]))

    def test_heights_are_relative_to_hole_one_start(self):
        grid = ground.ground_grid(self.WORLD, self.HOLES, self.HeightFromZ(), lambda x, z: (z, x), 20.0, 50.0)

        # Hole 1's start is at z = 0 and y = 2, so z = 0 maps to y = 2 and
        # every 10 m north adds 1 m.
        columns = grid["columns"]
        first_row_z = grid["origin"][1]
        self.assertAlmostEqual(2.0 + first_row_z * 0.1, grid["heights"][0], places=2)
        self.assertAlmostEqual(2.0 + (first_row_z + 20.0) * 0.1, grid["heights"][columns], places=2)



def latlon_at(x, z, origin=(56.0, 10.0)):
    return conv._xz_to_latlon(x, z, *origin)


class WorldStartTests(unittest.TestCase):
    def test_a_hole_start_is_the_tee_its_hole_is_built_from(self):
        # Two tee boxes: the hole plays from the back one (matching the line's
        # length); the world must start the hole there too, or the game moves
        # the whole hole by the gap.
        line = way(1, {"golf": "hole", "ref": "1"}, [latlon_at(0, 0), latlon_at(0, -300)])
        front = way(2, {"golf": "tee", "ref": "1"}, [latlon_at(-3, -120), latlon_at(3, -120), latlon_at(3, -125)])
        back = way(3, {"golf": "tee", "ref": "1"}, [latlon_at(-3, 2), latlon_at(3, 2), latlon_at(3, -2)])
        holes = conv.group_holes([line, front, back])
        hole_jsons = hole_jsons_for(holes, 56.0, 10.0)

        world = conv.course_world_to_json("t", "T", way(9, {}, [latlon_at(-50, 50), latlon_at(50, -350)]),
                                          holes, [], 56.0, 10.0, hole_jsons)

        tee = conv._latlon_to_xz(*hole_jsons[1]["source"]["tee_latlon"], 56.0, 10.0)
        start = world["hole_starts"][0]["position"]
        self.assertAlmostEqual(tee[0], start[0], delta=0.01)
        self.assertAlmostEqual(tee[1], start[2], delta=0.01)
        self.assertGreater(math.hypot(hole_jsons[1]["pin"][0], hole_jsons[1]["pin"][2]), 280.0)


class TeeVariantTests(unittest.TestCase):
    def test_lines_from_two_tee_sets_to_one_green_are_one_hole(self):
        white = way(1, {"golf": "hole", "ref": "7", "par": "3"}, [latlon_at(0, 0), latlon_at(0, -170)])
        yellow = way(2, {"golf": "hole", "ref": "7", "par": "3"}, [latlon_at(40, -60), latlon_at(1, -169)])
        others = [way(10 + n, {"golf": "hole", "ref": str(n)}, [latlon_at(300 + n * 40, 0), latlon_at(300 + n * 40, -350)])
                  for n in (1, 2, 3)]

        kept = conv._resolve_duplicate_hole_lines([white, yellow] + others)

        self.assertEqual([1, 11, 12, 13], sorted(el["id"] for el in kept))


class MadeGreenTests(unittest.TestCase):
    def test_a_pin_without_a_green_gets_a_stand_in(self):
        holes = conv.group_holes([
            way(1, {"golf": "hole", "ref": "1"}, [latlon_at(0, 0), latlon_at(0, -200)]),
            node(2, {"golf": "pin", "ref": "1"}, *latlon_at(0, -200)),
        ])

        h_json = conv.hole_to_json(1, holes[1], 56.0, 10.0, "t", conv.load_generation_config(None))

        greens = [z for z in h_json["material_zones"] if z["type"] == "green"]
        self.assertEqual(1, len(greens))
        self.assertTrue(h_json["source"]["made_green"])
        self.assertAlmostEqual(-200.0, greens[0]["center"][2], delta=0.5)


class WaterShapeTests(unittest.TestCase):
    def test_a_stream_drawn_as_a_line_becomes_a_chain_along_it(self):
        creek = way(5, {"golf": "water_hazard"}, [latlon_at(0, 0), latlon_at(60, 0), latlon_at(60, -60)])
        h = conv._empty_hole()
        h["waters"].append(creek)

        zones, fits = conv._material_zones(h, 56.0, 10.0, (0.0, 0.0), conv.DEFAULT_CONFIG["zones"])

        self.assertGreaterEqual(len(zones), 4)
        self.assertTrue(fits[0]["stream"])
        self.assertTrue(all(z["radii"][1] == 2.5 for z in zones))

    def test_a_multipolygon_lake_split_into_member_ways_is_one_ring(self):
        lake = {"type": "relation", "id": 7, "tags": {"golf": "water_hazard"}, "members": [
            {"role": "outer", "geometry": [{"lat": a, "lon": b} for a, b in (latlon_at(0, 0), latlon_at(40, 0), latlon_at(40, -20))]},
            {"role": "outer", "geometry": [{"lat": a, "lon": b} for a, b in (latlon_at(40, -20), latlon_at(0, -20), latlon_at(0, 0))]},
        ]}

        shapes = conv._element_shapes_xz(lake, 56.0, 10.0)

        self.assertEqual(1, len(shapes))
        self.assertTrue(shapes[0][1])
        self.assertAlmostEqual(800.0, abs(osm_ellipse.polygon_area(shapes[0][0])), delta=2.0)


def synthetic_course(holes):
    """
    Hole JSON and a course world for holes given as (tee_xz, pin_xz, par) in
    course metres around 56 N 10 E, built the way the converter writes them.
    """
    hole_jsons, starts = [], []
    for index, (tee, pin, par) in enumerate(holes):
        rel = (pin[0] - tee[0], pin[1] - tee[1])
        hole_jsons.append({
            "id": f"t_h{index + 1:02d}", "name": f"Hole {index + 1}", "par": par,
            "tee": [0.0, 0.0, 0.0], "pin": [rel[0], 0.0, rel[1]],
            "spline": {"control_points": [[0.0, 0.0, 0.0], [rel[0] / 2, 0.0, rel[1] / 2], [rel[0], 0.0, rel[1]]],
                       "width": 30.0, "rough_width": 46.0},
            "material_zones": [{"type": "green", "center": [rel[0], 0, rel[1]], "radii": [12.0, 9.0],
                                "rotation_degrees": 20.0}],
            "trees": [],
            "source": {"tee_latlon": list(latlon_at(*tee)), "pin_latlon": list(latlon_at(*pin))},
        })
        starts.append({"id": f"hole_{index + 1:02d}_start", "hole_index": index, "position": [tee[0], 0.0, tee[1]]})
    world = {"projection": {"origin_lat": 56.0, "origin_lon": 10.0}, "hole_starts": starts}
    return hole_jsons, world


class CheckTests(unittest.TestCase):
    # Three holes up and down a field: 1 north, 2 south, 3 north.
    HOLES = [((0, 0), (0, -350), 4), ((40, -350), (40, -200), 3), ((80, -190), (80, -650), 5)]
    CARD = {"total_par": 12, "holes": {
        "1": {"par": 4, "metres": 350, "bearing_deg": 0},
        "2": {"par": 3, "metres": 150, "faces": "S"},
        "3": {"par": 5, "metres": 460, "bearing_deg": 0}}}

    def codes(self, findings, level="error"):
        return sorted(item["code"] for item in findings.items if item["level"] == level)

    def check(self, holes=None, card=None, edit=None):
        hole_jsons, world = synthetic_course(holes or self.HOLES)
        if edit:
            edit(hole_jsons, world)
        return osm_checks.check_course("t", hole_jsons, world, card)

    def card_with(self, number, entry):
        return {**self.CARD, "holes": {**self.CARD["holes"], number: entry}}

    def test_a_clean_course_has_no_errors_or_warnings(self):
        findings = self.check(card=self.CARD)

        self.assertEqual([], self.codes(findings, "error") + self.codes(findings, "warn"))

    def test_internal_checks_run_without_a_scorecard(self):
        def take_hole_ones_green(hole_jsons, world):
            hole_jsons[1]["pin"] = [-40.0, 0.0, 0.0]
            hole_jsons[1]["material_zones"][0]["center"] = [-40.0, 0, 0.0]

        findings = self.check(edit=take_hole_ones_green)

        self.assertIn("GREEN_SHARED", self.codes(findings))

    def test_hole_count_and_par(self):
        card = self.card_with("2", {"par": 4, "metres": 150})
        card = {**card, "holes": {**card["holes"], "4": {"par": 3}}}

        findings = self.check(card=card)

        self.assertEqual(["HOLE_COUNT", "PAR_MISMATCH"], self.codes(findings))

    def test_a_hole_facing_the_wrong_way(self):
        findings = self.check(card=self.card_with("1", {"par": 4, "metres": 350, "faces": "W"}))

        self.assertEqual(["HOLE_BEARING"], self.codes(findings))
        message = [i["message"] for i in findings.items if i["code"] == "HOLE_BEARING"][0]
        self.assertIn("expected to face west (270°) but faces north (0°)", message)

    def test_a_hole_somewhat_short_is_a_warning(self):
        findings = self.check(card=self.card_with("3", {"par": 5, "metres": 400}))

        self.assertEqual(["HOLE_LENGTH"], self.codes(findings, "warn"))
        self.assertEqual([], self.codes(findings))

    def test_a_hole_far_off_finds_the_green_its_tee_pairs_with(self):
        # Hole 1's tee is 204 m from hole 2's green, not 350 m from its own.
        findings = self.check(card=self.card_with("1", {"par": 4, "metres": 205}))

        self.assertEqual(["HOLE_LENGTH", "TEE_GREEN_PAIRING"], self.codes(findings))
        message = [i["message"] for i in findings.items if i["code"] == "TEE_GREEN_PAIRING"][0]
        self.assertIn("hole 2's green", message)

    def test_a_long_walk_between_holes(self):
        holes = [((0, 0), (0, -350), 4), ((600, -350), (600, -200), 3)]

        findings = osm_checks.check_course("t", *synthetic_course(holes), None)

        self.assertEqual(["ROUTING"], self.codes(findings, "warn"))

    def test_the_same_hole_twice_overlaps(self):
        holes = [((0, 0), (0, -350), 4), ((5, -10), (5, -345), 4)]

        findings = osm_checks.check_course("t", *synthetic_course(holes), None)

        self.assertIn("HOLES_OVERLAP", self.codes(findings))

    def test_numbering_start_mismatch_pin_off_green_and_zone_format(self):
        def break_things(hole_jsons, world):
            hole_jsons[0]["id"] = "t_h07"
            world["hole_starts"][1]["position"] = [140.0, 0.0, -350.0]
            hole_jsons[2]["material_zones"][0]["center"] = [60.0, 0, -100.0]
            hole_jsons[2]["material_zones"].append({"type": "bunker", "center": [0, 0, -50], "radius": 3})

        findings = self.check(edit=break_things)

        self.assertEqual(["HOLE_START_MISMATCH", "NUMBERING_GAP", "PIN_OFF_GREEN", "ZONE_FORMAT"], self.codes(findings))

    def test_poor_zone_fits_and_made_greens_are_warnings(self):
        def mark(hole_jsons, world):
            hole_jsons[0]["source"]["zone_fit"] = [{"osm": "way/9", "type": "bunker", "pieces": 6, "iou": 0.4}]
            hole_jsons[1]["source"]["made_green"] = True

        self.assertEqual(["GREEN_MADE", "ZONE_FIT"], self.codes(self.check(edit=mark), "warn"))

    def test_holes_are_placed_by_their_start_and_rotation(self):
        hole_jsons, world = synthetic_course([((0, 0), (0, -100), 3)])
        world["hole_starts"][0]["rotation_degrees"] = 90.0

        placed = osm_checks.placed_holes(hole_jsons, world)

        # rotate_about_y turns -z (north) onto +x (east).
        self.assertAlmostEqual(100.0, placed[0]["pin"][0], places=6)
        self.assertAlmostEqual(0.0, placed[0]["pin"][1], places=6)

    def test_the_contact_sheet_numbers_every_tee_and_green(self):
        hole_jsons, world = synthetic_course(self.HOLES)
        placed = osm_checks.placed_holes(hole_jsons, world)

        svg = osm_contact_sheet.render("Test Course", placed, self.CARD,
                                       osm_checks.check_course("t", hole_jsons, world, self.CARD))

        self.assertTrue(svg.startswith("<svg"))
        self.assertEqual(3, svg.count("<ellipse"))
        for number in ("1", "2", "3"):
            self.assertGreaterEqual(svg.count(f">{number}</text>"), 2)
        self.assertIn("0 error(s), 0 warning(s)", svg)


def box(osm_id, tags, lat, lon, half_m=8.0):
    """A closed square way of side 2*half_m metres centred on (lat, lon)."""
    dlat = half_m / 111_320.0
    dlon = half_m / (111_320.0 * math.cos(math.radians(lat)))
    corners = [(lat - dlat, lon - dlon), (lat - dlat, lon + dlon), (lat + dlat, lon + dlon),
               (lat + dlat, lon - dlon), (lat - dlat, lon - dlon)]
    return way(osm_id, tags, corners)


def north_of(lat, metres):
    return lat + metres / 111_320.0


class AuditTests(unittest.TestCase):
    """osm_audit: every gap in a course's OSM data is reported, with what to map."""

    LAT = 56.0
    COURSE = way(999, {"leisure": "golf_course", "name": "Test Golf"},
                 [(55.99, 9.99), (55.99, 10.05), (56.01, 10.05), (56.01, 9.99), (55.99, 9.99)])

    def lon(self, column):
        return 10.0 + column * 0.005  # about 310 m apart

    def strip(self, column, from_m, to_m, half_width_m=12.0):
        """A closed fairway-like strip running north along a column."""
        lon = self.lon(column)
        dlon = half_width_m / (111_320.0 * math.cos(math.radians(self.LAT)))
        south, north = north_of(self.LAT, from_m), north_of(self.LAT, to_m)
        return [(south, lon - dlon), (south, lon + dlon), (north, lon + dlon), (north, lon - dlon), (south, lon - dlon)]

    def complete_hole(self, number, column, par=3, metres=120.0):
        lon = self.lon(column)
        green_lat = north_of(self.LAT, metres)
        return [
            box(100 + number, {"golf": "tee"}, self.LAT, lon),
            box(200 + number, {"golf": "green"}, green_lat, lon),
            way(300 + number, {"golf": "hole", "ref": str(number), "par": str(par), "dist:yellow": str(int(metres))},
                [(self.LAT, lon), (green_lat, lon)]),
        ]

    def audit(self, elements, config=None, scorecard=None):
        import osm_audit
        return osm_audit.audit_elements(elements, self.COURSE, "test_golf", config or {}, scorecard)

    def codes(self, findings, level=None):
        return [i["code"] for i in findings.items if level is None or i["level"] == level]

    def scorecard(self, holes):
        return {"holes": {str(n): {"par": 3, "metres": 120, "bearing_deg": 0} for n in range(1, holes + 1)}}

    def test_a_completely_mapped_course_has_nothing_to_fix(self):
        elements = self.complete_hole(1, 0) + self.complete_hole(2, 1)
        elements.append(node(400, {"golf": "pin"}, north_of(self.LAT, 120), self.lon(0)))
        self.assertEqual(self.codes(self.audit(elements, scorecard=self.scorecard(2))), [])

    def test_a_green_without_a_hole_line_names_the_tee_that_probably_serves_it(self):
        elements = self.complete_hole(1, 0)
        elements.append(box(202, {"golf": "green"}, north_of(self.LAT, 100), self.lon(1)))
        elements.append(box(102, {"golf": "tee"}, self.LAT, self.lon(1)))
        findings = self.audit(elements, scorecard=self.scorecard(2))
        self.assertIn("HOLE_LINES_MISSING", self.codes(findings, "warn"))
        missing = [i for i in findings.items if i["code"] == "HOLE_LINE_MISSING"]
        self.assertEqual(len(missing), 1)
        self.assertEqual(missing[0]["level"], "warn")
        self.assertIn("way/202", missing[0]["message"])
        self.assertIn("way/102", missing[0]["message"])
        self.assertIn("to the south", missing[0]["message"])
        self.assertTrue(missing[0]["link"].startswith("https://www.openstreetmap.org/edit#map="))

    def test_a_green_reached_only_by_a_fairway_points_beyond_its_far_end(self):
        elements = [box(201, {"golf": "green"}, north_of(self.LAT, 150), self.lon(0)),
                    way(501, {"golf": "fairway"}, self.strip(0, 60, 140))]
        findings = self.audit(elements, scorecard=self.scorecard(1))
        message = next(i["message"] for i in findings.items if i["code"] == "HOLE_LINE_MISSING")
        self.assertIn("fairway way/501 leads to it from the south", message.lower())

    def test_a_practice_green_is_not_a_missing_hole(self):
        elements = self.complete_hole(1, 0) + [box(250, {"golf": "green", "name": "Putting green"}, self.LAT, self.lon(2))]
        self.assertNotIn("HOLE_LINE_MISSING", self.codes(self.audit(elements, scorecard=self.scorecard(1))))

    def test_a_line_drawn_green_to_tee_is_reported_reversed(self):
        elements = self.complete_hole(1, 0)
        elements[2]["geometry"].reverse()
        findings = self.audit(elements, scorecard=self.scorecard(1))
        self.assertEqual(self.codes(findings, "warn"), [])
        self.assertIn("HOLE_LINE_REVERSED", self.codes(findings, "info"))

    def test_a_line_with_no_tee_or_green_at_its_ends_asks_for_them(self):
        elements = [e for e in self.complete_hole(1, 0) if e["tags"]["golf"] == "hole"]
        findings = self.audit(elements, scorecard=self.scorecard(1))
        self.assertIn("TEE_MISSING", self.codes(findings, "info"))
        self.assertIn("GREEN_MISSING", self.codes(findings, "warn"))

    def test_untagged_lines_say_the_par_and_number_were_guessed(self):
        elements = self.complete_hole(1, 0)
        elements[2]["tags"] = {"golf": "hole"}
        findings = self.audit(elements, scorecard=self.scorecard(1))
        warns = self.codes(findings, "warn")
        self.assertIn("LINE_NO_PAR", warns)
        self.assertIn("LINE_NO_REF", warns)
        self.assertIn("LINE_NO_DIST", self.codes(findings, "info"))

    def test_a_skipped_hole_number_is_a_gap(self):
        elements = self.complete_hole(1, 0) + self.complete_hole(3, 2)
        self.assertIn("REF_GAP", self.codes(self.audit(elements), "warn"))

    def test_a_par_four_line_that_crosses_no_fairway_is_noted(self):
        elements = self.complete_hole(1, 0, par=4, metres=320.0)
        self.assertIn("FAIRWAY_MISSING", self.codes(self.audit(elements), "info"))
        fairway = way(501, {"golf": "fairway"}, self.strip(0, 60, 250))
        self.assertNotIn("FAIRWAY_MISSING", self.codes(self.audit(elements + [fairway])))

    def test_a_config_tee_taken_from_a_fairway_is_a_guess(self):
        elements = [box(201, {"golf": "green"}, north_of(self.LAT, 150), self.lon(0)),
                    way(501, {"golf": "fairway"}, self.strip(0, 60, 130))]
        config = {"holes": [{"tee": "W501", "green": "W201", "par": 3}]}
        findings = self.audit(elements, config=config, scorecard=self.scorecard(1))
        self.assertIn("TEE_GUESSED", self.codes(findings, "warn"))
        missing = next(i for i in findings.items if i["code"] == "HOLE_LINE_MISSING")
        self.assertEqual(missing["hole"], 1)
        self.assertIn("course config pairs it", missing["message"])

    def test_a_course_without_a_scorecard_asks_for_one(self):
        self.assertIn("SCORECARD_MISSING", self.codes(self.audit(self.complete_hole(1, 0)), "warn"))
        no_lengths = {"holes": {"1": {"par": 3}}}
        self.assertIn("SCORECARD_NO_LENGTHS",
                      self.codes(self.audit(self.complete_hole(1, 0), scorecard=no_lengths), "warn"))

    def test_the_todo_is_a_checklist_with_links(self):
        import osm_audit
        elements = self.complete_hole(1, 0)
        elements[2]["geometry"].reverse()
        text = osm_audit.todo_markdown("Test Golf", self.COURSE, self.audit(elements))
        self.assertIn("- [ ] **HOLE_LINE_REVERSED**", text)
        self.assertIn("(https://www.openstreetmap.org/way/301)", text)

    def test_a_tee_outside_the_course_boundary_asks_to_extend_it(self):
        elements = self.complete_hole(1, 0)
        for el in elements:
            el["geometry"] = [{"lat": p["lat"] - 0.0105, "lon": p["lon"]} for p in el["geometry"]]
        findings = self.audit(elements)
        outside = [i for i in findings.items if i["code"] == "LINE_OUTSIDE_COURSE"]
        self.assertEqual(len(outside), 1)
        self.assertIn("tee end", outside[0]["message"])
        self.assertIn("way/999", outside[0]["message"])

    def test_a_course_is_fetched_with_its_outline(self):
        # `out geom bb` is bounds only (the last geometry mode wins), which
        # left every course without a boundary polygon.
        with mock.patch.object(conv, "_query", return_value={"elements": []}) as query:
            conv._fetch_elements_by_ref([("way", 1)])
        text = query.call_args[0][0]
        self.assertIn("out geom;", text)
        self.assertNotIn(" bb", text)

    def test_a_bare_side_of_a_hole_is_named_with_its_direction(self):
        # A hole playing north (-z) with trees only on its right, the east (+x).
        trees = [(15.0, -float(z)) for z in range(0, 200, 10)]
        placed = {"number": 1, "line": [(0.0, 0.0), (0.0, -200.0)], "trees": trees}
        findings = osm_checks.Findings()
        osm_checks._check_tree_sides(findings, placed, dict(osm_checks.DEFAULT_TOLERANCES))
        messages = [i["message"] for i in findings.items if i["code"] == "TREES_SPARSE"]
        self.assertEqual(len(messages), 1)
        self.assertIn("left (west)", messages[0])

    def test_a_fence_is_a_pole_at_each_node_and_between_far_ones(self):
        fence = way(700, {"barrier": "fence"}, [(56.0, 10.0), (north_of(56.0, 25.0), 10.0)])
        config = conv.DEFAULT_CONFIG["fence"]
        [out] = conv.fences_to_json([fence], [], 56.0, 10.0, config)
        self.assertEqual(out["osm_ref"], "way/700")
        self.assertEqual(len(out["poles"]), 4)  # 25 m at most 10 m apart: three stretches
        gaps = [math.dist((a[0], a[2]), (b[0], b[2])) for a, b in zip(out["poles"], out["poles"][1:])]
        self.assertTrue(all(abs(g - 25.0 / 3) < 0.05 for g in gaps), gaps)
        self.assertEqual((out["height"], out["height_from"]), (config["default_height_m"], "default"))

    def test_a_fence_height_comes_from_its_tag_or_a_driving_range_beside_it(self):
        config = conv.DEFAULT_CONFIG["fence"]
        tagged = way(701, {"barrier": "fence", "height": "18 m"}, [(56.0, 10.0), (56.0001, 10.0)])
        self.assertEqual(conv.fence_height(tagged, [], 56.0, 10.0, config), (18.0, "tag"))
        driving_range = box(702, {"golf": "driving_range"}, 56.0005, 10.0, half_m=40.0)
        net = way(703, {"barrier": "fence"}, [(56.0, 10.0), (56.0001, 10.0)])
        self.assertEqual(conv.fence_height(net, [driving_range], 56.0, 10.0, config),
                         (config["driving_range_height_m"], "driving_range"))
        self.assertIsNone(conv._height_tag("tall"))
        self.assertEqual(conv._height_tag("1.8"), 1.8)

    def test_a_fence_without_a_height_tag_is_noted(self):
        net = way(703, {"barrier": "fence"}, [(56.0, 10.0), (56.0001, 10.0)])
        import osm_audit
        findings = osm_audit.audit_elements(self.complete_hole(1, 0), self.COURSE, "test_golf", {}, None, [net], [])
        notes = [i for i in findings.items if i["code"] == "FENCE_NO_HEIGHT"]
        self.assertEqual(len(notes), 1)
        self.assertIn("way/703", notes[0]["message"])

    def test_a_pond_mapped_only_as_water_is_a_water_hazard(self):
        pond = box(600, {"natural": "water"}, self.LAT, self.lon(0))
        self.assertTrue(conv._is_golf_feature(pond))
        self.assertEqual(conv._as_water_hazard(pond)["tags"]["golf"], "water_hazard")
        tagged = box(601, {"natural": "water", "golf": "lateral_water_hazard"}, self.LAT, self.lon(0))
        self.assertEqual(conv._as_water_hazard(tagged)["tags"]["golf"], "lateral_water_hazard")


if __name__ == "__main__":
    unittest.main()
