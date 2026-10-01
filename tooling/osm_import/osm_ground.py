#!/usr/bin/env python3
"""
osm_ground.py — The ground height grid of a course world.

Each hole carries tee-relative heights along its spline. The ground grid is
the land between and around the holes, sampled from the same DEM, so the game
can build one continuous course: holes sit on real ground instead of floating
over a flat plane. Heights are in course coordinates, where hole 1's start is
the reference (its tee sits at its `position.y`).

The game blends the grid into each hole's edge, so the grid only needs to be
right at the scale of the landform; a cell of 15–25 m matches the DEMs.
"""

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
    The course world's "ground" entry: a row-major grid (rows along z, columns
    along x) covering every hole plus `margin`. `holes[i]` belongs to
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
        "heights": heights,
    }
