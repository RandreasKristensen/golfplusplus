#!/usr/bin/env python3
"""
osm_ground.py — The ground height grid of a course world.

The land under and around every hole, sampled from the DEM: the game's
ground takes its height from it everywhere, holes included (an imported
hole's own heights are 0, so it lies on the land as it is). Heights are in
course coordinates, where hole 1's start is the reference (its tee sits at
its `position.y`). The cell is the DEM's: 2 m on the Danish lidar, where it
shows green contours and bunker lips, 20 m on Terrarium.

The grid covers the world box: the course map's view of the holes, turned
and shaped as the game's paper shows it, plus a margin, so the map is land to
its edges and a hole at the course's edge looks out over more of it. The
aerial reading covers the same box.

The grid is written as `heights_cm`, one array per row in whole centimetres:
a row's first entry is its height and every next one the step from the one
before, so even a 2 m grid stays a few megabytes. course_world_text writes
each row on one line.
"""

import json
import math

import osm_elevation


def _rotate_y(x: float, z: float, degrees: float) -> tuple[float, float]:
    """Same rotation as place_hole_point in src/game/play_area.cpp."""
    radians = math.radians(degrees)
    c, s = math.cos(radians), math.sin(radians)
    return x * c - z * s, x * s + z * c


def placed_hole_points(world: dict, holes: list[dict]) -> list[tuple[float, float]]:
    """Course-space XZ of every hole's tee, spline and pin, placed by its start."""
    points = []
    for start, hole in zip(world.get("hole_starts", []), holes):
        tee = hole["tee"]
        sx, _sy, sz = start["position"]
        rotation = float(start.get("rotation_degrees", 0.0))
        for p in [tee] + hole["spline"]["control_points"] + [hole["pin"]]:
            x, z = _rotate_y(p[0] - tee[0], p[2] - tee[2], rotation)
            points.append((sx + x, sz + z))
    return points


# The course map's paper is this much wider than tall, and shows at most
# this much more than the holes' span either way, its margin round them
# (paper_aspect and map_margin_share in src/renderer/course_map_overlay.cpp).
MAP_PAPER_ASPECT = 1.5
MAP_PAPER_SLACK = 1.1


def _map_turned(x: float, z: float, cos_t: float, sin_t: float) -> tuple[float, float]:
    """A course point on the map's axes (turned_point in course_map_overlay.cpp):
    course +X to the left, +Z up, turned counter-clockwise."""
    px, pz = -x, z
    return px * cos_t - pz * sin_t, px * sin_t + pz * cos_t


def map_turn(points: list[tuple[float, float]]) -> tuple[float, float]:
    """The map's turn (cosine, sine) of the course round `points` (course_turn
    in course_map_overlay.cpp): the one, nearest to none, leaving it least tall."""
    best, best_height = (1.0, 0.0), float("inf")
    for step in range(180):
        degrees = -(step // 2) if step % 2 == 0 else step // 2 + 1
        turn = (math.cos(math.radians(degrees)), math.sin(math.radians(degrees)))
        heights = [_map_turned(x, z, *turn)[1] for x, z in points]
        height = max(heights) - min(heights)
        if height < best_height * 0.9999:
            best, best_height = turn, height
    return best


def world_box(points: list[tuple[float, float]], margin: float) -> tuple[float, float, float, float]:
    """(min_x, min_z, max_x, max_z) round the map's view of the holes at
    `points`: their box on the map's axes grown to the paper round them, then
    `margin` more on every side, back in course axes."""
    if not points:
        raise ValueError("a world box needs at least one point")
    cos_t, sin_t = map_turn(points)
    turned = [_map_turned(x, z, cos_t, sin_t) for x, z in points]
    low_u, high_u = min(p[0] for p in turned), max(p[0] for p in turned)
    low_v, high_v = min(p[1] for p in turned), max(p[1] for p in turned)
    paper_width = max(high_u - low_u, (high_v - low_v) * MAP_PAPER_ASPECT) * MAP_PAPER_SLACK
    width = paper_width + 2.0 * margin
    height = paper_width / MAP_PAPER_ASPECT + 2.0 * margin
    mid_u, mid_v = (low_u + high_u) * 0.5, (low_v + high_v) * 0.5
    corners = []
    for su in (-0.5, 0.5):
        for sv in (-0.5, 0.5):
            u, v = mid_u + su * width, mid_v + sv * height
            # Turned back, then course +X is the map's left.
            px, pz = u * cos_t + v * sin_t, -u * sin_t + v * cos_t
            corners.append((-px, pz))
    return (min(c[0] for c in corners), min(c[1] for c in corners),
            max(c[0] for c in corners), max(c[1] for c in corners))


def ground_grid(world: dict,
                sampler,
                to_latlon,
                cell_size: float,
                box: tuple[float, float, float, float]) -> dict:
    """
    The course world's "ground" entry: a grid (rows along z, columns along x)
    covering `box` (world_box). `to_latlon(x, z)` projects course space to
    latitude/longitude. Without a sampler the ground is flat at y = 0.
    """
    min_x, min_z, max_x, max_z = box
    columns = int(math.ceil((max_x - min_x) / cell_size)) + 1
    rows = int(math.ceil((max_z - min_z) / cell_size)) + 1

    heights = [0.0] * (columns * rows)
    if sampler is not None:
        grid_xz = [(min_x + column * cell_size, min_z + row * cell_size)
                   for row in range(rows) for column in range(columns)]
        start = world["hole_starts"][0]["position"]
        samples = sampler.elevations([to_latlon(x, z) for x, z in grid_xz] + [to_latlon(start[0], start[2])])
        filled = osm_elevation.fill_gaps(samples)
        # Hole 1's tee is at start.y in the course, so that DEM height maps there.
        base = filled[-1] - start[1]
        heights = [round(h - base, 2) for h in filled[:-1]]

    return {
        "origin": [round(min_x, 2), round(min_z, 2)],
        "cell_size": cell_size,
        "columns": columns,
        "rows": rows,
        "heights_cm": height_rows_cm(heights, columns),
    }


def height_rows_cm(heights: list[float], columns: int) -> list[list[int]]:
    """Row-major metres as `heights_cm` rows: each row's first height, then steps."""
    rows = []
    for start in range(0, len(heights), columns):
        centimetres = [round(h * 100.0) for h in heights[start:start + columns]]
        rows.append(centimetres[:1] + [b - a for a, b in zip(centimetres, centimetres[1:])])
    return rows


def heights_from_rows_cm(rows: list[list[int]]) -> list[float]:
    """`heights_cm` rows back as row-major metres."""
    heights = []
    for row in rows:
        total = 0
        for step in row:
            total += step
            heights.append(total / 100.0)
    return heights


def course_world_text(world: dict) -> str:
    """A course world as JSON, indented, with each ground row and each of
    the land's trees on one line."""
    rows = world.get("ground", {}).get("heights_cm")
    trees = world.get("trees")
    shown = dict(world)
    row_marker, tree_marker = "@@heights_cm@@", "@@trees@@"
    if rows is not None:
        shown["ground"] = {**world["ground"], "heights_cm": row_marker}
    if trees:
        shown["trees"] = tree_marker
    text = json.dumps(shown, indent=2)
    if rows is not None:
        lines = ",\n".join("      " + json.dumps(row, separators=(",", ":")) for row in rows)
        text = text.replace(f'"{row_marker}"', "[\n" + lines + "\n    ]")
    if trees:
        lines = ",\n".join("    " + json.dumps(tree) for tree in trees)
        text = text.replace(f'"{tree_marker}"', "[\n" + lines + "\n  ]")
    return text + "\n"
