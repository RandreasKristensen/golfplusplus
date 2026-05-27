#!/usr/bin/env python3
"""
osm_golf_convert.py — Convert OSM golf course data to hole JSON files.

Searches OpenStreetMap via the Overpass API and outputs one hole JSON file
per hole, plus a course JSON manifest, matching the golf++ schema.

Usage:
  python osm_golf_convert.py "Aarhus Golf Klub"
  python osm_golf_convert.py --name "Skandinavisk Golf Center"
  python osm_golf_convert.py --id R123456          # OSM relation ID
  python osm_golf_convert.py --lat 56.19 --lon 10.19  # nearest course
  python osm_golf_convert.py --id R123456 -o ../assets/holes --course-out ../assets/courses

Requirements: pip install requests (optional; falls back to Python stdlib)
"""

import argparse
import hashlib
import json
import math
import random
import re
import sys
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

try:
    import requests
except ImportError:
    requests = None

OVERPASS_INSTANCES = [
    "https://overpass-api.de/api/interpreter",
    "https://overpass.kumi.systems/api/interpreter",
]

_HEADERS = {"User-Agent": "osm_golf_convert/1.0 (golf course converter; github.com/RandreasKristensen)"}
EARTH_METERS_PER_DEGREE_LAT = 111_320.0

# ── Overpass queries ──────────────────────────────────────────────────────────

def _query(q: str) -> dict:
    """POST query to Overpass, trying fallback instances on failure."""
    last_err = None
    for url in OVERPASS_INSTANCES:
        if requests is not None:
            try:
                r = requests.post(url, data={"data": q}, timeout=90, headers=_HEADERS)
                r.raise_for_status()
                return r.json()
            except requests.HTTPError as e:
                last_err = e
                print(f"  [warn] {url} returned {e.response.status_code}, trying next...", file=sys.stderr)
            except requests.RequestException as e:
                last_err = e
                print(f"  [warn] {url} unreachable: {e}", file=sys.stderr)
        else:
            try:
                payload = urllib.parse.urlencode({"data": q}).encode("utf-8")
                req = urllib.request.Request(
                    url,
                    data=payload,
                    headers={**_HEADERS, "Content-Type": "application/x-www-form-urlencoded"},
                    method="POST",
                )
                with urllib.request.urlopen(req, timeout=90) as r:
                    return json.loads(r.read().decode("utf-8"))
            except urllib.error.HTTPError as e:
                last_err = e
                print(f"  [warn] {url} returned {e.code}, trying next...", file=sys.stderr)
            except (urllib.error.URLError, TimeoutError, json.JSONDecodeError) as e:
                last_err = e
                print(f"  [warn] {url} unreachable: {e}", file=sys.stderr)
    raise RuntimeError(f"All Overpass instances failed. Last error: {last_err}")


def _normalize_name(name: str) -> str:
    return re.sub(r"[^a-z0-9]+", " ", name.lower()).strip()


def _bounds_from_element(el: dict):
    b = el.get("bounds")
    if b:
        return b
    pts = _element_geom(el)
    if not pts:
        return None
    lats = [p[0] for p in pts]
    lons = [p[1] for p in pts]
    return {"minlat": min(lats), "minlon": min(lons),
            "maxlat": max(lats), "maxlon": max(lons)}


def _bounds_centroid(b: dict) -> tuple[float, float]:
    return ((b["minlat"] + b["maxlat"]) * 0.5,
            (b["minlon"] + b["maxlon"]) * 0.5)


def _latlon_distance_m(a_lat: float, a_lon: float, b_lat: float, b_lon: float) -> float:
    origin_lat = (a_lat + b_lat) * 0.5
    ax, az = _latlon_to_xz(a_lat, a_lon, origin_lat, a_lon)
    bx, bz = _latlon_to_xz(b_lat, b_lon, origin_lat, a_lon)
    return math.hypot(bx - ax, bz - az)


def _element_area_m2(el: dict) -> float:
    pts = _element_geom(el)
    if len(pts) < 3:
        b = _bounds_from_element(el)
        if not b:
            return 0.0
        lat, lon = _bounds_centroid(b)
        x1, z1 = _latlon_to_xz(b["minlat"], b["minlon"], lat, lon)
        x2, z2 = _latlon_to_xz(b["maxlat"], b["maxlon"], lat, lon)
        return abs((x2 - x1) * (z2 - z1))

    origin_lat, origin_lon = _centroid(pts)
    xz = [_latlon_to_xz(lat, lon, origin_lat, origin_lon) for lat, lon in pts]
    area = 0.0
    for a, b in zip(xz, xz[1:] + xz[:1]):
        area += a[0] * b[1] - b[0] * a[1]
    return abs(area) * 0.5


def _point_in_bounds(lat: float, lon: float, bounds: dict, pad_m: float = 0.0) -> bool:
    mid_lat = (bounds["minlat"] + bounds["maxlat"]) * 0.5
    lat_pad = pad_m / EARTH_METERS_PER_DEGREE_LAT
    cos_lat = max(0.01, abs(math.cos(math.radians(mid_lat))))
    lon_pad = pad_m / (EARTH_METERS_PER_DEGREE_LAT * cos_lat)
    return (bounds["minlat"] - lat_pad <= lat <= bounds["maxlat"] + lat_pad and
            bounds["minlon"] - lon_pad <= lon <= bounds["maxlon"] + lon_pad)


def _course_selection_score(el: dict, args) -> tuple:
    tags = el.get("tags", {})
    name = tags.get("name", "")
    b = _bounds_from_element(el)
    area = _element_area_m2(el)
    rel_rank = 0 if el.get("type") == "relation" else 1

    if args.lat is not None and args.lon is not None:
        inside = 0 if b and _point_in_bounds(args.lat, args.lon, b) else 1
        if b:
            c_lat, c_lon = _bounds_centroid(b)
        else:
            c_lat, c_lon = args.lat, args.lon
        return (inside, _latlon_distance_m(args.lat, args.lon, c_lat, c_lon), rel_rank, -area)

    wanted = _normalize_name(args.name or "")
    actual = _normalize_name(name)
    if wanted and actual == wanted:
        name_rank = 0
    elif wanted and (wanted in actual or actual in wanted):
        name_rank = 1
    else:
        name_rank = 2
    return (name_rank, rel_rank, -area, len(actual))


def _rank_course_candidates(elements: list[dict], args) -> list[dict]:
    return sorted(elements, key=lambda el: _course_selection_score(el, args))


def _format_osm_ref(el: dict) -> str:
    return f"{el.get('type', '?')}/{el.get('id', '?')}"


def _find_course(args) -> tuple[dict, str]:
    """Returns (course_element, course_name). Exits on failure."""
    if args.id:
        num = args.id.lstrip("RrWw")
        kind = "way" if args.id.upper().startswith("W") else "relation"
        q = f"[out:json][timeout:30];\n{kind}({num});\nout geom bb;"
    elif args.lat is not None and args.lon is not None:
        q = f"""[out:json][timeout:30];
(
  relation["leisure"="golf_course"](around:8000,{args.lat},{args.lon});
  way["leisure"="golf_course"](around:8000,{args.lat},{args.lon});
);
out geom bb;"""
    else:
        escaped = args.name.replace('"', '\\"')
        q = f"""[out:json][timeout:30];
(
  relation["leisure"="golf_course"]["name"~"{escaped}",i];
  way["leisure"="golf_course"]["name"~"{escaped}",i];
);
out geom bb;"""

    data = _query(q)
    els = data.get("elements", [])
    if not els:
        print("No golf course found. Check spelling or try --lat/--lon.", file=sys.stderr)
        sys.exit(1)

    ranked = _rank_course_candidates(els, args)
    el = ranked[0]
    name = el.get("tags", {}).get("name", "Unknown Course")
    print(f"  Selected OSM {_format_osm_ref(el)}: {name}", file=sys.stderr)

    close = []
    best_score = _course_selection_score(el, args)
    for alt in ranked[1:4]:
        alt_score = _course_selection_score(alt, args)
        if args.lat is not None and args.lon is not None:
            if alt_score[0] == best_score[0] and alt_score[1] <= best_score[1] + 750.0:
                close.append(alt)
        elif alt_score[:2] == best_score[:2]:
            close.append(alt)
    if close:
        print("  [warn] close course alternatives were found:", file=sys.stderr)
        for alt in close:
            alt_name = alt.get("tags", {}).get("name", "Unknown Course")
            print(f"    {_format_osm_ref(alt)}: {alt_name}", file=sys.stderr)
    return el, name


def _bbox_string_for_course(course_el: dict, pad_m: float = 35.0) -> str:
    b = _bounds_from_element(course_el)
    if not b:
        print("Course has no geometry in OSM data.", file=sys.stderr)
        sys.exit(1)
    mid_lat = (b["minlat"] + b["maxlat"]) * 0.5
    lat_pad = pad_m / EARTH_METERS_PER_DEGREE_LAT
    cos_lat = max(0.01, abs(math.cos(math.radians(mid_lat))))
    lon_pad = pad_m / (EARTH_METERS_PER_DEGREE_LAT * cos_lat)
    return f"{b['minlat'] - lat_pad},{b['minlon'] - lon_pad},{b['maxlat'] + lat_pad},{b['maxlon'] + lon_pad}"


def _course_footprint_polygons(course_el: dict) -> list[list[tuple[float, float]]]:
    polygons = []
    geom = course_el.get("geometry", [])
    if len(geom) >= 3:
        polygons.append([(pt["lat"], pt["lon"]) for pt in geom])
    for member in course_el.get("members", []):
        geom = member.get("geometry", [])
        if len(geom) >= 3 and member.get("role", "outer") in ("", "outer", "outline", "perimeter"):
            polygons.append([(pt["lat"], pt["lon"]) for pt in geom])
    return polygons


def _point_in_polygon_xz(pt, poly) -> bool:
    x, z = pt
    inside = False
    j = len(poly) - 1
    for i in range(len(poly)):
        xi, zi = poly[i]
        xj, zj = poly[j]
        crosses = ((zi > z) != (zj > z)) and (x < (xj - xi) * (z - zi) / ((zj - zi) or 1e-9) + xi)
        if crosses:
            inside = not inside
        j = i
    return inside


def _point_to_polygon_distance_xz(pt, poly) -> float:
    if _point_in_polygon_xz(pt, poly):
        return 0.0
    return min(_point_segment_distance(pt, a, b) for a, b in zip(poly, poly[1:] + poly[:1]))


def _element_in_course_footprint(el: dict, course_el: dict, buffer_m: float = 45.0) -> bool:
    pts = _element_geom(el)
    if not pts:
        return False

    polygons = _course_footprint_polygons(course_el)
    b = _bounds_from_element(course_el)
    if not polygons:
        return bool(b and any(_point_in_bounds(lat, lon, b, buffer_m) for lat, lon in pts))

    origin_lat, origin_lon = _centroid([p for poly in polygons for p in poly])
    poly_xz = [[_latlon_to_xz(lat, lon, origin_lat, origin_lon) for lat, lon in poly] for poly in polygons]
    for lat, lon in pts:
        pt = _latlon_to_xz(lat, lon, origin_lat, origin_lon)
        if any(_point_to_polygon_distance_xz(pt, poly) <= buffer_m for poly in poly_xz):
            return True
    return False


def _is_golf_feature(el: dict) -> bool:
    return "golf" in el.get("tags", {})


def _is_tree_feature(el: dict) -> bool:
    tags = el.get("tags", {})
    return (tags.get("natural") in ("tree", "tree_row", "wood", "scrub") or
            tags.get("landuse") == "forest")


def _is_path_feature(el: dict) -> bool:
    tags = el.get("tags", {})
    highway = tags.get("highway")
    return tags.get("golf") == "cartpath" or highway in ("path", "service", "track", "footway", "pedestrian")


def _dedupe_elements(elements: list[dict]) -> list[dict]:
    out = []
    seen = set()
    for el in elements:
        key = _element_key(el)
        if key in seen:
            continue
        seen.add(key)
        out.append(el)
    return out


def _fetch_elements(course_el: dict) -> tuple[list, list, list]:
    """Fetch golf, vegetation, and path/service elements scoped to the selected course."""
    relation_elements = []
    used_relation_scope = False
    if course_el.get("type") == "relation":
        rel_id = course_el["id"]
        q = f"""[out:json][timeout:90];
relation({rel_id})->.course;
(
  .course;
  >;
);
out geom;"""
        data = _query(q)
        relation_elements = data.get("elements", [])
        used_relation_scope = bool(relation_elements)

    if course_el.get("type") == "relation":
        rel_id = course_el["id"]
        q = f"""[out:json][timeout:90];
relation({rel_id})->.course;
.course map_to_area->.courseArea;
(
  relation["golf"](area.courseArea);
  way["golf"](area.courseArea);
  node["golf"](area.courseArea);
  node["natural"="tree"](area.courseArea);
  way["natural"="tree_row"](area.courseArea);
  way["natural"="wood"](area.courseArea);
  relation["natural"="wood"](area.courseArea);
  way["landuse"="forest"](area.courseArea);
  relation["landuse"="forest"](area.courseArea);
  way["natural"="scrub"](area.courseArea);
  relation["natural"="scrub"](area.courseArea);
  way["highway"~"^(path|service|track|footway|pedestrian)$"](area.courseArea);
  relation["highway"~"^(path|service|track|footway|pedestrian)$"](area.courseArea);
  way["golf"="cartpath"](area.courseArea);
  relation["golf"="cartpath"](area.courseArea);
);
out geom;"""
    else:
        bbox = _bbox_string_for_course(course_el)
        q = f"""[out:json][timeout:90];
(
  relation["golf"]({bbox});
  way["golf"]({bbox});
  node["golf"]({bbox});
  node["natural"="tree"]({bbox});
  way["natural"="tree_row"]({bbox});
  way["natural"="wood"]({bbox});
  relation["natural"="wood"]({bbox});
  way["landuse"="forest"]({bbox});
  relation["landuse"="forest"]({bbox});
  way["natural"="scrub"]({bbox});
  relation["natural"="scrub"]({bbox});
  way["highway"~"^(path|service|track|footway|pedestrian)$"]({bbox});
  relation["highway"~"^(path|service|track|footway|pedestrian)$"]({bbox});
  way["golf"="cartpath"]({bbox});
  relation["golf"="cartpath"]({bbox});
);
out geom;"""

    data = _query(q)
    fetched = _dedupe_elements(relation_elements + data.get("elements", []))
    scoped = [el for el in fetched if _element_in_course_footprint(el, course_el)]
    golf = [el for el in scoped if _is_golf_feature(el)]
    trees = [el for el in scoped if _is_tree_feature(el)]
    paths = [el for el in scoped if _is_path_feature(el)]

    if course_el.get("type") == "relation" and not used_relation_scope:
        print("  [warn] relation members were unavailable; relied on course-area query", file=sys.stderr)
    if not scoped and course_el.get("type") != "relation":
        print("  [warn] course has weak boundary data; bbox fallback may miss edge features", file=sys.stderr)
    return golf, trees, paths


# ── Geometry helpers ──────────────────────────────────────────────────────────

def _latlon_to_xz(lat, lon, origin_lat, origin_lon) -> tuple[float, float]:
    """Project WGS84 → local metres: X=East, Z=North, origin at tee."""
    cos_lat = math.cos(math.radians(origin_lat))
    x = (lon - origin_lon) * cos_lat * EARTH_METERS_PER_DEGREE_LAT
    z = (lat - origin_lat) * EARTH_METERS_PER_DEGREE_LAT
    return x, z


def _element_geom(el: dict) -> list[tuple[float, float]]:
    """Return list of (lat, lon) for any element type."""
    if el["type"] == "node":
        return [(el["lat"], el["lon"])]
    pts = [(pt["lat"], pt["lon"]) for pt in el.get("geometry", [])]
    if el["type"] == "relation":
        for member in el.get("members", []):
            pts.extend((pt["lat"], pt["lon"]) for pt in member.get("geometry", []))
    return pts


def _to_xz_list(elements, origin_lat, origin_lon) -> list[tuple[float, float]]:
    pts = []
    for el in elements:
        for lat, lon in _element_geom(el):
            pts.append(_latlon_to_xz(lat, lon, origin_lat, origin_lon))
    return pts


def _centroid(pts) -> tuple[float, float]:
    n = len(pts)
    return (sum(p[0] for p in pts) / n, sum(p[1] for p in pts) / n) if n else (0, 0)


def _ritter_circle(pts) -> tuple[float, float, float]:
    """Ritter's approximate minimum bounding circle → (cx, cz, radius)."""
    if not pts:
        return 0.0, 0.0, 5.0
    p = pts[0]
    q = max(pts, key=lambda t: (t[0]-p[0])**2 + (t[1]-p[1])**2)
    r = max(pts, key=lambda t: (t[0]-q[0])**2 + (t[1]-q[1])**2)
    cx, cz = (q[0]+r[0]) / 2, (q[1]+r[1]) / 2
    rad = math.hypot(q[0]-r[0], q[1]-r[1]) / 2
    for pt in pts:
        d = math.hypot(pt[0]-cx, pt[1]-cz)
        if d > rad:
            # Grow circle to include pt
            new_rad = (rad + d) / 2
            scale = (d - new_rad) / d if d > 0 else 0
            cx += (pt[0] - cx) * scale
            cz += (pt[1] - cz) * scale
            rad = new_rad
    return cx, cz, max(rad, 2.0)


def _aabb(pts) -> tuple:
    return (min(p[0] for p in pts), min(p[1] for p in pts),
            max(p[0] for p in pts), max(p[1] for p in pts))


def _fairway_centerline(poly_pts, tee, pin, n=5) -> list[tuple[float, float]]:
    """
    Slice the fairway polygon perpendicular to tee→pin at N positions,
    take the midpoint of each cross-section as a spline control point.
    Falls back to a straight line if polygon data is insufficient.
    """
    if len(poly_pts) < 3:
        return [tee] + [
            (tee[0] + (pin[0]-tee[0])*i/(n-1),
             tee[1] + (pin[1]-tee[1])*i/(n-1))
            for i in range(1, n)
        ]

    tx, tz = tee
    dx, dz = pin[0]-tx, pin[1]-tz
    length = math.hypot(dx, dz) or 1
    # Unit vector along hole (ua) and perpendicular (up)
    ua = (dx/length, dz/length)
    up = (-ua[1], ua[0])

    def proj_a(pt): return (pt[0]-tx)*ua[0] + (pt[1]-tz)*ua[1]
    def proj_p(pt): return (pt[0]-tx)*up[0] + (pt[1]-tz)*up[1]

    t_vals = [proj_a(p) for p in poly_pts]
    t_min, t_max = min(t_vals), max(t_vals)
    slice_w = (t_max - t_min) / n

    result = []
    for i in range(n):
        t_mid = t_min + slice_w * (i + 0.5)
        # Points in this slice
        sp = [proj_p(p) for p, tv in zip(poly_pts, t_vals)
              if abs(tv - t_mid) <= slice_w * 0.75]
        if len(sp) < 2:
            sp = [proj_p(p) for p in poly_pts]  # use all if slice is empty
        mid_p = (min(sp) + max(sp)) / 2
        wx = tx + t_mid*ua[0] + mid_p*up[0]
        wz = tz + t_mid*ua[1] + mid_p*up[1]
        result.append((wx, wz))
    return result


def _fairway_width(poly_pts, tee, pin) -> float:
    """Estimate average fairway width perpendicular to the tee→pin axis."""
    tx, tz = tee
    dx, dz = pin[0]-tx, pin[1]-tz
    length = math.hypot(dx, dz) or 1
    ua = (dx/length, dz/length)
    up = (-ua[1], ua[0])

    def proj_a(pt): return (pt[0]-tx)*ua[0] + (pt[1]-tz)*ua[1]
    def proj_p(pt): return (pt[0]-tx)*up[0] + (pt[1]-tz)*up[1]

    t_vals = [proj_a(p) for p in poly_pts]
    t_min, t_max = min(t_vals), max(t_vals)
    n = 6
    widths = []
    for i in range(n):
        t = t_min + (t_max-t_min)*(i+0.5)/n
        sw = (t_max-t_min)/n * 0.75
        perps = [proj_p(p) for p, tv in zip(poly_pts, t_vals) if abs(tv-t) <= sw]
        if len(perps) >= 2:
            widths.append(max(perps) - min(perps))
    return round(sum(widths)/len(widths), 1) if widths else 20.0


def _polyline_length(pts) -> float:
    return sum(math.hypot(b[0]-a[0], b[1]-a[1]) for a, b in zip(pts, pts[1:]))


def _resample_polyline(pts, n=5) -> list[tuple[float, float]]:
    """Return N evenly-spaced points along a line; duplicates if it is degenerate."""
    if not pts:
        return []
    if len(pts) == 1:
        return [pts[0]] * n

    total = _polyline_length(pts)
    if total <= 0.001:
        return [pts[0]] * n

    result = []
    targets = [total * i / (n - 1) for i in range(n)]
    seg_start_dist = 0.0
    seg_index = 0

    for target in targets:
        while seg_index < len(pts) - 2:
            a = pts[seg_index]
            b = pts[seg_index + 1]
            seg_len = math.hypot(b[0]-a[0], b[1]-a[1])
            if seg_start_dist + seg_len >= target:
                break
            seg_start_dist += seg_len
            seg_index += 1

        a = pts[seg_index]
        b = pts[seg_index + 1]
        seg_len = math.hypot(b[0]-a[0], b[1]-a[1])
        t = 0.0 if seg_len <= 0.001 else (target - seg_start_dist) / seg_len
        result.append((a[0] + (b[0]-a[0])*t, a[1] + (b[1]-a[1])*t))

    return result


def _point_segment_distance(pt, a, b) -> float:
    ax, az = a
    bx, bz = b
    px, pz = pt
    dx, dz = bx - ax, bz - az
    denom = dx*dx + dz*dz
    if denom <= 0.001:
        return math.hypot(px-ax, pz-az)
    t = max(0.0, min(1.0, ((px-ax)*dx + (pz-az)*dz) / denom))
    cx, cz = ax + dx*t, az + dz*t
    return math.hypot(px-cx, pz-cz)


def _point_polyline_distance(pt, line_pts) -> float:
    if not line_pts:
        return float("inf")
    if len(line_pts) == 1:
        return math.hypot(pt[0]-line_pts[0][0], pt[1]-line_pts[0][1])
    return min(_point_segment_distance(pt, a, b) for a, b in zip(line_pts, line_pts[1:]))


# ── Hole grouping ─────────────────────────────────────────────────────────────

def _empty_hole() -> dict:
    return {"lines": [], "tees": [], "pins": [], "fairways": [], "greens": [],
            "bunkers": [], "waters": [], "trees_abs": [], "tags": {}}


def _classify_into(el: dict, h: dict):
    tag = el.get("tags", {}).get("golf", "")
    h["tags"].update(el.get("tags", {}))
    if tag == "hole":                                   h["lines"].append(el)
    elif tag == "tee":                                  h["tees"].append(el)
    elif tag in ("pin", "flagstick"):                   h["pins"].append(el)
    elif tag == "fairway":                              h["fairways"].append(el)
    elif tag == "green":                                h["greens"].append(el)
    elif tag in ("bunker", "sand"):                     h["bunkers"].append(el)
    elif tag in ("water_hazard", "lateral_water_hazard", "water"): h["waters"].append(el)


def _element_key(el: dict) -> tuple[str, int]:
    return (el["type"], el["id"])


def _parse_hole_num(tags: dict):
    ref = tags.get("ref") or tags.get("hole")
    if ref is None:
        return None
    s = str(ref).strip()
    m = re.fullmatch(r"\d{1,2}", s)
    if not m:
        m = re.fullmatch(r"(?:hole|hul)\s*(\d{1,2})", s, re.IGNORECASE)
    if not m:
        return None
    num = int(m.group(1) if m.lastindex else m.group())
    return num if 1 <= num <= 36 else None


def _looks_like_course_hole_line(el: dict) -> bool:
    return el.get("tags", {}).get("golf") == "hole" and len(_element_geom(el)) >= 2


def _hole_line_xz(h: dict, origin_lat: float, origin_lon: float) -> list[tuple[float, float]]:
    lines = []
    for line in h["lines"]:
        pts = _to_xz_list([line], origin_lat, origin_lon)
        if len(pts) >= 2:
            lines.append(pts)
    if not lines:
        return []
    return max(lines, key=_polyline_length)


def _hole_anchor_xz(h: dict, origin_lat: float, origin_lon: float) -> tuple[float, float]:
    line = _hole_line_xz(h, origin_lat, origin_lon)
    if line:
        return _centroid(line)
    pts = _to_xz_list(h["tees"] + h["pins"] + h["greens"] + h["fairways"], origin_lat, origin_lon)
    return _centroid(pts) if pts else (0.0, 0.0)


def _oriented_hole_line_xz(h: dict, origin_lat: float, origin_lon: float) -> list[tuple[float, float]]:
    line = _hole_line_xz(h, origin_lat, origin_lon)
    if len(line) < 2:
        return line

    tee_pts = _to_xz_list(h["tees"], origin_lat, origin_lon)
    if tee_pts:
        tee = _centroid(tee_pts)
        start_d = math.hypot(tee[0]-line[0][0], tee[1]-line[0][1])
        end_d = math.hypot(tee[0]-line[-1][0], tee[1]-line[-1][1])
        return list(reversed(line)) if end_d < start_d else line

    pin_pts = _to_xz_list(h["pins"], origin_lat, origin_lon)
    if not pin_pts and h["greens"]:
        pin_pts = _to_xz_list(h["greens"][:1], origin_lat, origin_lon)
    if pin_pts:
        pin = _centroid(pin_pts)
        start_d = math.hypot(pin[0]-line[0][0], pin[1]-line[0][1])
        end_d = math.hypot(pin[0]-line[-1][0], pin[1]-line[-1][1])
        return list(reversed(line)) if start_d < end_d else line

    return line


def _assign_to_existing_holes(unassigned: list, holes: dict, origin_lat: float, origin_lon: float):
    hole_shapes = {}
    for num, h in holes.items():
        line = _oriented_hole_line_xz(h, origin_lat, origin_lon)
        anchor = _hole_anchor_xz(h, origin_lat, origin_lon)
        hole_shapes[num] = (line, anchor)

    for el in unassigned:
        tag = el.get("tags", {}).get("golf", "")
        if tag == "hole":
            continue
        pts = _to_xz_list([el], origin_lat, origin_lon)
        if not pts:
            continue
        pt = _centroid(pts)

        best_num = None
        best_d = float("inf")
        for num, (line, anchor) in hole_shapes.items():
            if tag == "tee" and line:
                d = min(math.hypot(pt[0]-line[0][0], pt[1]-line[0][1]),
                        math.hypot(pt[0]-line[-1][0], pt[1]-line[-1][1]))
            elif tag in ("green", "pin", "flagstick") and line:
                d = min(math.hypot(pt[0]-line[0][0], pt[1]-line[0][1]),
                        math.hypot(pt[0]-line[-1][0], pt[1]-line[-1][1]))
            elif line:
                d = _point_polyline_distance(pt, line)
            else:
                d = math.hypot(pt[0]-anchor[0], pt[1]-anchor[1])

            if d < best_d:
                best_d = d
                best_num = num

        threshold = {
            "tee": 90.0,
            "green": 120.0,
            "pin": 120.0,
            "flagstick": 120.0,
            "fairway": 140.0,
            "bunker": 160.0,
            "sand": 160.0,
            "water_hazard": 180.0,
            "lateral_water_hazard": 180.0,
            "water": 180.0,
        }.get(tag, 0.0)

        if best_num is not None and best_d <= threshold:
            _classify_into(el, holes[best_num])


def group_holes(elements: list) -> dict:
    """
    Returns {hole_number: hole_dict}.

    Strategy (in priority order):
      1. golf=hole relations with members
      2. Elements with ref=N or hole=N tag
      3. Spatial proximity to tee nodes
    """
    holes: dict[int, dict] = {}
    by_id = {(el["type"], el["id"]): el for el in elements}
    assigned = set()

    # ── Strategy 1: hole relations ────────────────────────────────────────────
    hole_rels = [el for el in elements
                 if el["type"] == "relation"
                 and el.get("tags", {}).get("golf") == "hole"]

    for rel in hole_rels:
        tags = rel.get("tags", {})
        num = _parse_hole_num(tags)
        if num is None:
            num = hole_rels.index(rel) + 1

        h = holes.setdefault(num, _empty_hole())
        h["tags"].update(tags)

        for member in rel.get("members", []):
            key = (member["type"], member["ref"])
            el = by_id.get(key)
            if el:
                _classify_into(el, h)
                assigned.add(key)

        # Some OSM hole relations carry their own geometry. Keep it as a
        # usable centerline when members are incomplete or untagged.
        if _element_geom(rel):
            _classify_into(rel, h)

    # ── Strategy 2: ref tags ──────────────────────────────────────────────────
    for el in elements:
        eid = _element_key(el)
        if eid in assigned:
            continue
        tags = el.get("tags", {})
        num = _parse_hole_num(tags)
        if num is not None:
            h = holes.setdefault(num, _empty_hole())
            _classify_into(el, h)
            assigned.add(eid)

    if holes:
        unnumbered_lines = [
            el for el in elements
            if _element_key(el) not in assigned
            and _looks_like_course_hole_line(el)
            and _parse_hole_num(el.get("tags", {})) is None
            and not (el.get("tags", {}).get("ref") or el.get("tags", {}).get("hole"))
        ]
        max_num = max(holes.keys())
        missing = [num for num in range(1, max_num + 1) if num not in holes]
        if max_num == 18 and len(missing) == 1 and len(unnumbered_lines) == 1:
            h = holes.setdefault(missing[0], _empty_hole())
            _classify_into(unnumbered_lines[0], h)
            assigned.add(_element_key(unnumbered_lines[0]))

    # ── Strategy 3: spatial attachment / tee clustering fallback ──────────────
    unassigned = [el for el in elements if _element_key(el) not in assigned]

    if holes:
        all_latlon = []
        for el in elements:
            all_latlon.extend(_element_geom(el))
        if all_latlon:
            origin_lat = sum(p[0] for p in all_latlon) / len(all_latlon)
            origin_lon = sum(p[1] for p in all_latlon) / len(all_latlon)
            _assign_to_existing_holes(unassigned, holes, origin_lat, origin_lon)
        return holes

    tee_nodes = [el for el in unassigned if el.get("tags", {}).get("golf") == "tee"]

    if tee_nodes and unassigned:
        # Assign each unassigned element to its nearest tee
        tee_positions = []
        for i, tee in enumerate(tee_nodes, start=max(holes.keys(), default=0)+1):
            geom = _element_geom(tee)
            if geom:
                tee_positions.append((i, geom[0][0], geom[0][1]))

        for el in unassigned:
            geom = _element_geom(el)
            if not geom:
                continue
            el_lat = sum(p[0] for p in geom) / len(geom)
            el_lon = sum(p[1] for p in geom) / len(geom)
            best_num, best_d = 1, float("inf")
            for num, t_lat, t_lon in tee_positions:
                d = _latlon_distance_m(el_lat, el_lon, t_lat, t_lon)
                if d < best_d:
                    best_d, best_num = d, num
            if best_d < 500:
                h = holes.setdefault(best_num, _empty_hole())
                _classify_into(el, h)

    return holes


def _stable_int_seed(*parts) -> int:
    h = hashlib.sha256()
    for part in parts:
        h.update(str(part).encode("utf-8"))
        h.update(b"\0")
    return int.from_bytes(h.digest()[:8], "big")


def _is_wooded_area(el: dict) -> bool:
    tags = el.get("tags", {})
    return tags.get("natural") in ("wood", "scrub") or tags.get("landuse") == "forest"


def _is_tree_row(el: dict) -> bool:
    return el.get("tags", {}).get("natural") == "tree_row"


def _point_in_element_xz(pt, el: dict, origin_lat: float, origin_lon: float, pad_m: float = 0.0) -> bool:
    pts = _to_xz_list([el], origin_lat, origin_lon)
    if not pts:
        return False
    if len(pts) >= 3:
        return _point_to_polygon_distance_xz(pt, pts) <= pad_m
    if len(pts) == 1:
        return math.hypot(pt[0] - pts[0][0], pt[1] - pts[0][1]) <= pad_m
    return _point_polyline_distance(pt, pts) <= pad_m


def _tree_blocked_by_zone(pt, h: dict, origin_lat: float, origin_lon: float) -> bool:
    for el in h["greens"] + h["bunkers"] + h["waters"]:
        if _point_in_element_xz(pt, el, origin_lat, origin_lon, pad_m=3.0):
            return True
    return False


def _tree_blocked_by_fairway_core(pt, h: dict, line, origin_lat: float, origin_lon: float) -> bool:
    if not line:
        return False
    fw_pts = _to_xz_list(h["fairways"], origin_lat, origin_lon)
    if len(fw_pts) >= 4 and len(line) >= 2:
        width = _fairway_width(fw_pts, line[0], line[-1])
        core = max(8.0, min(18.0, width * 0.45))
    else:
        core = 12.0
    return _point_polyline_distance(pt, line) <= core


def _tree_allowed_for_hole(pt, h: dict, line, origin_lat: float, origin_lon: float) -> bool:
    return not _tree_blocked_by_zone(pt, h, origin_lat, origin_lon) and not _tree_blocked_by_fairway_core(pt, h, line, origin_lat, origin_lon)


def _sample_polyline_points(pts, spacing_m: float = 12.0) -> list[tuple[float, float]]:
    total = _polyline_length(pts)
    if total <= 0.001:
        return []
    count = max(1, min(40, int(total / spacing_m)))
    return _resample_polyline(pts, count)


def _sample_wooded_polygon_trees(el: dict, origin_lat: float, origin_lon: float,
                                 seed: int, max_count: int = 24) -> list[tuple[float, float]]:
    pts = _to_xz_list([el], origin_lat, origin_lon)
    if len(pts) < 3:
        return []
    minx, minz, maxx, maxz = _aabb(pts)
    area = _element_area_m2(el)
    target = max(1, min(max_count, int(area / 650.0)))
    rng = random.Random(seed)
    samples = []
    attempts = target * 30
    while len(samples) < target and attempts > 0:
        attempts -= 1
        pt = (rng.uniform(minx, maxx), rng.uniform(minz, maxz))
        if _point_in_polygon_xz(pt, pts):
            samples.append(pt)
    return samples


def _nearest_hole_for_tree(pt, hole_shapes: dict[int, tuple[list, tuple]]) -> tuple[int | None, float]:
    best_num = None
    best_d = float("inf")
    for num, (line, anchor) in hole_shapes.items():
        d = _point_polyline_distance(pt, line) if line else math.hypot(pt[0] - anchor[0], pt[1] - anchor[1])
        if d < best_d:
            best_num = num
            best_d = d
    return best_num, best_d


def assign_trees_to_holes(holes: dict, tree_elements: list, origin_lat: float, origin_lon: float,
                          course_key: str):
    hole_shapes = {}
    for num, h in holes.items():
        line = _oriented_hole_line_xz(h, origin_lat, origin_lon)
        if not line:
            line = _to_xz_list(h["fairways"][:1], origin_lat, origin_lon)
        anchor = _hole_anchor_xz(h, origin_lat, origin_lon)
        hole_shapes[num] = (line, anchor)

    explicit_points = []
    wooded = []
    for el in tree_elements:
        tags = el.get("tags", {})
        if el.get("type") == "node" and tags.get("natural") == "tree":
            explicit_points.extend(_to_xz_list([el], origin_lat, origin_lon))
        elif _is_tree_row(el):
            pts = _to_xz_list([el], origin_lat, origin_lon)
            if len(pts) >= 2:
                explicit_points.extend(_sample_polyline_points(pts))
        elif _is_wooded_area(el):
            wooded.append(el)

    for pt in explicit_points:
        num, dist = _nearest_hole_for_tree(pt, hole_shapes)
        if num is None or dist > 95.0:
            continue
        line, _anchor = hole_shapes[num]
        if _tree_allowed_for_hole(pt, holes[num], line, origin_lat, origin_lon):
            holes[num]["trees_abs"].append(pt)

    for el in wooded:
        for num, (line, _anchor) in hole_shapes.items():
            seed = _stable_int_seed(course_key, num, el.get("type"), el.get("id"))
            for pt in _sample_wooded_polygon_trees(el, origin_lat, origin_lon, seed):
                dist = _point_polyline_distance(pt, line) if line else 0.0
                if dist > 85.0:
                    continue
                if _tree_allowed_for_hole(pt, holes[num], line, origin_lat, origin_lon):
                    holes[num]["trees_abs"].append(pt)

    for num, h in holes.items():
        deduped = []
        seen = set()
        for pt in h["trees_abs"]:
            key = (round(pt[0] / 2.0), round(pt[1] / 2.0))
            if key in seen:
                continue
            seen.add(key)
            deduped.append(pt)
        line = hole_shapes[num][0]
        deduped.sort(key=lambda p: (_point_polyline_distance(p, line) if line else 0.0, p[0], p[1]))
        h["trees_abs"] = deduped[:60]


# ── Hole → JSON ───────────────────────────────────────────────────────────────

def _r(v): return round(v, 2)


def hole_to_json(hole_num: int, h: dict, origin_lat: float, origin_lon: float,
                 course_id: str) -> dict:

    line_pts = _oriented_hole_line_xz(h, origin_lat, origin_lon)

    # ── tee position ──────────────────────────────────────────────────────────
    tee_latlon = None
    if h["tees"]:
        geom = _element_geom(h["tees"][0])
        if geom:
            tee_latlon = geom[0]

    if not tee_latlon:
        if line_pts:
            # Convert the local line start back to lat/lon only for the shared
            # tee conversion path below.
            x, z = line_pts[0]
            cos_lat = math.cos(math.radians(origin_lat)) or 1.0
            tee_latlon = (origin_lat + z / EARTH_METERS_PER_DEGREE_LAT,
                          origin_lon + x / (cos_lat * EARTH_METERS_PER_DEGREE_LAT))
        elif h["fairways"]:
            fw_pts = _to_xz_list(h["fairways"], origin_lat, origin_lon)
            if fw_pts:
                x, z = min(fw_pts, key=lambda p: math.hypot(p[0], p[1]))
                cos_lat = math.cos(math.radians(origin_lat)) or 1.0
                tee_latlon = (origin_lat + z / EARTH_METERS_PER_DEGREE_LAT,
                              origin_lon + x / (cos_lat * EARTH_METERS_PER_DEGREE_LAT))
            else:
                print(f"  [warn] hole {hole_num}: no tee, using course origin", file=sys.stderr)
                tee_latlon = (origin_lat, origin_lon)
        else:
            print(f"  [warn] hole {hole_num}: no tee, using course origin", file=sys.stderr)
            tee_latlon = (origin_lat, origin_lon)

    tee_x, tee_z = _latlon_to_xz(tee_latlon[0], tee_latlon[1], origin_lat, origin_lon)

    # ── pin position ──────────────────────────────────────────────────────────
    pin_x, pin_z = None, None

    if h["pins"]:
        geom = _element_geom(h["pins"][0])
        if geom:
            pin_x, pin_z = _latlon_to_xz(geom[0][0], geom[0][1], origin_lat, origin_lon)

    if pin_x is None and h["greens"]:
        gpts = _to_xz_list(h["greens"][:1], origin_lat, origin_lon)
        if gpts:
            pin_x, pin_z = _centroid(gpts)

    if pin_x is None and line_pts:
        pin_x, pin_z = line_pts[-1]

    if pin_x is None:
        print(f"  [warn] hole {hole_num}: no pin/green, estimating 100m ahead", file=sys.stderr)
        pin_x, pin_z = tee_x, tee_z + 100.0

    # ── fairway spline ────────────────────────────────────────────────────────
    fw_pts = _to_xz_list(h["fairways"], origin_lat, origin_lon)
    tee_xz = (tee_x, tee_z)
    pin_xz = (pin_x, pin_z)

    if len(fw_pts) >= 4:
        ctrl_xz = _fairway_centerline(fw_pts, tee_xz, pin_xz, n=5)
        width = _fairway_width(fw_pts, tee_xz, pin_xz)
        rough_width = round(width * 1.55, 1)
    elif len(line_pts) >= 2:
        ctrl_xz = _resample_polyline(line_pts, n=5)
        width = 20.0
        rough_width = 32.0
    else:
        # Straight interpolation: 4 evenly-spaced points
        ctrl_xz = [(tee_x + (pin_x-tee_x)*i/3,
                    tee_z + (pin_z-tee_z)*i/3) for i in range(4)]
        width = 20.0
        rough_width = 32.0
        if not h["fairways"]:
            print(f"  [warn] hole {hole_num}: no fairway data, using straight spline", file=sys.stderr)

    # Control points relative to this hole's tee (which is [0,0,0])
    def rel_xyz(xz): return [_r(xz[0]-tee_x), 0.0, _r(xz[1]-tee_z)]

    # ── material zones ────────────────────────────────────────────────────────
    zones = []

    for el in h["greens"]:
        pts = _to_xz_list([el], origin_lat, origin_lon)
        if pts:
            cx, cz, r = _ritter_circle(pts)
            zones.append({
                "type": "green",
                "center": [_r(cx-tee_x), 0, _r(cz-tee_z)],
                "radius": _r(r)
            })

    for el in h["bunkers"]:
        pts = _to_xz_list([el], origin_lat, origin_lon)
        if pts:
            cx, cz, r = _ritter_circle(pts)
            zones.append({
                "type": "bunker",
                "center": [_r(cx-tee_x), 0, _r(cz-tee_z)],
                "radius": _r(r)
            })

    for el in h["waters"]:
        pts = _to_xz_list([el], origin_lat, origin_lon)
        if pts:
            minx, minz, maxx, maxz = _aabb(pts)
            zones.append({
                "type": "water",
                "bounds": [
                    [_r(minx-tee_x), 0, _r(minz-tee_z)],
                    [_r(maxx-tee_x), 0, _r(maxz-tee_z)]
                ]
            })

    # ── par estimation ────────────────────────────────────────────────────────
    par = int(h["tags"].get("par", 0))
    if not par:
        dist = math.hypot(pin_x-tee_x, pin_z-tee_z)
        par = 3 if dist < 180 else (4 if dist < 400 else 5)

    trees = [{
        "position": [_r(x - tee_x), 0.0, _r(z - tee_z)],
        "trunk_radius": 0.35,
        "trunk_height": 2.4,
        "leaf_radius": 1.6,
        "leaf_height": 3.2
    } for x, z in h.get("trees_abs", [])]

    return {
        "id": f"{course_id}_h{hole_num:02d}",
        "name": h["tags"].get("name", f"Hole {hole_num}"),
        "par": par,
        "wind_seed": random.randint(1, 9999),
        "tee": [0.0, 0.0, 0.0],
        "pin": [_r(pin_x-tee_x), 0.0, _r(pin_z-tee_z)],
        "spline": {
            "control_points": [rel_xyz(p) for p in ctrl_xz],
            "width": width,
            "rough_width": rough_width
        },
        "material_zones": zones,
        "trees": trees
    }


def _hole_json_path_length(h_json: dict) -> float:
    pts = [(p[0], p[2]) for p in h_json.get("spline", {}).get("control_points", [])]
    return _polyline_length(pts)


def _scale_warnings(h_json: dict) -> list[str]:
    pin = h_json.get("pin", [0.0, 0.0, 0.0])
    direct = math.hypot(pin[0], pin[2])
    path = _hole_json_path_length(h_json)
    width = h_json.get("spline", {}).get("width", 0.0)
    warnings = []
    if direct < 45.0 or direct > 680.0:
        warnings.append(f"tee-to-pin distance {direct:.0f}m is suspicious")
    if path < 45.0 or path > 780.0:
        warnings.append(f"spline path {path:.0f}m is suspicious")
    if direct > 0 and path / direct > 2.2:
        warnings.append(f"spline path is {path / direct:.1f}x direct distance")
    if width < 6.0 or width > 85.0:
        warnings.append(f"fairway width {width:.1f}m is suspicious")
    return warnings


# ── CLI ───────────────────────────────────────────────────────────────────────

def _xyz(xz: tuple[float, float]) -> list[float]:
    return [_r(xz[0]), 0.0, _r(xz[1])]


def _hole_tee_xz(h: dict, origin_lat: float, origin_lon: float) -> tuple[float, float]:
    tee_pts = _to_xz_list(h["tees"], origin_lat, origin_lon)
    if tee_pts:
        return _centroid(tee_pts)
    line = _oriented_hole_line_xz(h, origin_lat, origin_lon)
    if line:
        return line[0]
    return _hole_anchor_xz(h, origin_lat, origin_lon)


def _hole_return_xz(h: dict, origin_lat: float, origin_lon: float) -> tuple[float, float]:
    pin_pts = _to_xz_list(h["pins"], origin_lat, origin_lon)
    if pin_pts:
        return _centroid(pin_pts)
    green_pts = _to_xz_list(h["greens"][:1], origin_lat, origin_lon)
    if green_pts:
        return _centroid(green_pts)
    line = _oriented_hole_line_xz(h, origin_lat, origin_lon)
    if line:
        return line[-1]
    return _hole_anchor_xz(h, origin_lat, origin_lon)


def _path_kind(el: dict) -> str:
    tags = el.get("tags", {})
    if tags.get("golf") == "cartpath" or tags.get("highway") in ("service", "track"):
        return "cart_road"
    return "walking_shortcut"


def _path_surface(el: dict) -> str:
    tags = el.get("tags", {})
    if tags.get("surface"):
        return tags["surface"]
    if tags.get("highway") in ("service", "track"):
        return "gravel"
    return "dirt"


def _route_from_path(el: dict, origin_lat: float, origin_lon: float, index: int) -> dict | None:
    pts = _to_xz_list([el], origin_lat, origin_lon)
    if len(pts) < 2:
        return None
    kind = _path_kind(el)
    route = {
        "id": f"{kind}_{index:02d}",
        "source": f"osm:{el.get('type', '?')}/{el.get('id', '?')}",
        "surface": _path_surface(el),
        "width": 3.2 if kind == "cart_road" else 1.6,
        "points": [_xyz(pt) for pt in pts],
    }
    if kind == "walking_shortcut":
        route["required_skill_id"] = "fitness"
        route["required_level"] = 2
    return route


def _fallback_cart_roads(course_id: str, hole_starts: list[dict]) -> list[dict]:
    if not hole_starts:
        return []

    points = [hole_starts[0]["position"]]
    for start in hole_starts:
        if points[-1] != start["position"]:
            points.append(start["position"])
        points.append(start["return_position"])

    return [{
        "id": f"{course_id}_fallback_cart_loop",
        "source": "generated:fallback",
        "surface": "gravel",
        "width": 3.4,
        "points": points,
    }]


def course_world_to_json(course_id: str,
                         course_name: str,
                         course_el: dict,
                         holes: dict,
                         path_elements: list,
                         origin_lat: float,
                         origin_lon: float) -> dict:
    hole_starts = []
    for num in sorted(holes.keys()):
        tee = _hole_tee_xz(holes[num], origin_lat, origin_lon)
        ret = _hole_return_xz(holes[num], origin_lat, origin_lon)
        hole_starts.append({
            "id": f"hole_{num:02d}_start",
            "hole_index": len(hole_starts),
            "label": f"Hole {num}",
            "position": _xyz(tee),
            "return_position": _xyz(ret),
            "interaction_radius": 5.0,
        })

    if hole_starts:
        first = hole_starts[0]["position"]
        clubhouse_spawn = [_r(first[0] - 16.0), 0.0, _r(first[2] - 12.0)]
    else:
        clubhouse_spawn = [0.0, 0.0, 0.0]

    cart_roads = []
    walking_shortcuts = []
    for index, el in enumerate(path_elements, start=1):
        route = _route_from_path(el, origin_lat, origin_lon, index)
        if not route:
            continue
        if _path_kind(el) == "cart_road":
            cart_roads.append(route)
        else:
            walking_shortcuts.append(route)

    if not cart_roads:
        cart_roads = _fallback_cart_roads(course_id, [{"position": clubhouse_spawn, "return_position": clubhouse_spawn}] + hole_starts)

    if not walking_shortcuts and len(hole_starts) >= 2:
        walking_shortcuts.append({
            "id": f"{course_id}_fitness_cut_01",
            "source": "generated:fallback",
            "surface": "rough",
            "width": 1.4,
            "required_skill_id": "fitness",
            "required_level": 2,
            "points": [hole_starts[0]["return_position"], hole_starts[1]["position"]],
        })

    return {
        "id": course_id,
        "name": course_name,
        "origin": {
            "lat": origin_lat,
            "lon": origin_lon,
            "projection": "equirectangular_meters_xz",
            "osm_ref": _format_osm_ref(course_el),
        },
        "clubhouse_spawn": clubhouse_spawn,
        "hole_starts": hole_starts,
        "cart_roads": cart_roads,
        "walking_shortcuts": walking_shortcuts,
        "spawn_zones": [
            {
                "id": f"{course_id}_roadside_collectibles",
                "type": "collectible",
                "placement_hint": "near_roads",
                "center": clubhouse_spawn,
                "radius": 120.0,
            },
            {
                "id": f"{course_id}_clubhouse_interactables",
                "type": "npc_sign_shop",
                "placement_hint": "near_clubhouse",
                "center": clubhouse_spawn,
                "radius": 24.0,
            },
        ],
    }


def _hole_world_anchors(hole_num: int, h: dict, origin_lat: float, origin_lon: float) -> tuple[tuple[float, float], tuple[float, float]]:
    line_pts = _oriented_hole_line_xz(h, origin_lat, origin_lon)

    tee_xz = None
    if h["tees"]:
        pts = _to_xz_list(h["tees"][:1], origin_lat, origin_lon)
        if pts:
            tee_xz = pts[0]
    if tee_xz is None and line_pts:
        tee_xz = line_pts[0]
    if tee_xz is None and h["fairways"]:
        pts = _to_xz_list(h["fairways"], origin_lat, origin_lon)
        if pts:
            tee_xz = min(pts, key=lambda p: math.hypot(p[0], p[1]))
    if tee_xz is None:
        print(f"  [warn] hole {hole_num}: no world tee anchor, using course origin", file=sys.stderr)
        tee_xz = (0.0, 0.0)

    pin_xz = None
    if h["pins"]:
        pts = _to_xz_list(h["pins"][:1], origin_lat, origin_lon)
        if pts:
            pin_xz = pts[0]
    if pin_xz is None and h["greens"]:
        pts = _to_xz_list(h["greens"][:1], origin_lat, origin_lon)
        if pts:
            pin_xz = _centroid(pts)
    if pin_xz is None and line_pts:
        pin_xz = line_pts[-1]
    if pin_xz is None:
        pin_xz = (tee_xz[0], tee_xz[1] + 100.0)

    return tee_xz, pin_xz


def _xyz_from_xz(pt: tuple[float, float]) -> list[float]:
    return [_r(pt[0]), 0.0, _r(pt[1])]


def _path_polyline(el: dict, origin_lat: float, origin_lon: float) -> list[tuple[float, float]]:
    pts = _to_xz_list([el], origin_lat, origin_lon)
    if len(pts) < 2:
        return []
    deduped = [pts[0]]
    for pt in pts[1:]:
        if math.hypot(pt[0] - deduped[-1][0], pt[1] - deduped[-1][1]) >= 0.25:
            deduped.append(pt)
    return deduped if len(deduped) >= 2 and _polyline_length(deduped) >= 5.0 else []


def _is_cart_road(el: dict) -> bool:
    tags = el.get("tags", {})
    return tags.get("golf") == "cartpath" or tags.get("highway") in ("service", "track")


def _smooth_connection(points: list[tuple[float, float]]) -> list[tuple[float, float]]:
    if len(points) < 2:
        return points
    smoothed = [points[0]]
    for a, b in zip(points, points[1:]):
        smoothed.append(((a[0] * 0.35) + (b[0] * 0.65), (a[1] * 0.35) + (b[1] * 0.65)))
    return smoothed


def _fallback_cart_roads(spawn: tuple[float, float], hole_anchors: list[dict]) -> list[dict]:
    if not hole_anchors:
        return []

    chain = [spawn]
    for anchor in hole_anchors:
        chain.append(anchor["start_xz"])
        chain.append(anchor["return_xz"])

    return [{
        "id": "fallback_main_cart_loop",
        "surface": "gravel",
        "width": 4.0,
        "source": "generated_fallback",
        "polyline": [_xyz_from_xz(pt) for pt in _smooth_connection(chain)]
    }]


def fitness_skill_id_for_world() -> str:
    return "fitness"


def _world_paths_from_osm(path_elements: list, origin_lat: float, origin_lon: float) -> tuple[list[dict], list[dict]]:
    cart_roads = []
    shortcuts = []
    for el in path_elements:
        pts = _path_polyline(el, origin_lat, origin_lon)
        if not pts:
            continue
        tags = el.get("tags", {})
        osm_ref = f"{el.get('type', '?')}/{el.get('id', '?')}"
        if _is_cart_road(el):
            cart_roads.append({
                "id": f"cart_{el.get('type', 'way')}_{el.get('id')}",
                "surface": "asphalt" if tags.get("surface") in ("asphalt", "paved", "concrete") else "gravel",
                "width": 4.0,
                "source": "osm",
                "osm_ref": osm_ref,
                "polyline": [_xyz_from_xz(pt) for pt in pts]
            })
        else:
            shortcuts.append({
                "id": f"shortcut_{el.get('type', 'way')}_{el.get('id')}",
                "surface": tags.get("surface", "dirt"),
                "width": 2.0,
                "source": "osm",
                "osm_ref": osm_ref,
                "required_skill_id": fitness_skill_id_for_world(),
                "required_level": 2,
                "polyline": [_xyz_from_xz(pt) for pt in pts]
            })
    return cart_roads, shortcuts


def _collectible_candidates(course_id: str,
                            spawn: tuple[float, float],
                            hole_anchors: list[dict]) -> list[dict]:
    collectibles = [
        {
            "id": f"{course_id}_clubhouse_token",
            "kind": "range_token",
            "position": _xyz_from_xz((spawn[0] + 2.0, spawn[1] + 2.0)),
            "interaction_radius": 3.0,
            "repeatable": True,
            "repeatable_cooldown_holes": 1,
            "reward": {
                "money": 1,
                "skill_xp": {"fitness": 5}
            }
        }
    ]

    if hole_anchors:
        first = hole_anchors[0]["start_xz"]
        collectibles.append({
            "id": f"{course_id}_lost_ball_01",
            "kind": "lost_ball",
            "position": _xyz_from_xz(((spawn[0] + first[0]) * 0.5, (spawn[1] + first[1]) * 0.5)),
            "interaction_radius": 3.0,
            "repeatable": False,
            "reward": {
                "money": 3,
                "skill_xp": {"fitness": 12},
                "world_flag": f"{course_id}_found_lost_ball_01"
            }
        })

    if len(hole_anchors) >= 2:
        a = hole_anchors[0]["return_xz"]
        b = hole_anchors[1]["start_xz"]
        collectibles.append({
            "id": f"{course_id}_fitness_cache_01",
            "kind": "cache",
            "position": _xyz_from_xz(((a[0] + b[0]) * 0.5, (a[1] + b[1]) * 0.5)),
            "interaction_radius": 3.0,
            "repeatable": False,
            "requirement": {
                "skill_id": "fitness",
                "min_level": 2
            },
            "reward": {
                "money": 8,
                "skill_xp": {"fitness": 20},
                "world_flag": f"{course_id}_found_fitness_cache_01"
            }
        })

    return collectibles


def course_world_to_json(course_id: str,
                         course_name: str,
                         course_el: dict,
                         holes: dict,
                         path_elements: list,
                         origin_lat: float,
                         origin_lon: float) -> dict:
    hole_anchors = []
    for output_index, hole_num in enumerate(sorted(holes.keys())):
        tee_xz, pin_xz = _hole_world_anchors(hole_num, holes[hole_num], origin_lat, origin_lon)
        hole_anchors.append({
            "hole_num": hole_num,
            "hole_index": output_index,
            "start_xz": tee_xz,
            "return_xz": pin_xz,
        })

    if hole_anchors:
        first = hole_anchors[0]["start_xz"]
        spawn = (first[0] - 18.0, first[1] - 14.0)
    else:
        spawn = (0.0, 0.0)

    cart_roads, shortcuts = _world_paths_from_osm(path_elements, origin_lat, origin_lon)
    if not cart_roads:
        cart_roads = _fallback_cart_roads(spawn, hole_anchors)

    hole_starts = []
    for anchor in hole_anchors:
        hole_starts.append({
            "id": f"hole_{anchor['hole_num']:02d}_start",
            "hole_index": anchor["hole_index"],
            "position": _xyz_from_xz(anchor["start_xz"]),
            "return_position": _xyz_from_xz(anchor["return_xz"]),
            "interaction_radius": 4.0
        })

    return {
        "id": course_id,
        "name": course_name,
        "source": {
            "type": "osm",
            "osm_ref": _format_osm_ref(course_el)
        },
        "projection": {
            "type": "equirectangular",
            "origin_lat": origin_lat,
            "origin_lon": origin_lon,
            "units": "meters"
        },
        "spawn": {
            "id": "clubhouse_spawn",
            "position": _xyz_from_xz(spawn),
            "radius": 5.0
        },
        "hole_starts": hole_starts,
        "cart_roads": cart_roads,
        "walking_shortcuts": shortcuts,
        "collectibles": _collectible_candidates(course_id, spawn, hole_anchors),
        "spawn_zones": [
            {"id": "road_collectibles", "kind": "collectible", "near": "cart_roads", "count": 8},
            {"id": "tree_signs", "kind": "sign", "near": "trees", "count": 4},
            {"id": "clubhouse_npcs", "kind": "npc", "near": "spawn", "count": 2}
        ],
        "interactables": [
            {
                "id": "clubhouse_notice",
                "kind": "sign",
                "position": _xyz_from_xz((spawn[0] + 3.0, spawn[1] + 1.5)),
                "interaction_radius": 3.0,
                "content_id": "clubhouse_notice"
            }
        ]
    }


def slugify(name: str) -> str:
    return re.sub(r"[^a-z0-9]+", "_", name.lower()).strip("_")


def _project_root() -> Path:
    return Path(__file__).resolve().parent.parent


def main():
    ap = argparse.ArgumentParser(
        prog="osm_golf_convert.py",
        description="Convert an OSM golf course to hole JSON files (golf++ schema)",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python osm_golf_convert.py "Aarhus Golf Klub"
  python osm_golf_convert.py --name "Skandinavisk Golf Center"
  python osm_golf_convert.py --id R3456789
  python osm_golf_convert.py --lat 56.185 --lon 10.214
        """
    )
    ap.add_argument("name", nargs="?", help="Course name to search for")
    ap.add_argument("--name", dest="name", metavar="NAME", help="Course name (alternative)")
    ap.add_argument("--id",   metavar="ID",  help="OSM relation/way ID, e.g. R3456789")
    ap.add_argument("--lat",  type=float,    help="Latitude  for nearest-course search")
    ap.add_argument("--lon",  type=float,    help="Longitude for nearest-course search")
    default_holes = _project_root() / "assets" / "holes"
    default_courses = _project_root() / "assets" / "courses"
    default_worlds = _project_root() / "assets" / "course_worlds"
    ap.add_argument("-o", "--out", default=str(default_holes), metavar="DIR",
                    help=f"Output directory for hole JSON files (default: {default_holes})")
    ap.add_argument("--course-out", default=str(default_courses), metavar="DIR",
                    help=f"Output directory for course manifest JSON (default: {default_courses})")
    ap.add_argument("--world-out", default=str(default_worlds), metavar="DIR",
                    help=f"Output directory for course-world JSON (default: {default_worlds})")
    ap.add_argument("--overpass", metavar="URL",
                    help="Custom Overpass API URL (default: overpass-api.de)")
    ap.add_argument("--no-course", action="store_true",
                    help="Skip writing the course manifest JSON")
    ap.add_argument("--no-world", action="store_true",
                    help="Skip writing the course-world JSON")
    args = ap.parse_args()

    if not any([args.name, args.id, args.lat is not None, args.lon is not None]):
        ap.print_help()
        sys.exit(0)
    if (args.lat is None) != (args.lon is None):
        print("--lat and --lon must be provided together.", file=sys.stderr)
        sys.exit(1)

    if args.overpass:
        OVERPASS_INSTANCES.insert(0, args.overpass)

    # ── 1. Locate the course ─────────────────────────────────────────────────
    print("→ Locating course on OSM...", file=sys.stderr)
    course_el, course_name = _find_course(args)
    course_id = slugify(course_name)
    print(f"  Found: {course_name}  (id: {course_id})", file=sys.stderr)

    # ── 2. Fetch golf elements ────────────────────────────────────────────────
    print("→ Fetching golf elements...", file=sys.stderr)
    elements, tree_elements, path_elements = _fetch_elements(course_el)
    print(f"  Retrieved {len(elements)} golf elements, {len(tree_elements)} tree/wood elements, and {len(path_elements)} path elements", file=sys.stderr)

    if not elements:
        print("\nNo golf elements found inside the course boundary.\n"
              "The course may not have detailed hole mapping in OSM.\n"
              "Check coverage at: https://overpass-turbo.eu", file=sys.stderr)
        sys.exit(1)

    # ── 3. Determine coordinate origin ────────────────────────────────────────
    # Use centroid of all elements so the projection is centred over the course.
    all_latlon = []
    for el in elements:
        all_latlon.extend(_element_geom(el))
    if not all_latlon:
        print("Elements have no geometry.", file=sys.stderr)
        sys.exit(1)
    origin_lat = sum(p[0] for p in all_latlon) / len(all_latlon)
    origin_lon = sum(p[1] for p in all_latlon) / len(all_latlon)

    # ── 4. Group by hole ──────────────────────────────────────────────────────
    print("→ Grouping elements by hole...", file=sys.stderr)
    holes = group_holes(elements)

    if not holes:
        print("\nCould not identify individual holes.\n"
              "OSM data may be missing hole relations or ref tags.", file=sys.stderr)
        sys.exit(1)

    print(f"  Identified {len(holes)} hole(s)", file=sys.stderr)
    assign_trees_to_holes(holes, tree_elements, origin_lat, origin_lon, f"{course_el.get('type')}:{course_el.get('id')}")

    # ── 5. Write hole files ───────────────────────────────────────────────────
    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)
    course_dir = Path(args.course_out)
    if not args.no_course:
        course_dir.mkdir(parents=True, exist_ok=True)
    world_dir = Path(args.world_out)
    if not args.no_world:
        world_dir.mkdir(parents=True, exist_ok=True)
    hole_paths = []

    for num in sorted(holes.keys()):
        print(f"  Processing hole {num}...", file=sys.stderr)
        h_json = hole_to_json(num, holes[num], origin_lat, origin_lon, course_id)
        fname = f"{course_id}_h{num:02d}.json"
        fpath = out_dir / fname
        with open(fpath, "w", encoding="utf-8") as f:
            json.dump(h_json, f, indent=2)
        hole_paths.append(f"holes/{fname}")
        dist = math.hypot(h_json["pin"][0], h_json["pin"][2])
        path_len = _hole_json_path_length(h_json)
        width = h_json["spline"]["width"]
        print(f"    {fname}  par {h_json['par']}  direct {dist:.0f}m  path {path_len:.0f}m  width {width:.1f}m  zones {len(h_json['material_zones'])}  trees {len(h_json['trees'])}", file=sys.stderr)
        for warning in _scale_warnings(h_json):
            print(f"      [warn] {warning}", file=sys.stderr)

    # ── 6. Write course manifest ──────────────────────────────────────────────
    world_reference = f"course_worlds/{course_id}.json"
    if not args.no_world:
        world_json = course_world_to_json(course_id,
                                          course_name,
                                          course_el,
                                          holes,
                                          path_elements,
                                          origin_lat,
                                          origin_lon)
        world_file = world_dir / f"{course_id}.json"
        with open(world_file, "w", encoding="utf-8") as f:
            json.dump(world_json, f, indent=2)
        print(f"\n-> Course world: {world_file}", file=sys.stderr)

    if not args.no_course:
        course_json = {
            "id": course_id,
            "name": course_name,
            "hole_count": len(holes),
            "holes": hole_paths
        }
        if not args.no_world:
            course_json["world"] = world_reference
        course_file = course_dir / f"{course_id}.json"
        with open(course_file, "w", encoding="utf-8") as f:
            json.dump(course_json, f, indent=2)
        print(f"\n→ Course manifest: {course_file}", file=sys.stderr)

    print(f"→ Done! {len(holes)} hole(s) in {out_dir}/", file=sys.stderr)


if __name__ == "__main__":
    main()
