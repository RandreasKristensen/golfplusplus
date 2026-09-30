import argparse
import math
import sys
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))
import osm_golf_convert as conv
import osm_elevation as elev
import osm_elevation as elev
import osm_elevation as elev


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

        corridors = []
        for num in sorted(holes.keys()):
            tee_xz, pin_xz = conv._hole_world_anchors(num, holes[num], origin_lat, origin_lon)
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
                                          origin_lon)

        self.assertEqual(["cart_way_6"], [route["id"] for route in world["cart_roads"]])

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
        """Regression: picking the hole way used to throw the measured width away."""
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
        profile = elev.relative_profile([42.0, 45.0, 48.0], [0.0, 100.0, 200.0])

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

    def test_dataset_autoselect_prefers_the_regional_dem(self):
        self.assertEqual("ned10m", elev.auto_dataset(33.50, -82.02))    # Augusta
        self.assertEqual("eudem25m", elev.auto_dataset(56.34, -2.80))   # St Andrews
        self.assertEqual("mapzen", elev.auto_dataset(-33.9, 151.2))     # Sydney

    def test_sampler_batches_and_caches_lookups(self):
        sampler = elev.ElevationSampler(dataset="mapzen", verbose=False)
        calls = []

        def fake_batch(batch):
            calls.append(list(batch))
            for key in batch:
                sampler._cache[key] = 100.0

        sampler._fetch_batch = fake_batch
        values = sampler.elevations([(56.0, 10.0), (56.0, 10.0), (56.1, 10.1)])

        self.assertEqual([100.0, 100.0, 100.0], values)
        self.assertEqual(1, len(calls), "duplicate coordinates should share one lookup")
        self.assertEqual(2, len(calls[0]))

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
        """
        Taking the tee furthest from the pin reaches past the back tee onto a
        neighbouring hole's, which used to stretch Augusta's 15th by 60 m.
        """
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
        """A bunker's name used to be able to become the hole's name."""
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
    def test_elongated_green_uses_equal_area_radius_not_half_its_diagonal(self):
        # A 30 x 10 m green: Ritter would call it ~15.8 m across.
        green = [(0.0, 0.0), (30.0, 0.0), (30.0, 10.0), (0.0, 10.0)]

        _cx, _cz, radius = conv._zone_circle({}, green)

        self.assertAlmostEqual(math.sqrt(300.0 / math.pi), radius, places=1)

    def test_green_radius_is_capped(self):
        huge = [(0.0, 0.0), (90.0, 0.0), (90.0, 90.0), (0.0, 90.0)]

        _cx, _cz, radius = conv._zone_circle({}, huge, max_radius=22.0)

        self.assertEqual(22.0, radius)

    def test_shared_green_recentres_on_the_pin_that_lies_on_it(self):
        double_green = [(0.0, 0.0), (60.0, 0.0), (60.0, 30.0), (0.0, 30.0)]
        pin = (50.0, 15.0)

        cx, cz, _r = conv._zone_circle({}, double_green, anchor=pin)

        self.assertEqual(pin, (cx, cz))

    def test_a_pin_off_the_polygon_does_not_move_a_neighbours_green(self):
        """Otherwise two greens stack on the same pin."""
        green = [(0.0, 0.0), (20.0, 0.0), (20.0, 20.0), (0.0, 20.0)]
        distant_pin = (300.0, 300.0)

        cx, cz, _r = conv._zone_circle({}, green, anchor=distant_pin)

        self.assertEqual((10.0, 10.0), (cx, cz))


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


if __name__ == "__main__":
    unittest.main()
