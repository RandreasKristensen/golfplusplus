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
  python osm_golf_convert.py --id R123456 -o ./my_course/holes

Requirements: pip install requests
"""

import argparse
import json
import math
import random
import re
import sys
from pathlib import Path

try:
    import requests
except ImportError:
    print("Missing dependency: pip install requests", file=sys.stderr)
    sys.exit(1)

OVERPASS_INSTANCES = [
    "https://overpass-api.de/api/interpreter",
    "https://overpass.kumi.systems/api/interpreter",
]

_HEADERS = {"User-Agent": "osm_golf_convert/1.0 (golf course converter; github.com/RandreasKristensen)"}

# ── Overpass queries ──────────────────────────────────────────────────────────

def _query(q: str) -> dict:
    """POST query to Overpass, trying fallback instances on failure."""
    last_err = None
    for url in OVERPASS_INSTANCES:
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
    raise RuntimeError(f"All Overpass instances failed. Last error: {last_err}")

def _find_course(args) -> tuple[dict, str]:
    """Returns (course_element, course_name). Exits on failure."""
    if args.id:
        num = args.id.lstrip("RrWw")
        kind = "way" if args.id.upper().startswith("W") else "relation"
        q = f"[out:json][timeout:30];\n{kind}({num});\nout geom bb;"
    elif args.lat and args.lon:
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

    el = els[0]
    name = el.get("tags", {}).get("name", "Unknown Course")
    return el, name


def _fetch_elements(course_el: dict) -> list:
    """Fetch all golf-tagged elements within the course bounding box."""
    b = course_el.get("bounds")
    if not b:
        # Fallback: derive from geometry
        lats, lons = [], []
        for pt in course_el.get("geometry", []):
            lats.append(pt["lat"]); lons.append(pt["lon"])
        if not lats:
            print("Course has no geometry in OSM data.", file=sys.stderr)
            sys.exit(1)
        b = {"minlat": min(lats), "minlon": min(lons),
             "maxlat": max(lats), "maxlon": max(lons)}

    # Expand bbox slightly so edge holes aren't clipped
    pad = 0.003  # ~300m
    s, w = b["minlat"] - pad, b["minlon"] - pad
    n, e = b["maxlat"] + pad, b["maxlon"] + pad
    bbox = f"{s},{w},{n},{e}"

    q = f"""[out:json][timeout:90];
(
  relation["golf"]({bbox});
  way["golf"]({bbox});
  node["golf"]({bbox});
);
out geom;"""
    data = _query(q)
    return data.get("elements", [])


# ── Geometry helpers ──────────────────────────────────────────────────────────

def _latlon_to_xz(lat, lon, origin_lat, origin_lon) -> tuple[float, float]:
    """Project WGS84 → local metres: X=East, Z=North, origin at tee."""
    cos_lat = math.cos(math.radians(origin_lat))
    x = (lon - origin_lon) * cos_lat * 111_320
    z = (lat - origin_lat) * 111_320
    return x, z


def _element_geom(el: dict) -> list[tuple[float, float]]:
    """Return list of (lat, lon) for any element type."""
    if el["type"] == "node":
        return [(el["lat"], el["lon"])]
    return [(pt["lat"], pt["lon"]) for pt in el.get("geometry", [])]


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


# ── Hole grouping ─────────────────────────────────────────────────────────────

def _empty_hole() -> dict:
    return {"tees": [], "pins": [], "fairways": [], "greens": [],
            "bunkers": [], "waters": [], "tags": {}}


def _classify_into(el: dict, h: dict):
    tag = el.get("tags", {}).get("golf", "")
    h["tags"].update(el.get("tags", {}))
    if tag == "tee":                                    h["tees"].append(el)
    elif tag in ("pin", "hole", "flagstick"):           h["pins"].append(el)
    elif tag == "fairway":                              h["fairways"].append(el)
    elif tag == "green":                                h["greens"].append(el)
    elif tag in ("bunker", "sand"):                     h["bunkers"].append(el)
    elif tag in ("water_hazard", "lateral_water_hazard", "water"): h["waters"].append(el)


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
        ref = tags.get("ref") or tags.get("hole") or tags.get("name", "")
        try:
            num = int(re.search(r"\d+", str(ref)).group())
        except (AttributeError, ValueError):
            num = hole_rels.index(rel) + 1

        h = holes.setdefault(num, _empty_hole())
        h["tags"].update(tags)

        for member in rel.get("members", []):
            key = (member["type"], member["ref"])
            el = by_id.get(key)
            if el:
                _classify_into(el, h)
                assigned.add(key)

    # ── Strategy 2: ref tags ──────────────────────────────────────────────────
    for el in elements:
        eid = (el["type"], el["id"])
        if eid in assigned:
            continue
        tags = el.get("tags", {})
        ref = tags.get("ref") or tags.get("hole")
        try:
            num = int(str(ref))
            h = holes.setdefault(num, _empty_hole())
            _classify_into(el, h)
            assigned.add(eid)
        except (TypeError, ValueError):
            pass

    # ── Strategy 3: spatial clustering around tees ────────────────────────────
    unassigned = [el for el in elements if (el["type"], el["id"]) not in assigned]
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
                d = math.hypot((el_lat-t_lat)*111320, (el_lon-t_lon)*111320)
                if d < best_d:
                    best_d, best_num = d, num
            if best_d < 500:
                h = holes.setdefault(best_num, _empty_hole())
                _classify_into(el, h)

    return holes


# ── Hole → JSON ───────────────────────────────────────────────────────────────

def _r(v): return round(v, 2)


def hole_to_json(hole_num: int, h: dict, origin_lat: float, origin_lon: float,
                 course_id: str) -> dict:

    # ── tee position ──────────────────────────────────────────────────────────
    tee_latlon = None
    if h["tees"]:
        geom = _element_geom(h["tees"][0])
        if geom:
            tee_latlon = geom[0]

    if not tee_latlon:
        # Fall back to fairway start or origin
        fw_pts = _to_xz_list(h["fairways"], origin_lat, origin_lon)
        if fw_pts:
            # Tee is likely the polygon point closest to the origin
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
        "material_zones": zones
    }


# ── CLI ───────────────────────────────────────────────────────────────────────

def slugify(name: str) -> str:
    return re.sub(r"[^a-z0-9]+", "_", name.lower()).strip("_")


def main():
    ap = argparse.ArgumentParser(
        prog="osm_golf_convert.py",
        description="Convert an OSM golf course to hole JSON files (golf++ schema)",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python osm_golf_convert.py "Aarhus Golf Klub"
  python osm_golf_convert.py --name "Skandinavisk Golf Center" -o ./holes
  python osm_golf_convert.py --id R3456789
  python osm_golf_convert.py --lat 56.185 --lon 10.214
        """
    )
    ap.add_argument("name", nargs="?", help="Course name to search for")
    ap.add_argument("--name", dest="name", metavar="NAME", help="Course name (alternative)")
    ap.add_argument("--id",   metavar="ID",  help="OSM relation/way ID, e.g. R3456789")
    ap.add_argument("--lat",  type=float,    help="Latitude  for nearest-course search")
    ap.add_argument("--lon",  type=float,    help="Longitude for nearest-course search")
    ap.add_argument("-o", "--out", default="holes", metavar="DIR",
                    help="Output directory for hole JSON files (default: ./holes)")
    ap.add_argument("--overpass", metavar="URL",
                    help="Custom Overpass API URL (default: overpass-api.de)")
    ap.add_argument("--no-course", action="store_true",
                    help="Skip writing the course manifest JSON")
    args = ap.parse_args()

    if not any([args.name, args.id, args.lat]):
        ap.print_help()
        sys.exit(0)

    if args.overpass:
        OVERPASS_INSTANCES.insert(0, args.overpass)

    # ── 1. Locate the course ─────────────────────────────────────────────────
    print("→ Locating course on OSM...", file=sys.stderr)
    course_el, course_name = _find_course(args)
    course_id = slugify(course_name)
    print(f"  Found: {course_name}  (id: {course_id})", file=sys.stderr)

    # ── 2. Fetch golf elements ────────────────────────────────────────────────
    print("→ Fetching golf elements...", file=sys.stderr)
    elements = _fetch_elements(course_el)
    print(f"  Retrieved {len(elements)} elements", file=sys.stderr)

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

    # ── 5. Write hole files ───────────────────────────────────────────────────
    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)
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
        print(f"    {fname}  par {h_json['par']}  ~{dist:.0f}m", file=sys.stderr)

    # ── 6. Write course manifest ──────────────────────────────────────────────
    if not args.no_course:
        course_json = {
            "id": course_id,
            "name": course_name,
            "hole_count": len(holes),
            "holes": hole_paths
        }
        course_file = out_dir.parent / f"{course_id}.json"
        with open(course_file, "w", encoding="utf-8") as f:
            json.dump(course_json, f, indent=2)
        print(f"\n→ Course manifest: {course_file}", file=sys.stderr)

    print(f"→ Done! {len(holes)} hole(s) in {out_dir}/", file=sys.stderr)


if __name__ == "__main__":
    main()
