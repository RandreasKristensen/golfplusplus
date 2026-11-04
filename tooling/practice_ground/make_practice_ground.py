"""Writes the practice ground: a driving range and a putting and chipping green.

Not a real place: a made-up practice facility, the same every run, in
assets/world/courses/practice_ground/ (course.json, world.json, holes/).
Every hole is a practice station (course.json "practice"): the range's six
bays, each a tee box aimed at its own target green, and six stations round
one green, dropped on the ground with no tee box (hole "tee_box": false):
two putts, a chip from the apron, a chip from the rough and two bunker
shots. Run tooling/art/make_art.py after it to draw its backdrops.

    python tooling/practice_ground/make_practice_ground.py
"""

from __future__ import annotations

import json
import math
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
COURSE_ID = "practice_ground"
FOLDER = REPO / "assets" / "world" / "courses" / COURSE_ID

# Course coordinates in metres: +x east, -z north. The range plays north
# from its tee line along z = 0; the green lies east of the tee line.
BAY_SPACING = 12.0
BAYS = 6
# Each bay's target green: (metres out, metres across from the range's middle, radius).
TARGETS = [(50.0, -14.0, 7.0), (80.0, 12.0, 8.0), (110.0, -6.0, 9.0),
           (140.0, 16.0, 10.0), (175.0, -12.0, 11.0), (210.0, 4.0, 12.0)]
RANGE_HALF_WIDTH = 55.0
RANGE_NET_DEPTH = 290.0
NET_HEIGHT = 14.0
NET_POLE_SPACING = 18.0

GREEN_CENTER = (78.0, 22.0)
GREEN_RADII = (15.0, 10.0)  # across x, along z
APRON_WIDTH = 2.5
# Cups on the green, offsets from its centre, one per station.
CUPS = [(-8.0, -3.0), (6.0, 4.0), (-2.0, 5.5), (9.0, -4.0), (-10.0, 3.5), (2.0, -6.0)]
BUNKERS = [((78.0, 36.0), (8.0, 3.5)), ((96.0, 18.0), (3.5, 7.0))]
# The green's stations: kind, where the ball is dropped, its cup's index.
STATIONS = [("putt", (75.0, 19.0), 0), ("putt", (73.0, 25.0), 1),
            ("chip", (58.0, 24.0), 2), ("chip", (80.0, 4.0), 3),
            ("bunker", (76.0, 36.5), 4), ("bunker", (96.5, 16.0), 5)]
STATION_REACH = 1.5
BAY_REACH = 3.0

GRID_CELL = 2.0
GRID_LOW = (-80.0, -310.0)
GRID_HIGH = (130.0, 80.0)
# The green tilts back to front, as real practice greens do.
GREEN_TILT = 0.015


def land_height(x: float, z: float) -> float:
    """Metres: gentle swells on the range, a slope up behind the green."""
    swell = 0.6 * math.sin(x / 37.0) * math.cos(z / 53.0) + 0.4 * math.sin((x + z) / 71.0)
    gx, gz = GREEN_CENTER
    rise = GREEN_TILT * (z - gz) if abs(x - gx) < 40.0 and abs(z - gz) < 40.0 else 0.0
    near_tee = min(1.0, max(0.0, (-z - 15.0) / 30.0))  # flat by the tee line and the green
    return swell * near_tee + rise


def ellipse(center: tuple[float, float], radii: tuple[float, float], points: int = 24) -> list[list[float]]:
    cx, cz = center
    rx, rz = radii
    return [[round(cx + rx * math.cos(2.0 * math.pi * i / points), 2), round(cz + rz * math.sin(2.0 * math.pi * i / points), 2)]
            for i in range(points)]


def straight_sided(corners: list[list[float]], corner_radius: float) -> list[list[float]]:
    """The polygon with straight sides and corners rounded over about `corner_radius` metres. The game draws a
    zone's edge as a curve through its points, so each side is a run of four points in a line."""
    out = []
    for i, (ax, az) in enumerate(corners):
        bx, bz = corners[(i + 1) % len(corners)]
        length = math.hypot(bx - ax, bz - az)
        for along in (corner_radius, 2.0 * corner_radius, length - 2.0 * corner_radius, length - corner_radius):
            out.append([round(ax + (bx - ax) * along / length, 2), round(az + (bz - az) * along / length, 2)])
    return out


def local(outline: list[list[float]], origin: tuple[float, float]) -> list[list[float]]:
    return [[round(x - origin[0], 2), round(z - origin[1], 2)] for x, z in outline]


def point(x: float, z: float, origin: tuple[float, float]) -> list[float]:
    return [round(x - origin[0], 2), 0.0, round(z - origin[1], 2)]


def hole(hole_id: str, name: str, tee: tuple[float, float], pin: tuple[float, float], width: float,
         zones: list[dict], trees: list[dict], drop: bool) -> dict:
    """A station in its own coordinates: its tee at the origin."""
    middle = ((tee[0] + pin[0]) / 2.0, (tee[1] + pin[1]) / 2.0)
    data = {
        "id": f"{COURSE_ID}_{hole_id}",
        "name": name,
        "par": 3,
        "tee": [0.0, 0.0, 0.0],
        "pin": point(*pin, tee),
        "spline": {"control_points": [point(*tee, tee), point(*middle, tee), point(*pin, tee)],
                   "width": width, "rough_width": width + 4.0},
        "ribbon_fairway": False,
        "material_zones": [{"type": z["type"], "outline": local(z["outline"], tee)} for z in zones],
        "trees": [{**t, "position": point(*t["position"], tee)} for t in trees],
        "source": {"type": "generated", "tool": "tooling/practice_ground/make_practice_ground.py"},
    }
    if drop:
        data["tee_box"] = False
    return data


def range_trees() -> list[dict]:
    trees = []
    for i in range(0, 17):
        z = -10.0 - i * 18.0
        for side in (-1.0, 1.0):
            x = side * (RANGE_HALF_WIDTH + 8.0 + 3.0 * math.sin(i * 1.7))
            trees.append({"position": (x, z), "leaf_radius": 3.75, "leaf_height": 7.5, "trunk_height": 3.0})
    for i in range(-3, 4):
        trees.append({"position": (i * 16.0, -RANGE_NET_DEPTH - 10.0), "leaf_radius": 4.0, "leaf_height": 8.0, "trunk_height": 3.2})
    return trees


def range_net() -> list[list[float]]:
    """Down the west side, across the far end, back up the east side."""
    corners = [(-RANGE_HALF_WIDTH, 10.0), (-RANGE_HALF_WIDTH, -RANGE_NET_DEPTH),
               (RANGE_HALF_WIDTH, -RANGE_NET_DEPTH), (RANGE_HALF_WIDTH, 10.0)]
    poles = []
    for (ax, az), (bx, bz) in zip(corners, corners[1:]):
        steps = max(1, math.ceil(math.hypot(bx - ax, bz - az) / NET_POLE_SPACING))
        for s in range(steps):
            t = s / steps
            poles.append([round(ax + (bx - ax) * t, 2), 0.0, round(az + (bz - az) * t, 2)])
    poles.append([corners[-1][0], 0.0, corners[-1][1]])
    return poles


def bays() -> list[tuple[dict, tuple[float, float]]]:
    first_x = -BAY_SPACING * (BAYS - 1) / 2.0
    # The whole range is fairway from behind the tee line to the far net, so
    # nothing (a floodlight) stands among the bays.
    field = straight_sided([[-RANGE_HALF_WIDTH + 3.0, 8.0], [RANGE_HALF_WIDTH - 3.0, 8.0],
                            [RANGE_HALF_WIDTH - 3.0, -RANGE_NET_DEPTH + 3.0],
                            [-RANGE_HALF_WIDTH + 3.0, -RANGE_NET_DEPTH + 3.0]], 3.0)
    out = []
    for i, (out_m, across, radius) in enumerate(TARGETS):
        tee = (first_x + i * BAY_SPACING, 0.0)
        pin = (across, -out_m)
        zones = [{"type": "green", "outline": ellipse(pin, (radius, radius * 0.8))}]
        if i == 0:
            zones.append({"type": "fairway", "outline": field})
        if i in (2, 4):
            zones.append({"type": "bunker", "outline": ellipse((pin[0] + radius + 3.0, pin[1] + 2.0), (2.5, 4.0), 16)})
        trees = range_trees() if i == 0 else []
        out.append((hole(f"bay_{i + 1}", f"Bay {i + 1}", tee, pin, 8.0, zones, trees, drop=False), tee))
    return out


def green_stations() -> list[tuple[dict, tuple[float, float]]]:
    gx, gz = GREEN_CENTER
    cups = [(gx + dx, gz + dz) for dx, dz in CUPS]
    complex_zones = [
        {"type": "fairway", "outline": ellipse(GREEN_CENTER, (GREEN_RADII[0] + APRON_WIDTH, GREEN_RADII[1] + APRON_WIDTH))},
        {"type": "fairway", "outline": straight_sided([[48.0, 20.0], [64.0, 17.0], [64.0, 29.0], [48.0, 28.0]], 1.5)},
        {"type": "green", "outline": ellipse(GREEN_CENTER, GREEN_RADII, 32)},
    ] + [{"type": "bunker", "outline": ellipse(center, radii, 20)} for center, radii in BUNKERS]
    trees = [{"position": (gx + 34.0 * math.cos(a), gz + 26.0 * math.sin(a)), "leaf_radius": 3.25, "leaf_height": 6.5,
              "trunk_height": 2.6} for a in (0.3, 1.1, 1.9, 2.6, 4.3, 5.2)]
    out = []
    names = {"putt": "Putting", "chip": "Chipping", "bunker": "Bunker"}
    for i, (kind, tee, cup) in enumerate(STATIONS):
        zones = complex_zones if i == 0 else []
        out.append((hole(f"green_{i + 1}", f"{names[kind]} {i + 1}", tee, cups[cup], 1.0, zones,
                         trees if i == 0 else [], drop=True), tee))
    return out


def ground() -> dict:
    columns = int((GRID_HIGH[0] - GRID_LOW[0]) / GRID_CELL) + 1
    rows = int((GRID_HIGH[1] - GRID_LOW[1]) / GRID_CELL) + 1
    heights = []
    for r in range(rows):
        z = GRID_LOW[1] + r * GRID_CELL
        row, last = [], 0
        for c in range(columns):
            cm = round(100.0 * land_height(GRID_LOW[0] + c * GRID_CELL, z))
            row.append(cm - last)
            last = cm
        heights.append(row)
    return {"origin": list(GRID_LOW), "cell_size": GRID_CELL, "columns": columns, "rows": rows, "heights_cm": heights}


def main() -> int:
    stations = bays() + green_stations()
    (FOLDER / "holes").mkdir(parents=True, exist_ok=True)
    for old in (FOLDER / "holes").glob("*.json"):
        old.unlink()
    hole_refs, starts = [], []
    for index, (data, tee) in enumerate(stations):
        name = data["id"].removeprefix(f"{COURSE_ID}_")
        (FOLDER / "holes" / f"{name}.json").write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
        hole_refs.append(f"holes/{name}.json")
        starts.append({"id": f"{name}_start", "hole_index": index,
                       "position": [tee[0], round(land_height(*tee), 2), tee[1]],
                       "interaction_radius": STATION_REACH if data.get("tee_box") is False else BAY_REACH})
    course = {
        "id": COURSE_ID,
        "name": "Practice Ground",
        "practice": True,
        "carts": False,
        "backdrop": {"sky": "sky.bmp", "land": "land.bmp", "haze_color": [0.75, 0.8, 0.83], "haze_amount": 1.0,
                     "haze_distance": 600},
        "sky": {"date": "06-21", "utc_offset_hours": 2},
        "green_stimp_feet": 9.5,
        "air": {"elevation_meters": 30, "mean_temperature_c": 15.5, "daily_temperature_range_c": 8.5},
        "holes": hole_refs,
        "world": "world.json",
    }
    world = {
        "id": COURSE_ID,
        "name": "Practice Ground",
        "source": {"type": "generated", "tool": "tooling/practice_ground/make_practice_ground.py"},
        "projection": {"type": "equirectangular", "origin_lat": 56.0, "origin_lon": 10.0, "units": "meters"},
        "hole_starts": starts,
        "cart_roads": [],
        "fences": [{"poles": range_net(), "height": NET_HEIGHT}],
        "collectibles": [],
        "ground": ground(),
    }
    (FOLDER / "course.json").write_text(json.dumps(course, indent=2) + "\n", encoding="utf-8")
    (FOLDER / "world.json").write_text(json.dumps(world, separators=(",", ":")) + "\n", encoding="utf-8")
    print(f"wrote {FOLDER.relative_to(REPO)}: {len(stations)} stations")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
