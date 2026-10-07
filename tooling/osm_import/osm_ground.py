#!/usr/bin/env python3
"""
osm_ground.py — The ground height grid of a course world.

The land under and around every hole, sampled from the DEM: the game's
ground takes its height from it everywhere, holes included (an imported
hole's own heights are 0, so it lies on the land as it is). Heights are in
course coordinates, where hole 1's start is the reference (its tee sits at
its `position.y`). The cell is the DEM's: 2 m on the Danish lidar, where it
shows green contours and bunker lips, 20 m on Terrarium.

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


def ground_grid(world: dict,
                holes: list[dict],
                sampler,
                to_latlon,
                cell_size: float,
                margin: float) -> dict:
    """
    The course world's "ground" entry: a grid (rows along z, columns along x)
    covering every hole plus `margin`. `holes[i]` belongs to
    world["hole_starts"][i]. `to_latlon(x, z)` projects course space to
    latitude/longitude. Without a sampler the ground is flat at y = 0.
    """
    points = placed_hole_points(world, holes)
    if not points:
        raise ValueError("a ground grid needs at least one placed hole")
    min_x = min(p[0] for p in points) - margin
    max_x = max(p[0] for p in points) + margin
    min_z = min(p[1] for p in points) - margin
    max_z = max(p[1] for p in points) + margin
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
    """A course world as JSON, indented, with each ground row on one line."""
    rows = world.get("ground", {}).get("heights_cm")
    if rows is None:
        return json.dumps(world, indent=2) + "\n"
    marker = "@@heights_cm@@"
    text = json.dumps({**world, "ground": {**world["ground"], "heights_cm": marker}}, indent=2)
    lines = ",\n".join("      " + json.dumps(row, separators=(",", ":")) for row in rows)
    return text.replace(f'"{marker}"', "[\n" + lines + "\n    ]") + "\n"
