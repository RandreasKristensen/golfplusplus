#!/usr/bin/env python3
"""
osm_bus_stop.py — where the club's bus parks on an imported course, in the
importer's local metres (x east, z south).

Real clubs have a car park by the clubhouse, near the first tee. The stop is
the place on an OSM car park (amenity=parking), else on a service road,
nearest the first tee whose whole bus footprint keeps `clearance_meters` off every
fairway, green, tee, bunker and water, every hole's line of play across its
rough (play_corridors), every tree and fence, and every mapped cart path.
Places beside or behind the first tee come before any ahead of it (within
`ahead_degrees` of the way it plays), so the bus and the path from it stay out
of the way of the first shot; the first tee is usually by the last green, so
that is near the end of the round too. The bus lies along the car park's longest side or the road, its
door (the right-hand side: Denmark drives on the right) towards the first
tee. The search is bounded: `max_candidates` places of each kind, nearest first. No clear
place is an import error (osm_checks BUS_STOP), fixed here or in the config.

The bus's size, door and approach are the game's model
(assets/world/props/hub_bus.json), so placement and the game agree. Pure
Python.
"""

import json
import math
from pathlib import Path

import osm_outline

BUS_MODEL = Path(__file__).resolve().parents[2] / "assets" / "world" / "props" / "hub_bus.json"


def load_bus(path: Path = BUS_MODEL) -> dict:
    """The model's footprint half sizes, door and approach in its own frame
    (+X left, +Z forward), as (across, along)."""
    with open(path, "r", encoding="utf-8") as f:
        points = json.load(f)["points"]
    low, high = points["body_min"], points["body_max"]
    return {"half_width": (high[0] - low[0]) * 0.5, "half_length": (high[2] - low[2]) * 0.5,
            "centre": ((high[0] + low[0]) * 0.5, (high[2] + low[2]) * 0.5),
            "door": (points["door"][0], points["door"][2]), "approach": (points["approach"][0], points["approach"][2])}


def heading_of(forward) -> float:
    """The game's heading (yaw_direction: zero faces +z) for a forward (x, z)."""
    return math.atan2(forward[0], forward[1])


def model_point(position, heading: float, local) -> tuple[float, float]:
    """A model point (across, along) of a bus at `position` facing `heading`:
    +X is the bus's left, as in the game's prop frame."""
    fx, fz = math.sin(heading), math.cos(heading)
    left = (fz, -fx)
    return (position[0] + left[0] * local[0] + fx * local[1], position[1] + left[1] * local[0] + fz * local[1])


def footprint(position, heading: float, bus: dict, step: float = 0.5):
    """The bus's outline on the ground, its edges resampled every `step` metres."""
    hw, hl = bus["half_width"], bus["half_length"]
    cx, cz = bus["centre"]
    corners = [model_point(position, heading, (cx + sx * hw, cz + sz * hl))
               for sx, sz in ((1, 1), (-1, 1), (-1, -1), (1, -1))]
    return osm_outline.resample(corners + corners[:1], step)[:-1]


def _box(points, grow: float = 0.0):
    xs, zs = [p[0] for p in points], [p[1] for p in points]
    return (min(xs) - grow, min(zs) - grow, max(xs) + grow, max(zs) + grow)


def _boxes_touch(a, b) -> bool:
    return a[0] <= b[2] and b[0] <= a[2] and a[1] <= b[3] and b[1] <= a[3]


class Obstacles:
    """What the bus keeps clear of: rings (zones), lines (cart paths, fences)
    and circles (trees), each with a bounding box for a quick reject."""

    def __init__(self, rings=(), lines=(), circles=()):
        self.rings = [(ring, _box(ring)) for ring in rings if len(ring) >= 3]
        resampled = [osm_outline.resample(line, 0.5) for line in lines if len(line) >= 2]
        self.lines = [(line, _box(line)) for line in resampled]
        self.circles = [((x, z), r) for x, z, r in circles]

    def clear(self, outline, clearance: float) -> bool:
        box = _box(outline, clearance)
        for ring, ring_box in self.rings:
            if _boxes_touch(box, ring_box) and osm_outline.rings_gap(outline, ring) < clearance:
                return False
        for line, line_box in self.lines:
            if _boxes_touch(box, line_box) and any(
                    osm_outline.point_in_polygon(p, outline) or osm_outline.edge_distance(p, outline) < clearance
                    for p in line if box[0] <= p[0] <= box[2] and box[1] <= p[1] <= box[3]):
                return False
        for centre, radius in self.circles:
            reach = clearance + radius
            if box[0] - radius <= centre[0] <= box[2] + radius and box[1] - radius <= centre[1] <= box[3] + radius and (
                    osm_outline.point_in_polygon(centre, outline) or osm_outline.edge_distance(centre, outline) < reach):
                return False
        return True


def _longest_side(ring):
    a, b = max(zip(ring, ring[1:] + ring[:1]), key=lambda e: math.dist(e[0], e[1]))
    length = math.dist(a, b) or 1.0
    return ((b[0] - a[0]) / length, (b[1] - a[1]) / length)


def play_corridors(lines, half_widths):
    """Each hole's line of play, `half_widths[i]` either side: where its shots fly."""
    return [osm_outline.strip(line, [half] * len(line)) for line, half in zip(lines, half_widths) if len(line) >= 2]


def ahead_of(point, tee, forward, degrees: float) -> bool:
    """Whether `point` lies within `degrees` either side of `forward` from `tee`."""
    dx, dz = point[0] - tee[0], point[1] - tee[1]
    length = math.hypot(dx, dz)
    return length > 0.0 and (dx * forward[0] + dz * forward[1]) / length > math.cos(math.radians(degrees))


def candidates(parkings, roads, step: float):
    """(position, along) places on each car park (a grid inside it, along its
    longest side), then along each service road."""
    return _parking_places(parkings, step), _road_places(roads, step)


def _parking_places(parkings, step: float):
    out = []
    for ring in parkings:
        if len(ring) < 3:
            continue
        along = _longest_side(ring)
        x0, z0, x1, z1 = _box(ring)
        for i in range(int((x1 - x0) // step) + 1):
            for j in range(int((z1 - z0) // step) + 1):
                p = (x0 + i * step, z0 + j * step)
                if osm_outline.point_in_polygon(p, ring):
                    out.append((p, along))
    return out


def _road_places(roads, step: float):
    out = []
    for line in roads:
        points = osm_outline.resample(line, step)
        for a, b in zip(points, points[1:]):
            length = math.dist(a, b)
            if length > 0.0:
                out.append((((a[0] + b[0]) * 0.5, (a[1] + b[1]) * 0.5), ((b[0] - a[0]) / length, (b[1] - a[1]) / length)))
    return out


def choose(parkings, roads, first_tee, obstacles: Obstacles, rules: dict, bus: dict | None = None,
           first_way=None) -> dict | None:
    """The car park stop nearest `first_tee`, else the road stop nearest it
    ({"position": (x, z), "heading", "approach": (x, z), "footprint": ring}),
    or None when no place within the search is clear. With `first_way` (the
    unit way the first hole plays from its tee), places ahead of the tee come
    after all the others of their kind.
    On a car park the bus may stand either way along it; it turns its door to
    the tee."""
    bus = bus or load_bus()
    limit = int(rules["max_candidates"])

    def order(place):
        ahead = first_way is not None and ahead_of(place[0], first_tee, first_way, float(rules["ahead_degrees"]))
        return (ahead, math.dist(place[0], first_tee))

    places = [place for kind in candidates(parkings, roads, float(rules["step_meters"]))
              for place in sorted(kind, key=order)[:limit]]
    clearance = float(rules["clearance_meters"])
    for position, along in places:
        headings = [heading_of(along), heading_of((-along[0], -along[1]))]
        heading = min(headings, key=lambda h: math.dist(model_point(position, h, bus["door"]), first_tee))
        outline = footprint(position, heading, bus)
        if obstacles.clear(outline, clearance):
            return {"position": position, "heading": heading,
                    "approach": model_point(position, heading, bus["approach"]), "footprint": outline}
    return None


def stop_json(stop: dict) -> dict:
    """The course world's bus_stop. Its height is the ground's: the game stands
    the bus on a pad flattened like a tee, so y is 0 here."""
    x, z = stop["position"]
    return {"position": [round(x, 2), 0.0, round(z, 2)], "heading": round(stop["heading"], 4)}
