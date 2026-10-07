"""
osm_checks.py — Verify an imported course: coded, descriptive findings.

Runs after every import (osm_golf_convert.py) and on files already in assets/
(verify_osm_import.py). It reads only the generated JSON — the holes, the
course world and, when there is one, the published scorecard in
testdata/scorecards.json — so it judges what the game will load, not what the
converter meant to write.

Every finding has a level, a code and a sentence:

  error  the course is wrong (a hole faces the wrong way, plays a third short,
         starts somewhere else than its tee). A correctly imported course has
         none.
  warn   worth a look in the hole editor (a hole 15% short of a championship
         tee, a long walk between holes, a bunker its ellipses fit poorly).
  info   true of real courses too, listed for completeness (holes crossing).

Checks that need no scorecard (numbering, pairing, routing, overlap, loader
contract, plausibility) run on every course. Tolerances are the `checks`
section of osm_golf_config.json.

Coordinates: x east, z south, metres, as everywhere in the importer. A hole
is placed in the course by its world hole start, which is its tee.
"""

import json
import math
from pathlib import Path

SCORECARDS = Path(__file__).resolve().parent / "testdata" / "scorecards.json"

DEFAULT_TOLERANCES = {
    # Hole length against the scorecard, as a fraction of the scorecard.
    "length_warn": 0.12,
    "length_error": 0.25,
    # Hole direction (tee to pin) against the scorecard's bearing.
    "bearing_warn_deg": 25.0,
    "bearing_error_deg": 45.0,
    # Walk from one green to the next tee.
    "routing_warn_m": 300.0,
    "routing_error_m": 700.0,
    # Two pins this close are one green claimed by two holes; two tees, one tee.
    "shared_pin_m": 5.0,
    "shared_tee_m": 2.0,
    # A hole running this close to another for this share of its length is a
    # duplicate of it.
    "overlap_distance_m": 12.0,
    "overlap_share": 0.6,
    # A hole start further than this from its hole's tee moves the hole.
    "start_mismatch_m": 1.0,
    # Projected tee-to-pin distance against the geodesic one.
    "projection_error": 0.005,
    # Zones whose ellipses cover their OSM polygon worse than this.
    "zone_fit_warn_iou": 0.7,
    # Fewer trees than this per 100 m of hole within `tree_side_m` of one side
    # of its line is worth checking against the real course.
    "sparse_trees_per_100m": 2.0,
    "tree_side_m": 40.0,
    # Lengths a hole of each par plausibly has, metres along the line of play.
    "par_lengths": {"3": [40, 260], "4": [200, 520], "5": [380, 660], "6": [500, 800]},
}

_COMPASS = ["north", "north-east", "east", "south-east", "south", "south-west", "west", "north-west"]
_FACES = {"N": 0, "NNE": 22.5, "NE": 45, "ENE": 67.5, "E": 90, "ESE": 112.5, "SE": 135, "SSE": 157.5,
          "S": 180, "SSW": 202.5, "SW": 225, "WSW": 247.5, "W": 270, "WNW": 292.5, "NW": 315, "NNW": 337.5}

# WGS84, for the geodesic reference distance.
_A = 6378137.0
_F = 1 / 298.257223563
_B = _A * (1 - _F)
_METERS_PER_DEGREE_LAT = 111_320.0


# ── small geometry ────────────────────────────────────────────────────────────

def compass_name(bearing: float) -> str:
    return _COMPASS[int(((bearing % 360.0) + 22.5) // 45.0) % 8]


def bearing_deg(a, b) -> float:
    """Compass bearing from a to b, both (x east, z south): 0 north, 90 east."""
    return math.degrees(math.atan2(b[0] - a[0], -(b[1] - a[1]))) % 360.0


def angle_between(a: float, b: float) -> float:
    return abs((a - b + 180.0) % 360.0 - 180.0)


def polyline_length(pts) -> float:
    return sum(math.dist(a, b) for a, b in zip(pts, pts[1:]))


def _segment_distance(p, a, b) -> float:
    dx, dz = b[0] - a[0], b[1] - a[1]
    denom = dx * dx + dz * dz
    t = 0.0 if denom <= 1e-9 else max(0.0, min(1.0, ((p[0] - a[0]) * dx + (p[1] - a[1]) * dz) / denom))
    return math.dist(p, (a[0] + dx * t, a[1] + dz * t))


def polyline_distance(p, pts) -> float:
    if len(pts) == 1:
        return math.dist(p, pts[0])
    return min(_segment_distance(p, a, b) for a, b in zip(pts, pts[1:]))


def resample(pts, spacing: float):
    total = polyline_length(pts)
    if total <= 0.0:
        return list(pts[:1])
    count = max(2, int(total / spacing) + 1)
    out, walked, i = [], 0.0, 0
    for k in range(count):
        target = total * k / (count - 1)
        while i < len(pts) - 2 and walked + math.dist(pts[i], pts[i + 1]) < target:
            walked += math.dist(pts[i], pts[i + 1])
            i += 1
        seg = math.dist(pts[i], pts[i + 1]) or 1e-9
        t = min(1.0, max(0.0, (target - walked) / seg))
        out.append((pts[i][0] + (pts[i + 1][0] - pts[i][0]) * t, pts[i][1] + (pts[i + 1][1] - pts[i][1]) * t))
    return out


def _segments_cross(a, b, c, d) -> bool:
    def orient(p, q, r):
        return (q[0] - p[0]) * (r[1] - p[1]) - (q[1] - p[1]) * (r[0] - p[0])
    return (orient(a, b, c) * orient(a, b, d) < 0) and (orient(c, d, a) * orient(c, d, b) < 0)


def lines_cross(p, q) -> bool:
    return any(_segments_cross(a, b, c, d) for a, b in zip(p, p[1:]) for c, d in zip(q, q[1:]))


def latlon_to_xz(lat, lon, origin_lat, origin_lon):
    """The course world's equirectangular projection (projection in its JSON)."""
    return ((lon - origin_lon) * math.cos(math.radians(origin_lat)) * _METERS_PER_DEGREE_LAT,
            (origin_lat - lat) * _METERS_PER_DEGREE_LAT)


def geodesic_m(lat1, lon1, lat2, lon2) -> float:
    """
    Vincenty inverse solution on WGS84, in metres. Deliberately not the
    importer's flat projection: this is what the projection is measured against.
    """
    if abs(lat1 - lat2) < 1e-12 and abs(lon1 - lon2) < 1e-12:
        return 0.0
    u1 = math.atan((1 - _F) * math.tan(math.radians(lat1)))
    u2 = math.atan((1 - _F) * math.tan(math.radians(lat2)))
    l_diff = math.radians(lon2 - lon1)
    sin_u1, cos_u1, sin_u2, cos_u2 = math.sin(u1), math.cos(u1), math.sin(u2), math.cos(u2)
    lam = l_diff
    for _ in range(200):
        sin_lam, cos_lam = math.sin(lam), math.cos(lam)
        sin_sigma = math.hypot(cos_u2 * sin_lam, cos_u1 * sin_u2 - sin_u1 * cos_u2 * cos_lam)
        if sin_sigma == 0:
            return 0.0
        cos_sigma = sin_u1 * sin_u2 + cos_u1 * cos_u2 * cos_lam
        sigma = math.atan2(sin_sigma, cos_sigma)
        sin_alpha = cos_u1 * cos_u2 * sin_lam / sin_sigma
        cos_sq_alpha = 1 - sin_alpha ** 2
        cos2_sigma_m = 0.0 if cos_sq_alpha == 0 else cos_sigma - 2 * sin_u1 * sin_u2 / cos_sq_alpha
        c = _F / 16 * cos_sq_alpha * (4 + _F * (4 - 3 * cos_sq_alpha))
        lam_prev = lam
        lam = l_diff + (1 - c) * _F * sin_alpha * (
            sigma + c * sin_sigma * (cos2_sigma_m + c * cos_sigma * (-1 + 2 * cos2_sigma_m ** 2)))
        if abs(lam - lam_prev) < 1e-12:
            break
    u_sq = cos_sq_alpha * (_A ** 2 - _B ** 2) / (_B ** 2)
    a_coef = 1 + u_sq / 16384 * (4096 + u_sq * (-768 + u_sq * (320 - 175 * u_sq)))
    b_coef = u_sq / 1024 * (256 + u_sq * (-128 + u_sq * (74 - 47 * u_sq)))
    delta_sigma = b_coef * sin_sigma * (
        cos2_sigma_m + b_coef / 4 * (
            cos_sigma * (-1 + 2 * cos2_sigma_m ** 2)
            - b_coef / 6 * cos2_sigma_m * (-3 + 4 * sin_sigma ** 2) * (-3 + 4 * cos2_sigma_m ** 2)))
    return _B * a_coef * (sigma - delta_sigma)


# ── the course as placed ──────────────────────────────────────────────────────

def _rotate(x, z, radians):
    """rotate_about_y in src/physics/vector_math.h, on (x, z)."""
    c, s = math.cos(radians), math.sin(radians)
    return x * c - z * s, x * s + z * c


def placed_holes(hole_jsons: list[dict], world: dict | None) -> list[dict]:
    """
    Each hole in the course's shared coordinates, the way the game places it:
    the hole's tee on its world hole start, turned by the start's
    rotation_degrees. Without a world, holes are placed by their recorded
    tee coordinates around the first hole's tee.
    """
    starts = (world or {}).get("hole_starts", [])
    projection = (world or {}).get("projection")
    first_tee = hole_jsons[0].get("source", {}).get("tee_latlon") if hole_jsons else None
    placed = []
    for index, h in enumerate(hole_jsons):
        start = next((s for s in starts if s.get("hole_index") == index), None)
        if start is not None:
            origin = (start["position"][0], start["position"][2])
            turn = math.radians(float(start.get("rotation_degrees", 0.0)))
        else:
            tee_ll = h.get("source", {}).get("tee_latlon")
            ref = (projection["origin_lat"], projection["origin_lon"]) if projection else first_tee
            origin = latlon_to_xz(*tee_ll, *ref) if tee_ll and ref else (0.0, 0.0)
            turn = 0.0
        tee = h.get("tee", [0.0, 0.0, 0.0])

        def to_world(p, origin=origin, turn=turn, tee=tee):
            x, z = _rotate(p[0] - tee[0], p[2] - tee[2], turn)
            return (origin[0] + x, origin[1] + z)

        zones = []
        for zone in h.get("material_zones", []):
            if "center" in zone and "radii" in zone:
                zones.append({"type": zone.get("type"), "center": to_world(zone["center"]),
                              "radii": tuple(zone["radii"]),
                              "rotation": math.radians(float(zone.get("rotation_degrees", 0.0))) + turn})
        line = [to_world(p) for p in h.get("spline", {}).get("control_points", [])]
        placed.append({
            "number": index + 1,
            "json": h,
            "start": start,
            "tee": to_world(tee),
            "pin": to_world(h.get("pin", [0.0, 0.0, 0.0])),
            "line": line,
            "zones": zones,
            "trees": [to_world(t["position"]) for t in h.get("trees", []) if "position" in t],
        })
    return placed


# ── reference data ────────────────────────────────────────────────────────────

def load_scorecard(course_id: str, path: Path = SCORECARDS) -> dict | None:
    try:
        with open(path, "r", encoding="utf-8") as f:
            return json.load(f).get("courses", {}).get(course_id)
    except (OSError, json.JSONDecodeError):
        return None


def expected_bearing(entry: dict) -> float | None:
    if entry.get("bearing_deg") is not None:
        return float(entry["bearing_deg"]) % 360.0
    faces = str(entry.get("faces", "")).upper()
    return float(_FACES[faces]) if faces in _FACES else None


# ── checks ────────────────────────────────────────────────────────────────────

class Findings:
    def __init__(self):
        self.items: list[dict] = []

    def add(self, level: str, code: str, message: str, hole: int | None = None, link: str | None = None):
        """`link`: where to see or fix it (an OSM feature or the editor at a spot)."""
        self.items.append({"level": level, "code": code, "hole": hole, "message": message, "link": link})

    def extend(self, other: "Findings"):
        self.items.extend(other.items)

    def count(self, level: str) -> int:
        return sum(1 for item in self.items if item["level"] == level)

    def lines(self, levels=("error", "warn", "info")) -> list[str]:
        order = {"error": 0, "warn": 1, "info": 2}
        items = sorted((i for i in self.items if i["level"] in levels),
                       key=lambda i: (order[i["level"]], i["hole"] or 0, i["code"]))
        return [f"{i['level'].upper():5} {i['code']}: {i['message']}" for i in items]


def _hole_length(p: dict) -> float:
    """Along the line of play when there is one, the way a scorecard measures."""
    direct = math.dist(p["tee"], p["pin"])
    return max(direct, polyline_length(p["line"])) if p["line"] else direct


def _check_loader_contract(findings: Findings, p: dict):
    h, n = p["json"], p["number"]
    if h.get("tee") != [0.0, 0.0, 0.0]:
        findings.add("error", "LOADER", f"hole {n}'s tee must be the local origin, is {h.get('tee')}", n)
    spline = h.get("spline", {})
    points = spline.get("control_points", [])
    if len(points) < 2 or any(len(q) != 3 for q in points):
        findings.add("error", "LOADER", f"hole {n} needs at least two vec3 control points", n)
    width = spline.get("width", 0.0)
    if not width > 0.0:
        findings.add("error", "LOADER", f"hole {n}'s fairway width {width} is not positive", n)
    if spline.get("rough_width", width) < width:
        findings.add("error", "LOADER", f"hole {n}'s rough is narrower than its fairway", n)
    for zone in h.get("material_zones", []):
        radii = zone.get("radii")
        if "radius" in zone or "bounds" in zone:
            findings.add("error", "ZONE_FORMAT", f"hole {n} has a {zone.get('type')} zone with "
                         f"`{'radius' if 'radius' in zone else 'bounds'}`; "
                         f"zones are ellipses with `radii` and `rotation_degrees`", n)
        elif not (isinstance(radii, list) and len(radii) == 2
                                           and min(radii) > 0 and "rotation_degrees" in zone):
            findings.add("error", "ZONE_FORMAT", f"hole {n} has a malformed {zone.get('type')} zone: {zone}", n)


def _check_projection(findings: Findings, p: dict, tol: dict, world: dict | None):
    h, n = p["json"], p["number"]
    src = h.get("source", {})
    tee_ll, pin_ll = src.get("tee_latlon"), src.get("pin_latlon")
    if not (tee_ll and pin_ll):
        return
    reference = geodesic_m(*tee_ll, *pin_ll)
    direct = math.hypot(h["pin"][0], h["pin"][2])
    if reference > 1.0 and abs(direct - reference) / reference > tol["projection_error"]:
        findings.add("error", "PROJECTION", f"hole {n} is {direct:.1f} m tee to pin but {reference:.1f} m "
                     f"on the ground ({(direct - reference) / reference * 100:+.2f}%)", n)
    projection = (world or {}).get("projection")
    if projection and p["start"] is not None:
        expected = latlon_to_xz(*tee_ll, projection["origin_lat"], projection["origin_lon"])
        gap = math.dist(expected, p["tee"])
        if gap > tol["start_mismatch_m"]:
            findings.add("error", "HOLE_START_MISMATCH",
                         f"hole {n}'s world start is {gap:.0f} m from its tee, so the game draws the whole "
                         f"hole {gap:.0f} m away from where it is", n)


def _check_scorecard(findings: Findings, placed: list[dict], card: dict, tol: dict):
    holes = card.get("holes", {})
    if len(placed) != len(holes):
        findings.add("error", "HOLE_COUNT", f"expected {len(holes)} holes, imported {len(placed)}")
    total = sum(p["json"].get("par", 0) for p in placed)
    if card.get("total_par") is not None and total != card["total_par"] and len(placed) == len(holes):
        findings.add("error", "TOTAL_PAR", f"course par is {total}, the scorecard says {card['total_par']}")
    for p in placed:
        n = p["number"]
        entry = holes.get(str(n))
        if entry is None:
            continue
        par = p["json"].get("par")
        if entry.get("par") is not None and par != entry["par"]:
            findings.add("error", "PAR_MISMATCH", f"hole {n} is par {par}, the scorecard says par {entry['par']}", n)
        if entry.get("name") and p["json"].get("name") != entry["name"]:
            findings.add("warn", "NAME_MISMATCH", f"hole {n} is called {p['json'].get('name')!r}, "
                         f"the scorecard says {entry['name']!r}", n)
        length = _hole_length(p)
        card_m = entry.get("metres")
        if card_m:
            err = (length - card_m) / card_m
            if abs(err) > tol["length_warn"]:
                level = "error" if abs(err) > tol["length_error"] else "warn"
                findings.add(level, "HOLE_LENGTH", f"hole {n} is {length:.0f} m, the scorecard says "
                             f"{card_m:.0f} m ({err * 100:+.0f}%)", n)
            if abs(err) > tol["length_error"]:
                _suggest_pairing(findings, p, placed, card_m, expected_bearing(entry), tol)
        expected = expected_bearing(entry)
        if expected is not None:
            actual = bearing_deg(p["tee"], p["pin"])
            off = angle_between(actual, expected)
            if off > tol["bearing_warn_deg"]:
                level = "error" if off > tol["bearing_error_deg"] else "warn"
                findings.add(level, "HOLE_BEARING",
                             f"hole {n} was expected to face {compass_name(expected)} ({expected:.0f}°) "
                             f"but faces {compass_name(actual)} ({actual:.0f}°)", n)


def _suggest_pairing(findings: Findings, p: dict, placed: list[dict], card_m: float,
                     expected: float | None, tol: dict):
    """A hole far off its length: is its tee playing to another hole's green?"""
    n = p["number"]
    own = abs(_hole_length(p) - card_m)
    for other in placed:
        if other is p:
            continue
        d = math.dist(p["tee"], other["pin"])
        if abs(d - card_m) > min(own * 0.5, card_m * tol["length_warn"]):
            continue
        if expected is not None and angle_between(bearing_deg(p["tee"], other["pin"]), expected) > tol["bearing_warn_deg"]:
            continue
        findings.add("error", "TEE_GREEN_PAIRING",
                     f"hole {n}'s tee pairs better with hole {other['number']}'s green: {d:.0f} m away, "
                     f"against the scorecard's {card_m:.0f} m (its own green is {_hole_length(p):.0f} m)", n)
        return


def _check_plausibility(findings: Findings, p: dict, tol: dict):
    n, h = p["number"], p["json"]
    par = h.get("par")
    length = _hole_length(p)
    low, high = tol["par_lengths"].get(str(par), (0, 0))
    if not low <= length <= high:
        findings.add("warn", "PAR_LENGTH", f"hole {n} is par {par} but {length:.0f} m long "
                     f"(par {par} holes are {low}-{high} m)", n)
    direct = math.dist(p["tee"], p["pin"])
    if direct > 1.0 and length / direct > 1.6:
        findings.add("warn", "LINE_DETOUR", f"hole {n}'s line of play is {length / direct:.1f}x its "
                     f"tee-to-pin distance", n)
    greens = [z for z in p["zones"] if z["type"] == "green"]
    if h.get("source", {}).get("made_green"):
        findings.add("warn", "GREEN_MADE", f"hole {n} has no green in OSM; a circle at the pin stands in. "
                     f"Shape it in the hole editor (or map it in OSM)", n)
    if not greens:
        findings.add("error", "GREEN_MISSING", f"hole {n} has no green", n)
    elif not any(_in_zone(p["pin"], z, pad=2.0) for z in greens):
        nearest = min(math.dist(p["pin"], z["center"]) for z in greens)
        findings.add("error", "PIN_OFF_GREEN", f"hole {n}'s pin is not on its green (nearest green "
                     f"centre {nearest:.0f} m away)", n)
    for fit in h.get("source", {}).get("zone_fit", []) or []:
        if fit.get("iou", 1.0) < tol["zone_fit_warn_iou"]:
            findings.add("warn", "ZONE_FIT", f"hole {n}'s {fit.get('type')} {fit.get('osm')} is covered "
                         f"{fit['iou'] * 100:.0f}% by its {fit.get('pieces')} ellipse(s); reshape it in the editor", n)


def _check_tree_sides(findings: Findings, p: dict, tol: dict):
    """A side of a hole with hardly a tree: often trees that OSM is missing."""
    n, line = p["number"], p["line"]
    if len(line) < 2:
        return
    length = polyline_length(line)
    counts = {"left": 0, "right": 0}
    for tree in p["trees"]:
        if polyline_distance(tree, line) > tol["tree_side_m"]:
            continue
        # The segment nearest the tree says which side it stands on.
        a, b = min(zip(line, line[1:]), key=lambda ab: _segment_distance(tree, *ab))
        dx, dz = b[0] - a[0], b[1] - a[1]
        # Facing along (dx, dz) with x east and z south, left is (dz, -dx).
        side = (tree[0] - a[0]) * dz - (tree[1] - a[1]) * dx
        counts["left" if side > 0 else "right"] += 1
    bearing = bearing_deg(line[0], line[-1])
    for side, turn in (("left", -90.0), ("right", 90.0)):
        if counts[side] < tol["sparse_trees_per_100m"] * length / 100.0:
            findings.add("info", "TREES_SPARSE",
                         f"hole {n} has {counts[side]} tree(s) along its {side} ({compass_name(bearing + turn)}) "
                         f"side within {tol['tree_side_m']:.0f} m: if that side is wooded on the real course, "
                         f"map the trees in OSM (natural=tree, tree_row or wood) and re-import", n)


def _in_zone(pt, zone, pad: float = 0.0) -> bool:
    dx, dz = pt[0] - zone["center"][0], pt[1] - zone["center"][1]
    c, s = math.cos(zone["rotation"]), math.sin(zone["rotation"])
    u, v = dx * c + dz * s, -dx * s + dz * c
    a, b = zone["radii"][0] + pad, zone["radii"][1] + pad
    return (u / a) ** 2 + (v / b) ** 2 <= 1.0


def _check_numbering(findings: Findings, course_id: str, placed: list[dict]):
    for p in placed:
        expected = f"{course_id}_h{p['number']:02d}"
        if p["json"].get("id") != expected:
            findings.add("error", "NUMBERING_GAP", f"hole {p['number']} of the course is "
                         f"{p['json'].get('id')!r}, expected {expected!r}", p["number"])


def _check_pairs(findings: Findings, placed: list[dict], tol: dict):
    samples = {p["number"]: resample(p["line"], 5.0) for p in placed if len(p["line"]) >= 2}
    for i, a in enumerate(placed):
        for b in placed[i + 1:]:
            na, nb = a["number"], b["number"]
            if math.dist(a["pin"], b["pin"]) < tol["shared_pin_m"]:
                findings.add("error", "GREEN_SHARED", f"holes {na} and {nb} finish on the same pin; "
                             f"one of them took the other's green", na)
            if math.dist(a["tee"], b["tee"]) < tol["shared_tee_m"]:
                findings.add("warn", "TEE_SHARED", f"holes {na} and {nb} start from the same spot", na)
            if na not in samples or nb not in samples:
                continue
            for first, second in ((a, b), (b, a)):
                pts = samples[first["number"]]
                close = sum(1 for q in pts if polyline_distance(q, second["line"]) <= tol["overlap_distance_m"])
                if close / len(pts) >= tol["overlap_share"]:
                    findings.add("error", "HOLES_OVERLAP",
                                 f"{close / len(pts) * 100:.0f}% of hole {first['number']} runs within "
                                 f"{tol['overlap_distance_m']:.0f} m of hole {second['number']}: "
                                 f"the same hole imported twice", first["number"])
                    break
            else:
                if lines_cross(a["line"], b["line"]):
                    findings.add("info", "HOLES_CROSS", f"holes {na} and {nb} cross", na)


def _check_routing(findings: Findings, placed: list[dict], tol: dict):
    for a, b in zip(placed, placed[1:]):
        walk = math.dist(a["pin"], b["tee"])
        if walk <= tol["routing_warn_m"]:
            continue
        level = "error" if walk > tol["routing_error_m"] else "warn"
        hint = ""
        others = [p for p in placed if p is not b and p is not a]
        if others:
            nearest = min(others, key=lambda p: math.dist(a["pin"], p["tee"]))
            d = math.dist(a["pin"], nearest["tee"])
            if d < walk * 0.5:
                hint = f"; hole {nearest['number']}'s tee is {d:.0f} m away"
        findings.add(level, "ROUTING", f"walk from green {a['number']} to tee {b['number']} is "
                     f"{walk:.0f} m{hint}", a["number"])


def check_course(course_id: str, hole_jsons: list[dict], world: dict | None,
                 scorecard: dict | None, tolerances: dict | None = None) -> Findings:
    """Every check on one course; holes in play order."""
    tol = {**DEFAULT_TOLERANCES, **(tolerances or {})}
    findings = Findings()
    if not hole_jsons:
        findings.add("error", "HOLE_COUNT", "no holes were imported")
        return findings
    placed = placed_holes(hole_jsons, world)
    if world is not None and len(world.get("hole_starts", [])) != len(hole_jsons):
        findings.add("error", "HOLE_COUNT", f"the course world has {len(world.get('hole_starts', []))} "
                     f"hole starts for {len(hole_jsons)} holes")
    _check_numbering(findings, course_id, placed)
    for p in placed:
        _check_loader_contract(findings, p)
        _check_projection(findings, p, tol, world)
        _check_plausibility(findings, p, tol)
        _check_tree_sides(findings, p, tol)
    _check_pairs(findings, placed, tol)
    _check_routing(findings, placed, tol)
    if scorecard:
        _check_scorecard(findings, placed, scorecard, tol)
    return findings


def hole_table(placed: list[dict], scorecard: dict | None) -> list[str]:
    """One line per hole: par, length, direction, and the scorecard's figures beside them."""
    holes = (scorecard or {}).get("holes", {})
    rows = [f"{'hole':>4} {'par':>3} {'length':>7} {'faces':>12} | {'card par':>8} {'card m':>7} "
            f"{'diff':>6} {'card faces':>12}"]
    for p in placed:
        entry = holes.get(str(p["number"]), {})
        length = _hole_length(p)
        actual = bearing_deg(p["tee"], p["pin"])
        card_m = entry.get("metres")
        diff = f"{(length - card_m) / card_m * 100:+5.0f}%" if card_m else "     -"
        expected = expected_bearing(entry)
        card_faces = f"{compass_name(expected)} {expected:3.0f}" if expected is not None else "-"
        rows.append(f"{p['number']:>4} {p['json'].get('par', '?'):>3} {length:>6.0f}m "
                    f"{compass_name(actual) + ' ' + format(actual, '3.0f'):>12} | "
                    f"{entry.get('par', '-'):>8} {card_m or '-':>7} {diff:>6} {card_faces:>12}")
    return rows
