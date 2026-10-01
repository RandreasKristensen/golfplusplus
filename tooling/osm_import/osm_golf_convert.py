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
  python osm_golf_convert.py --id R123456 -o ../../assets/holes --course-out ../../assets/courses

Requirements: pip install requests (optional; falls back to Python stdlib)
"""

import argparse
import hashlib
import json
import math
import random
import re
import sys
import time
import unicodedata
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

try:
    import requests
except ImportError:
    requests = None

import osm_elevation
import osm_ground

# overpass-api.de is the reference instance and is tried first. The mirrors are
# full-planet instances from the OSM wiki's list, used when the primary is
# overloaded — which it regularly is for a query this size. Mirrors get a short
# timeout so a dead one costs seconds rather than minutes, and any mirror that
# times out is dropped for the rest of the run. Override with --overpass.
OVERPASS_INSTANCES = [
    "https://overpass-api.de/api/interpreter",
    "https://overpass.kumi.systems/api/interpreter",
    "https://maps.mail.ru/osm/tools/overpass/api/interpreter",
]
OVERPASS_PRIMARY_TIMEOUT = 180
# Mirrors do real work whenever the primary is overloaded, so they need room to
# answer a course-sized query. A mirror that is genuinely down costs this once,
# because the first failure takes it out of the rotation for the run.
OVERPASS_MIRROR_TIMEOUT = 60

# Politeness limits. Overpass and Nominatim are volunteer-funded services; both
# publish usage policies asking for an identifying User-Agent, about one request
# per second, and caching instead of repeat fetches. See tooling/README.md.
OVERPASS_MIN_INTERVAL = 2.0
NOMINATIM_MIN_INTERVAL = 1.2
MAX_OVERPASS_REQUESTS = 40
_LAST_CALL: dict[str, float] = {}
_OVERPASS_REQUESTS = 0
# Mirrors that timed out once are skipped for the rest of the run. A dead
# mirror otherwise costs its full timeout on every single retry round.
_DEAD_INSTANCES: set[str] = set()
NOMINATIM_URL = "https://nominatim.openstreetmap.org/search"

_HEADERS = {"User-Agent": "osm_golf_convert/1.0 (golf course converter; github.com/RandreasKristensen)"}
EARTH_METERS_PER_DEGREE_LAT = 111_320.0
DEFAULT_CONFIG = {
    "tree": {
        "trunk_radius": 0.65,
        "trunk_height": 5.0,
        "leaf_radius": 4.7,
        "leaf_height": 6.0,
        "max_per_hole": 60,
    },
    "hole": {
        "fallback_width": 20.0,
        "fallback_rough_width": 32.0,
        "rough_width_multiplier": 1.55,
    },
    "world": {
        "hole_start_interaction_radius": 4.0,
        "fallback_cart_width": 4.0,
        "shortcut_width": 2.0,
        "max_shortcut_length": 180.0,
        "max_cart_road_length": 280.0,
        "max_path_distance_from_holes": 75.0,
        "fairway_avoidance_clearance": 8.0,
        "fallback_road_extra_offset": 8.0,
        "max_shortcut_count": 12,
    },
    "elevation": {
        "enabled": True,
        # Terrarium tile zoom (see osm_elevation.py); 14 is ~5–8 m per pixel.
        "zoom": 14,
        "max_grade": 0.25,
        # Steepest side slope a hole may tilt with, rise per metre across it.
        "max_bank": 0.2,
        "smooth_window": 3,
    },
    # The course world's ground grid (see osm_ground.py).
    "ground": {
        "cell_size": 20.0,
        "margin": 120.0,
    },
    "courses": {},
}


def _deep_merge(base: dict, override: dict) -> dict:
    result = {key: _deep_merge(value, {}) if isinstance(value, dict) else value for key, value in base.items()}
    for key, value in override.items():
        if isinstance(value, dict) and isinstance(result.get(key), dict):
            result[key] = _deep_merge(result[key], value)
        elif isinstance(value, dict):
            result[key] = _deep_merge(value, {})
        else:
            result[key] = value
    return result


def load_generation_config(path: str | None, course_id: str | None = None) -> dict:
    config = _deep_merge({}, DEFAULT_CONFIG)
    if path:
        try:
            with open(path, "r", encoding="utf-8") as f:
                loaded = json.load(f)
            if isinstance(loaded, dict):
                config = _deep_merge(config, loaded)
        except FileNotFoundError:
            print(f"  [warn] config not found: {path}; using defaults", file=sys.stderr)
        except json.JSONDecodeError as e:
            print(f"  [warn] config parse failed: {path}: {e}; using defaults", file=sys.stderr)

    if course_id:
        course_overrides = config.get("courses", {}).get(course_id, {})
        if isinstance(course_overrides, dict):
            config = _deep_merge(config, course_overrides)
    return config

# ── Response cache ────────────────────────────────────────────────────────────

# Overpass is rate-limited and slow, and a single course import issues several
# large queries. Caching raw responses makes re-running the converter after a
# config tweak instant, and lets the verification harness replay a course
# offline. Set by main(); None disables caching.
_CACHE_DIR: Path | None = None
_CACHE_REFRESH = False


def set_cache(directory: str | Path | None, refresh: bool = False) -> None:
    global _CACHE_DIR, _CACHE_REFRESH
    _CACHE_DIR = Path(directory) if directory else None
    _CACHE_REFRESH = refresh
    if _CACHE_DIR:
        _CACHE_DIR.mkdir(parents=True, exist_ok=True)


def _cache_file(namespace: str, key: str) -> Path | None:
    if _CACHE_DIR is None:
        return None
    digest = hashlib.sha256(key.encode("utf-8")).hexdigest()[:24]
    return _CACHE_DIR / f"{namespace}_{digest}.json"


def _cache_read(namespace: str, key: str):
    path = _cache_file(namespace, key)
    if path is None or _CACHE_REFRESH or not path.exists():
        return None
    try:
        with open(path, "r", encoding="utf-8") as f:
            return json.load(f)
    except (json.JSONDecodeError, OSError):
        return None


def _cache_write(namespace: str, key: str, value) -> None:
    path = _cache_file(namespace, key)
    if path is None:
        return
    try:
        with open(path, "w", encoding="utf-8") as f:
            json.dump(value, f)
    except OSError as e:
        print(f"  [warn] could not write cache {path}: {e}", file=sys.stderr)


# ── Overpass queries ──────────────────────────────────────────────────────────

class OverpassUnavailable(RuntimeError):
    """Every Overpass instance refused or failed to answer."""


def _throttle(bucket: str, min_interval: float) -> None:
    """
    Keep at least `min_interval` seconds between calls to a given service.

    Overpass and Nominatim are donated infrastructure running on a handful of
    machines. Their usage policies ask for a identifying User-Agent, roughly
    one request per second, and results cached rather than re-fetched. This
    plus the on-disk cache is what keeps a course import down to a handful of
    requests total.
    """
    now = time.monotonic()
    elapsed = now - _LAST_CALL.get(bucket, 0.0)
    if elapsed < min_interval:
        time.sleep(min_interval - elapsed)
    _LAST_CALL[bucket] = time.monotonic()


def _wait_for_overpass_slot(max_wait: float = 120.0) -> None:
    """
    Ask Overpass whether it has a free slot for us, and wait if it does not.

    This is the check the Overpass operators explicitly ask heavy clients to
    make. It costs one cheap request and means we queue politely instead of
    firing expensive queries at a server that is already saturated.
    """
    status_url = OVERPASS_INSTANCES[0].replace("/interpreter", "/status")
    deadline = time.monotonic() + max_wait
    while time.monotonic() < deadline:
        try:
            _throttle("overpass-status", 1.0)
            if requests is not None:
                text = requests.get(status_url, timeout=20, headers=_HEADERS).text
            else:
                req = urllib.request.Request(status_url, headers=_HEADERS, method="GET")
                with urllib.request.urlopen(req, timeout=20) as r:
                    text = r.read().decode("utf-8", "replace")
        except Exception:
            return  # Status unavailable is not a reason to refuse to work.

        match = re.search(r"(\d+)\s+slots available now", text)
        if match and int(match.group(1)) > 0:
            return
        if "slots available now" not in text:
            return

        wait_match = re.findall(r"Slot available after: .*?, in (\d+) seconds", text)
        wait = min(float(wait_match[0]), 30.0) + 1.0 if wait_match else 5.0
        print(f"  [info] waiting {wait:.0f}s for a free Overpass slot...", file=sys.stderr)
        time.sleep(wait)


OVERPASS_DISPATCHER_ERROR = re.compile(r"Dispatcher_Client|osm3s_osm_base|runtime error")


def _http_json(url: str, *, data: dict | None = None, timeout: int = 90) -> dict:
    """GET or POST (when `data` is given) and decode JSON, with or without requests."""
    if requests is not None:
        if data is None:
            r = requests.get(url, timeout=timeout, headers=_HEADERS)
        else:
            r = requests.post(url, data=data, timeout=timeout, headers=_HEADERS)
        r.raise_for_status()
        return r.json()

    if data is None:
        req = urllib.request.Request(url, headers=_HEADERS, method="GET")
    else:
        req = urllib.request.Request(
            url,
            data=urllib.parse.urlencode(data).encode("utf-8"),
            headers={**_HEADERS, "Content-Type": "application/x-www-form-urlencoded"},
            method="POST",
        )
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return json.loads(r.read().decode("utf-8"))


def _query(q: str) -> dict:
    """POST query to Overpass, trying fallback instances and backing off on 429."""
    cached = _cache_read("overpass", q)
    if cached is not None:
        return cached

    global _OVERPASS_REQUESTS

    last_err = None
    attempts = len(OVERPASS_INSTANCES) * 5
    for attempt in range(attempts):
        if _OVERPASS_REQUESTS >= MAX_OVERPASS_REQUESTS:
            raise RuntimeError(
                f"Stopping after {_OVERPASS_REQUESTS} Overpass requests. A single course "
                f"import should need well under {MAX_OVERPASS_REQUESTS}; something is "
                f"retrying in a loop. Re-run later rather than raising this limit.")

        index = attempt % len(OVERPASS_INSTANCES)
        url = OVERPASS_INSTANCES[index]
        if url in _DEAD_INSTANCES:
            continue
        timeout = OVERPASS_PRIMARY_TIMEOUT if index == 0 else OVERPASS_MIRROR_TIMEOUT
        if index == 0:
            _wait_for_overpass_slot()
        _throttle("overpass", OVERPASS_MIN_INTERVAL)
        try:
            _OVERPASS_REQUESTS += 1
            data = _http_json(url, data={"data": q}, timeout=timeout)
            _cache_write("overpass", q, data)
            return data
        except Exception as e:  # HTTP, transport and decode errors are all retryable
            last_err = e
            status = getattr(getattr(e, "response", None), "status_code", None) or getattr(e, "code", None)
            body = getattr(getattr(e, "response", None), "text", "") or ""
            restarting = bool(OVERPASS_DISPATCHER_ERROR.search(body))

            if restarting:
                label = "is restarting its database backend"
            elif status:
                label = f"returned {status}"
            else:
                label = f"unreachable: {type(e).__name__}"

            # An instance whose database backend is mid-restart, or that we
            # cannot reach at all, will not recover inside one run. Write it
            # off now so the remaining attempts go somewhere that might answer,
            # instead of spending the retry budget on a machine that is down.
            if restarting or status is None:
                _DEAD_INSTANCES.add(url)
                print(f"  [warn] {url} {label}; skipping it for this run", file=sys.stderr)
                if all(u in _DEAD_INSTANCES for u in OVERPASS_INSTANCES):
                    break
                continue

            if attempt == attempts - 1:
                break
            # 429 and 504 are the server saying it is overloaded. Back off hard
            # and exponentially rather than treating it as a transient blip.
            round_number = attempt // len(OVERPASS_INSTANCES) + 1
            wait = 15.0 * (2 ** (round_number - 1)) if status in (429, 504) else 3.0
            print(f"  [warn] {url} {label}; retrying in {wait:.0f}s...", file=sys.stderr)
            time.sleep(wait)
    tried = ", ".join(OVERPASS_INSTANCES)
    raise OverpassUnavailable(
        f"Every Overpass instance failed. Last error: {last_err}\n"
        f"Tried: {tried}\n"
        f"This usually means the servers are overloaded rather than anything being "
        f"wrong with the course or the query.\n"
        f"Check https://overpass-api.de/api/status and try again later, or pass "
        f"--overpass URL to use a different instance.\n"
        f"Whatever already downloaded is cached, so a re-run resumes from there.")


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


def _fetch_elements_by_ref(refs: list[tuple[str, int]]) -> list[dict]:
    """Fetch specific OSM elements with full geometry and bounding boxes."""
    if not refs:
        return []
    body = "\n".join(f"  {kind}({oid});" for kind, oid in refs)
    return _query(f"[out:json][timeout:60];\n(\n{body}\n);\nout geom bb;").get("elements", [])


def _nominatim_search(name: str, limit: int = 20) -> list[tuple[str, int]]:
    """
    Geocode a course name to OSM refs.

    Overpass has no name index, so `way["name"~"..."]` is a planet-wide scan:
    it takes minutes when it does not simply time out or get rate-limited. This
    is why searching by name used to fail while --lat/--lon worked. Nominatim
    *is* a name index, answers in well under a second, and hands back the very
    OSM ids Overpass wants.
    """
    key = f"{name}|{limit}"
    cached = _cache_read("nominatim", key)
    if cached is None:
        url = NOMINATIM_URL + "?" + urllib.parse.urlencode({
            "q": name,
            "format": "jsonv2",
            "limit": limit,
            "extratags": 1,
        })
        try:
            _throttle("nominatim", NOMINATIM_MIN_INTERVAL)
            cached = _http_json(url, timeout=45)
        except Exception as e:
            print(f"  [warn] name lookup unavailable ({type(e).__name__}); "
                  f"falling back to Overpass search", file=sys.stderr)
            return []
        _cache_write("nominatim", key, cached)

    refs = []
    for entry in cached:
        kind = entry.get("osm_type")
        oid = entry.get("osm_id")
        if kind not in ("way", "relation") or oid is None:
            continue
        # Nominatim returns the clubhouse node, the restaurant, the car park…
        # Keep only things that could be the course polygon itself.
        category, feature = entry.get("category"), entry.get("type")
        if (category, feature) not in (("leisure", "golf_course"), ("landuse", "recreation_ground")):
            extra = entry.get("extratags") or {}
            if extra.get("leisure") != "golf_course" and extra.get("golf") != "course":
                continue
        refs.append((kind, int(oid)))
    return refs


def _find_course(args) -> tuple[dict, str]:
    """Returns (course_element, course_name). Exits on failure."""
    els: list[dict] = []

    if args.id:
        num = args.id.lstrip("RrWw")
        kind = "way" if args.id.upper().startswith("W") else "relation"
        els = _fetch_elements_by_ref([(kind, int(num))])
    elif args.lat is not None and args.lon is not None:
        els = _query(f"""[out:json][timeout:60];
(
  relation["leisure"="golf_course"](around:8000,{args.lat},{args.lon});
  way["leisure"="golf_course"](around:8000,{args.lat},{args.lon});
);
out geom bb;""").get("elements", [])
    else:
        print("  Resolving name via Nominatim...", file=sys.stderr)
        els = _fetch_elements_by_ref(_nominatim_search(args.name))
        if not els:
            # Last resort: a bounded Overpass regex. Slow, but better than
            # telling the user the course does not exist.
            print("  Nominatim found nothing; trying a direct Overpass name search "
                  "(this can take a minute)...", file=sys.stderr)
            escaped = args.name.replace('"', '\\"')
            els = _query(f"""[out:json][timeout:180];
(
  relation["leisure"="golf_course"]["name"~"{escaped}",i];
  way["leisure"="golf_course"]["name"~"{escaped}",i];
);
out geom bb;""").get("elements", [])

    els = [el for el in els if el.get("type") in ("way", "relation")]
    if not els:
        print("No golf course found. Check spelling or try --lat/--lon.", file=sys.stderr)
        sys.exit(1)

    ranked = _rank_course_candidates(els, args)

    if getattr(args, "list_courses", False):
        print(f"\n{len(ranked)} candidate(s):", file=sys.stderr)
        for el in ranked:
            b = _bounds_from_element(el)
            centre = f"{_bounds_centroid(b)[0]:.5f},{_bounds_centroid(b)[1]:.5f}" if b else "?"
            print(f"  --id {'W' if el['type'] == 'way' else 'R'}{el['id']:<12} "
                  f"{el.get('tags', {}).get('name', 'Unknown'):<45} "
                  f"{_element_area_m2(el) / 10000.0:6.1f} ha  @ {centre}", file=sys.stderr)
        sys.exit(0)

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


GOLF_SELECTORS = [
    'relation["golf"]',
    'way["golf"]',
    'node["golf"]',
]
VEGETATION_SELECTORS = [
    'node["natural"="tree"]',
    'way["natural"="tree_row"]',
    'way["natural"="wood"]',
    'relation["natural"="wood"]',
    'way["landuse"="forest"]',
    'relation["landuse"="forest"]',
    'way["natural"="scrub"]',
    'relation["natural"="scrub"]',
]
PATH_SELECTORS = [
    'way["highway"~"^(path|service|track|footway|pedestrian)$"]',
    'relation["highway"~"^(path|service|track|footway|pedestrian)$"]',
    'way["golf"="cartpath"]',
    'relation["golf"="cartpath"]',
]


def _course_area_prelude(course_el: dict) -> str | None:
    """
    An Overpass prelude that binds `.courseArea` to the course polygon.

    Scoping to the course *area* instead of its bounding box matters twice
    over. It is far cheaper for the server — a bbox around the Old Course also
    contains the town of St Andrews and six other courses — and it is more
    correct, because neighbouring courses never enter the result set in the
    first place.

    map_to_area works for a relation and for a closed way, which is how most
    single courses are mapped.
    """
    kind = course_el.get("type")
    if kind not in ("way", "relation"):
        return None
    return f"{kind}({course_el['id']})->.course;\n.course map_to_area->.courseArea;"


def _scoped_query(course_el: dict, selectors: list[str], timeout: int = 180) -> list[dict]:
    """
    Run one selector group against the course, area-scoped when possible.

    Each group is its own request so that a heavy one (paths through a town)
    cannot take the cheap ones down with it, and so a retry only repeats the
    part that failed.
    """
    prelude = _course_area_prelude(course_el)
    if prelude:
        body = "\n".join(f"  {sel}(area.courseArea);" for sel in selectors)
        query = f"[out:json][timeout:{timeout}];\n{prelude}\n(\n{body}\n);\nout geom;"
        try:
            # An empty result is an answer, not a failure: plenty of courses
            # genuinely have no scrub inside the boundary. Falling back to the
            # bbox here would pull in the surrounding town for nothing.
            return _query(query).get("elements", [])
        except OverpassUnavailable:
            raise
        except RuntimeError as e:
            print(f"  [warn] area-scoped query failed ({e}); falling back to the "
                  f"course bounding box", file=sys.stderr)

    bbox = _bbox_string_for_course(course_el)
    body = "\n".join(f"  {sel}({bbox});" for sel in selectors)
    return _query(f"[out:json][timeout:{timeout}];\n(\n{body}\n);\nout geom;").get("elements", [])


def _fetch_elements(course_el: dict) -> tuple[list, list, list]:
    """Fetch golf, vegetation, and path/service elements scoped to the selected course."""
    relation_elements = []
    used_relation_scope = False
    if course_el.get("type") == "relation":
        rel_id = course_el["id"]
        data = _query(f"""[out:json][timeout:90];
relation({rel_id})->.course;
(
  .course;
  >;
);
out geom;""")
        relation_elements = data.get("elements", [])
        used_relation_scope = bool(relation_elements)

    fetched = list(relation_elements)
    fetched += _scoped_query(course_el, GOLF_SELECTORS)
    fetched += _scoped_query(course_el, VEGETATION_SELECTORS)
    fetched += _scoped_query(course_el, PATH_SELECTORS)

    scoped = [el for el in _dedupe_elements(fetched)
              if _element_in_course_footprint(el, course_el)]
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
    """
    Project WGS84 -> local metres: X = east, Z = south. The game's left is +X
    when facing +Z (see yaw_left in src/physics/vector_math.h), so with Z
    south, east is on the right when facing north, as on the real course; a
    Z-north frame mirrors every course. North is also up in the hole editor,
    which draws +Z downwards.
    """
    cos_lat = math.cos(math.radians(origin_lat))
    x = (lon - origin_lon) * cos_lat * EARTH_METERS_PER_DEGREE_LAT
    z = (origin_lat - lat) * EARTH_METERS_PER_DEGREE_LAT
    return x, z


def _xz_to_latlon(x, z, origin_lat, origin_lon) -> tuple[float, float]:
    """Inverse of _latlon_to_xz — needed to ask a DEM about a local point."""
    cos_lat = math.cos(math.radians(origin_lat)) or 1.0
    return (origin_lat - z / EARTH_METERS_PER_DEGREE_LAT,
            origin_lon + x / (cos_lat * EARTH_METERS_PER_DEGREE_LAT))


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


def _polygon_area_xz(pts) -> float:
    if len(pts) < 3:
        return 0.0
    area = 0.0
    for a, b in zip(pts, pts[1:] + pts[:1]):
        area += a[0] * b[1] - b[0] * a[1]
    return abs(area) * 0.5


def _zone_circle(el: dict, pts, anchor=None,
                 min_radius: float = 2.0, max_radius: float = 22.0):
    """
    Fit the circle the game will use for a green or bunker → (cx, cz, radius).

    Ritter's bounding circle returns half the longest diagonal, which badly
    over-states an elongated green and doubles a shared one: the Old Course
    imported with 50 m "greens". The equal-area radius matches how much ground
    the zone actually covers, which is what putting and sand behaviour care
    about. Ritter is kept as an upper bound so the circle never claims to be
    bigger than the polygon it came from.

    `anchor` (the pin) recentres the circle when the polygon's centroid is too
    far away to be this hole's part of it.
    """
    ritter_x, ritter_z, ritter_r = _ritter_circle(pts)
    area = _polygon_area_xz(pts)
    radius = math.sqrt(area / math.pi) if area > 0 else ritter_r
    radius = max(min_radius, min(radius, ritter_r, max_radius))

    cx, cz = _centroid(pts)
    if anchor is not None and _point_in_polygon_xz(anchor, pts):
        # Only recentre when the pin genuinely lies on this polygon. Snapping a
        # neighbouring hole's green onto our pin would stack two greens on the
        # same spot.
        if math.hypot(anchor[0] - cx, anchor[1] - cz) > radius * 0.5:
            cx, cz = anchor
    return cx, cz, radius


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


def _control_point_count(length_m: float) -> int:
    """
    How many spline control points a hole of this length deserves.

    Five was fixed regardless of length, which under-described a 550 m par 5
    and over-described a 150 m par 3. Roughly one point per 45 m keeps a
    dogleg's shape without chasing OSM vertex noise.
    """
    return max(4, min(14, int(round(length_m / 45.0)) + 3))


def _snap_endpoints(line, tee, pin, snap_radius: float = 55.0):
    """
    Force a centreline to start at the tee and end at the pin.

    An OSM golf=hole way is drawn from somewhere on the teeing ground to
    somewhere on the green, which is close to but not exactly the tee marker
    and pin we position the hole with. Left alone, the spline starts a few
    metres off the tee, which shows up in-game as the ball spawning beside the
    fairway ribbon rather than on it.
    """
    out = list(line)
    if not out:
        return [tee, pin]

    if math.hypot(out[0][0] - tee[0], out[0][1] - tee[1]) <= snap_radius:
        out[0] = tee
    else:
        out.insert(0, tee)

    if math.hypot(out[-1][0] - pin[0], out[-1][1] - pin[1]) <= snap_radius:
        out[-1] = pin
    else:
        out.append(pin)
    return out


def _line_spans_hole(line, tee, pin) -> bool:
    """
    Is this OSM way plausibly the centreline of *this* hole?

    Guards against a hole way that was mis-tagged, only partially drawn, or
    grouped onto the wrong hole. A real centreline runs from near the tee to
    near the green and is not wildly longer than the straight distance.
    """
    if len(line) < 2:
        return False
    direct = math.hypot(pin[0] - tee[0], pin[1] - tee[1])
    if direct < 1.0:
        return False

    length = _polyline_length(line)
    if not (direct * 0.85 <= length <= direct * 2.0):
        return False

    # Endpoints must actually reach the tee and the green.
    reach = max(45.0, direct * 0.2)
    return (math.hypot(line[0][0] - tee[0], line[0][1] - tee[1]) <= reach and
            math.hypot(line[-1][0] - pin[0], line[-1][1] - pin[1]) <= reach)


def _hole_centerline(h: dict, line_pts, fw_pts, tee_xz, pin_xz,
                     hole_config: dict) -> tuple[list, float, float, str]:
    """
    Build the spline control points for one hole.

    Source priority:
      1. The OSM `golf=hole` way. This is the surveyed line of play, which is
         what a scorecard measures and what a dogleg actually follows.
      2. The fairway polygon's centreline, when no hole way exists.
      3. A straight tee→pin line.

    Width always comes from the fairway polygon when there is one, regardless
    of which source shaped the centreline. Previously the two were coupled, so
    any hole that fell back to the hole way also threw away a perfectly good
    measured width in favour of the 20 m default.
    """
    direct = math.hypot(pin_xz[0] - tee_xz[0], pin_xz[1] - tee_xz[1])

    fallback_width = float(hole_config.get("fallback_width", 20.0))
    max_width = float(hole_config.get("max_width", 70.0))

    width = fallback_width
    if len(fw_pts) >= 4:
        measured = _fairway_width(fw_pts, tee_xz, pin_xz)
        # Links courses share one huge fairway polygon between two holes, and
        # measuring across it yields a 250 m "fairway". Anything wider than a
        # real fairway means the polygon is not this hole's alone.
        width = measured if 8.0 <= measured <= max_width else fallback_width
    rough_width = round(width * float(hole_config.get("rough_width_multiplier", 1.55)), 1)

    # A fairway polygon is only trustworthy for shape if it is also a plausible
    # width. Links courses share one polygon between two holes, and its
    # "centre" then runs down the gap between them.
    fairway_usable = len(fw_pts) >= 4 and width != fallback_width

    if _line_spans_hole(line_pts, tee_xz, pin_xz):
        if len(line_pts) <= 2 and fairway_usable:
            # The hole way is a bare tee→green segment: correct length, no
            # shape. The fairway polygon knows where the hole actually bends,
            # so take its centreline and pin it to the hole way's endpoints.
            ctrl = _snap_endpoints(
                _fairway_centerline(fw_pts, tee_xz, pin_xz, n=_control_point_count(direct)),
                tee_xz, pin_xz, snap_radius=1e9)
            # Only if it produces a believable dogleg. A short par 3 has no
            # bend to find, and slicing its surrounding fairway invents one —
            # it added 33 m to Augusta's 12th, which is a straight 142 m shot.
            if _polyline_length(ctrl) <= max(direct * 1.15, direct + 15.0):
                return ctrl, width, rough_width, "fairway_shape"
        snapped = _snap_endpoints(line_pts, tee_xz, pin_xz)
        n = _control_point_count(_polyline_length(snapped))
        return _resample_polyline(snapped, n), width, rough_width, "hole_way"

    if len(fw_pts) >= 4:
        n = _control_point_count(direct)
        ctrl = _fairway_centerline(fw_pts, tee_xz, pin_xz, n=n)
        return _snap_endpoints(ctrl, tee_xz, pin_xz, snap_radius=1e9), width, rough_width, "fairway"

    if len(line_pts) >= 2:
        # A hole way that failed the span check is still better than nothing;
        # snapping its ends keeps the tee and pin authoritative.
        snapped = _snap_endpoints(line_pts, tee_xz, pin_xz)
        n = _control_point_count(_polyline_length(snapped))
        return _resample_polyline(snapped, n), width, rough_width, "hole_way_partial"

    n = _control_point_count(direct)
    straight = [(tee_xz[0] + (pin_xz[0] - tee_xz[0]) * i / (n - 1),
                 tee_xz[1] + (pin_xz[1] - tee_xz[1]) * i / (n - 1)) for i in range(n)]
    return straight, width, rough_width, "straight"


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
    """
    Return N evenly-spaced points along a line, ends included; one point is
    the line's middle. Duplicates if the line is degenerate.
    """
    if not pts or n < 1:
        return []
    if len(pts) == 1:
        return [pts[0]] * n

    total = _polyline_length(pts)
    if total <= 0.001:
        return [pts[0]] * n

    result = []
    targets = [total * i / (n - 1) for i in range(n)] if n > 1 else [total * 0.5]
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


PRACTICE_PATTERN = re.compile(r"practice|putting|chipping|driving|range|øve", re.IGNORECASE)


def _parse_hole_numbers(tags: dict) -> list[int]:
    """
    Every hole number an element belongs to.

    Usually one, but links courses share a green or a fairway between two
    holes and label it "3/15" or "2;16". Returning a list lets a shared green
    be attached to both holes instead of whichever one happened to win a
    nearest-neighbour test — which is why half the Old Course used to import
    with no putting surface at all.

    `name` is consulted as well as `ref`/`hole` because shared features are
    commonly labelled that way, but only when the name is purely numeric, so a
    bunker called "Hell Bunker" is never mistaken for a hole number.
    """
    numbers: list[int] = []
    for key in ("ref", "hole", "name"):
        raw = tags.get(key)
        if raw is None:
            continue
        text = str(raw).strip()
        if key == "name" and PRACTICE_PATTERN.search(text):
            return []
        parts = re.split(r"[\s/;,&+-]+", text)
        parsed = []
        for part in parts:
            part = part.lstrip("#").strip()  # "#17" is a label, not a name
            if not part:
                continue
            m = re.fullmatch(r"\d{1,2}", part) or re.fullmatch(
                r"(?:hole|hul)\s*(\d{1,2})", part, re.IGNORECASE)
            if not m:
                parsed = []
                break
            num = int(m.group(1) if m.lastindex else m.group())
            if not 1 <= num <= 36:
                parsed = []
                break
            parsed.append(num)
        for num in parsed:
            if num not in numbers:
                numbers.append(num)
        if numbers:
            break
    return numbers


def _parse_hole_num(tags: dict):
    numbers = _parse_hole_numbers(tags)
    return numbers[0] if numbers else None


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
    """
    Return the hole's centreline running tee end → green end.

    Orientation is decided by the green, not the tees. A hole has exactly one
    green but often three or four tee boxes strung out over 400 m, and one of
    those may be the next hole's tee that landed here during spatial grouping.
    Averaging them put the "tee" in the middle of the hole and flipped the line
    at random; the green is unambiguous.
    """
    line = _hole_line_xz(h, origin_lat, origin_lon)
    if len(line) < 2:
        return line

    target = None
    pin_pts = _to_xz_list(h["pins"][:1], origin_lat, origin_lon)
    if pin_pts:
        target = _centroid(pin_pts)
    elif h["greens"]:
        green_pts = _to_xz_list(h["greens"][:1], origin_lat, origin_lon)
        if green_pts:
            target = _centroid(green_pts)

    if target is not None:
        start_d = math.hypot(target[0]-line[0][0], target[1]-line[0][1])
        end_d = math.hypot(target[0]-line[-1][0], target[1]-line[-1][1])
        # The end nearest the green must be last.
        return list(reversed(line)) if start_d < end_d else line

    # No green: fall back to the single tee nearest either end.
    tee_pts = _to_xz_list(h["tees"], origin_lat, origin_lon)
    if tee_pts:
        start_d = min(math.hypot(p[0]-line[0][0], p[1]-line[0][1]) for p in tee_pts)
        end_d = min(math.hypot(p[0]-line[-1][0], p[1]-line[-1][1]) for p in tee_pts)
        return list(reversed(line)) if end_d < start_d else line

    return line


def _select_tee_xz(h: dict, line, pin_xz, origin_lat: float, origin_lon: float):
    """
    Choose which of a hole's tee boxes the imported hole plays from.

    A well-mapped hole has one tee element per tee colour, and spatial grouping
    can add a neighbouring hole's tee on top of that. Taking the first one in
    the list gave holes that started beside their own green.

    Among the tees that actually sit at the start of this hole, the back tee is
    chosen — the one furthest from the pin. That is the tee a published
    scorecard measures from, so it is the one that makes the imported hole play
    its real length.
    """
    candidates = []
    for el in h["tees"]:
        pts = _to_xz_list([el], origin_lat, origin_lon)
        if pts:
            candidates.append(_centroid(pts))
    if not candidates:
        return None

    if not (line and len(line) >= 2):
        if pin_xz is not None and len(candidates) > 1:
            return max(candidates, key=lambda c: math.hypot(c[0]-pin_xz[0], c[1]-pin_xz[1]))
        return candidates[0]

    start = line[0]
    distances = [math.hypot(c[0]-start[0], c[1]-start[1]) for c in candidates]
    nearest = min(distances)
    # Everything within 60 m of the line start is a tee box for this hole;
    # anything further away belongs to a different hole.
    near_start = [c for c, d in zip(candidates, distances) if d <= nearest + 60.0]
    if len(near_start) == 1 or pin_xz is None:
        return near_start[0]

    # Among this hole's tee boxes, pick the one that makes the hole play the
    # length the surveyed centreline says it is. Simply taking the tee furthest
    # from the pin reaches past the back tee onto a neighbouring hole's, which
    # stretched Augusta's 15th by 60 m.
    target = _polyline_length(line)
    return min(near_start,
               key=lambda c: abs(math.hypot(c[0]-pin_xz[0], c[1]-pin_xz[1]) - target))


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

        distances = {}
        for num, (line, anchor) in hole_shapes.items():
            if line and tag in ("tee", "green", "pin", "flagstick"):
                # Tees and greens belong at an *end* of a hole, not alongside it.
                d = min(math.hypot(pt[0]-line[0][0], pt[1]-line[0][1]),
                        math.hypot(pt[0]-line[-1][0], pt[1]-line[-1][1]))
            elif line:
                d = _point_polyline_distance(pt, line)
            else:
                d = math.hypot(pt[0]-anchor[0], pt[1]-anchor[1])
            distances[num] = d

        if not distances:
            continue
        best_num = min(distances, key=distances.get)
        best_d = distances[best_num]

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

        if best_num is None or best_d > threshold:
            continue

        _classify_into(el, holes[best_num])

        # A shared green sits at the end of two holes at once. When a second
        # hole also finishes right on it, give it that green too — otherwise
        # one of the pair imports with nothing to putt on. The radius is tight
        # on purpose: a merely *nearby* green belongs to its own hole, and
        # attaching it here would stack two greens on one pin.
        if tag == "green" and len(pts) >= 3:
            # Whether two holes really share a green is answered by the green
            # itself, not by a distance threshold: both holes must finish *on
            # the polygon*. A links double green is big enough for that; on a
            # compact par-3 course the next green is 25 m away and no hole but
            # its own ends on it. Comparing against centroids instead handed
            # half of Marienlyst's holes a second green.
            for num, (line, _anchor) in hole_shapes.items():
                if num == best_num or not line:
                    continue
                if _point_to_polygon_distance_xz(line[-1], pts) <= 12.0:
                    _classify_into(el, holes[num])


def _resolve_duplicate_hole_lines(elements: list) -> list:
    """
    Drop hole centrelines belonging to a second course inside the same boundary.

    A single `leisure=golf_course` polygon often covers more than one course —
    Augusta National's contains the Par 3 Course, so refs 1–9 appear twice.
    Left alone, the two sets merge: a 410 m par 4 and a 125 m par 3 become one
    hole, and the par tag of whichever won is applied to it.

    Holes whose ref appears only once are treated as the real course, and each
    contested ref is resolved to the candidate that sits nearest to them and
    is closest to their typical length. Both signals are needed: proximity
    alone can be fooled where the two courses interleave, and length alone
    would pick wrong on a course with a genuinely short hole.
    """
    lines = [el for el in elements if el.get("tags", {}).get("golf") == "hole"]
    by_ref: dict[int, list] = {}
    for el in lines:
        num = _parse_hole_num(el.get("tags", {}))
        if num is not None:
            by_ref.setdefault(num, []).append(el)

    contested = {ref: els for ref, els in by_ref.items() if len(els) > 1}
    if not contested:
        return elements

    def centroid_latlon(el):
        pts = _element_geom(el)
        return _centroid(pts) if pts else None

    def length_m(el):
        pts = _element_geom(el)
        return sum(_latlon_distance_m(a[0], a[1], b[0], b[1]) for a, b in zip(pts, pts[1:]))

    anchors = [els[0] for ref, els in by_ref.items() if len(els) == 1]
    if not anchors:
        # Two complete courses share the boundary. Keep the longer one, which
        # is the championship course rather than an academy or par-3 layout.
        medians = {}
        for ref, els in by_ref.items():
            for i, el in enumerate(els):
                medians.setdefault(i, []).append(length_m(el))
        best_index = max(medians, key=lambda i: sorted(medians[i])[len(medians[i]) // 2])
        anchors = [els[best_index] for els in by_ref.values() if len(els) > best_index]
        print("  [warn] two complete courses share this boundary; keeping the longer "
              "layout. Use --list and --id to import the other one.", file=sys.stderr)

    anchor_points = [p for p in (centroid_latlon(el) for el in anchors) if p]
    anchor_lengths = sorted(length_m(el) for el in anchors)
    typical_length = anchor_lengths[len(anchor_lengths) // 2] if anchor_lengths else 300.0

    dropped = set()
    kept_lines: dict[int, list] = {}
    for ref, els in by_ref.items():
        if len(els) == 1:
            kept_lines[ref] = _element_geom(els[0])
            continue

        def score(el):
            point = centroid_latlon(el)
            if point is None or not anchor_points:
                return float("inf")
            nearest = min(_latlon_distance_m(point[0], point[1], a[0], a[1])
                          for a in anchor_points)
            length_penalty = abs(length_m(el) - typical_length)
            return nearest + length_penalty

        keeper = min(els, key=score)
        kept_lines[ref] = _element_geom(keeper)
        for el in els:
            if _element_key(el) != _element_key(keeper):
                dropped.add(_element_key(el))

    # The other course duplicates more than its centrelines: its tees, pins,
    # greens and bunkers carry the same refs. Anything claiming a ref while
    # sitting far from that hole's kept centreline belongs to the layout we
    # just discarded.
    foreign = 0
    for el in elements:
        key = _element_key(el)
        if key in dropped or el.get("tags", {}).get("golf") == "hole":
            continue
        ref = _parse_hole_num(el.get("tags", {}))
        line = kept_lines.get(ref) if ref is not None else None
        if not line:
            continue
        point = centroid_latlon(el)
        if point is None:
            continue
        if min(_latlon_distance_m(point[0], point[1], a, b) for a, b in line) > 250.0:
            dropped.add(key)
            foreign += 1

    if dropped:
        print(f"  [warn] dropped {len(dropped) - foreign} hole centreline(s) and {foreign} "
              f"other feature(s) belonging to a second course inside this boundary",
              file=sys.stderr)
    return [el for el in elements if _element_key(el) not in dropped]


_PREFIXED_REF = re.compile(r"\s*([A-Za-z]*)\s*0*(\d{1,2})\s*")


def _split_ref(tags: dict) -> tuple[str, int] | None:
    """("P", 3) for ref "P3", ("", 12) for "12"; None without a single-hole ref."""
    m = _PREFIXED_REF.fullmatch(str(tags.get("ref", "")))
    return (m.group(1).upper(), int(m.group(2))) if m else None


def select_course_by_ref_prefix(elements: list, prefix: str, origin_lat: float, origin_lon: float) -> list:
    """
    Keep the course whose hole refs carry `prefix` ("" for plain numbers) when
    one boundary holds several courses told apart by ref, like Kalø's main
    course (1–18) and its par-3 course (P1–P9).

    Hole lines of the chosen course are renumbered to plain numbers and the
    other courses' lines are dropped. A feature with a ref follows its prefix;
    one without goes to whichever course has the nearest hole line, so neither
    course steals the other's greens and bunkers. With a single course in the
    boundary the elements come back unchanged.
    """
    prefix = prefix.upper()
    lines = [el for el in elements if el.get("tags", {}).get("golf") == "hole" and _split_ref(el.get("tags", {}))]
    prefixes = {_split_ref(el["tags"])[0] for el in lines}
    if prefix not in prefixes:
        found = ", ".join(repr(p) for p in sorted(prefixes)) or "none"
        raise ValueError(f"no holes with ref prefix {prefix!r} in this boundary (found: {found})")
    if prefixes == {prefix} and not prefix:
        return elements

    line_xz = [(_split_ref(el["tags"])[0] == prefix, _to_xz_list([el], origin_lat, origin_lon)) for el in lines]

    def nearest_line_is_ours(el) -> bool:
        pts = _to_xz_list([el], origin_lat, origin_lon)
        if not pts:
            return False
        point = _centroid(pts)
        ours = min((_point_polyline_distance(point, xz) for mine, xz in line_xz if mine and len(xz) > 1), default=math.inf)
        theirs = min((_point_polyline_distance(point, xz) for mine, xz in line_xz if not mine and len(xz) > 1), default=math.inf)
        return ours <= theirs

    selected = []
    foreign = 0
    for el in elements:
        tags = el.get("tags", {})
        ref = _split_ref(tags)
        if ref is not None:
            if ref[0] != prefix:
                foreign += tags.get("golf") != "hole"
                continue
            el = {**el, "tags": {**tags, "ref": str(ref[1])}}
        elif tags.get("golf") not in (None, "hole") and not nearest_line_is_ours(el):
            foreign += 1
            continue
        selected.append(el)
    kept = sum(1 for el in lines if _split_ref(el["tags"])[0] == prefix)
    print(f"  Course with ref prefix {prefix!r}: {kept} hole line(s); "
          f"dropped {len(lines) - kept} line(s) and {foreign} feature(s) of the other course(s)", file=sys.stderr)
    return selected


def group_holes(elements: list) -> dict:
    """
    Returns {hole_number: hole_dict}.

    Strategy (in priority order):
      1. golf=hole relations with members
      2. Elements with ref=N or hole=N tag
      3. Spatial proximity to tee nodes
    """
    elements = _resolve_duplicate_hole_lines(elements)
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
        numbers = _parse_hole_numbers(el.get("tags", {}))
        # A shared green or fairway belongs to every hole it names, not just
        # the first: both holes need the putting surface.
        for num in numbers:
            _classify_into(el, holes.setdefault(num, _empty_hole()))
        if numbers:
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
                          course_key: str, max_per_hole: int = 60):
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
        h["trees_abs"] = deduped[:max(0, max_per_hole)]


# ── Hole → JSON ───────────────────────────────────────────────────────────────

def _r(v): return round(v, 2)


def _int_tag(value):
    """Parse an OSM tag that should be a small integer, tolerating junk."""
    if value is None:
        return None
    m = re.match(r"\s*(\d{1,2})", str(value))
    return int(m.group(1)) if m else None


def hole_to_json(hole_num: int, h: dict, origin_lat: float, origin_lon: float,
                 course_id: str, config: dict | None = None,
                 elevation: "osm_elevation.ElevationSampler | None" = None) -> dict:
    config = config or DEFAULT_CONFIG
    tree_config = config.get("tree", {})
    hole_config = config.get("hole", {})

    line_pts = _oriented_hole_line_xz(h, origin_lat, origin_lon)

    # ── pin position ──────────────────────────────────────────────────────────
    # Resolved before the tee, because picking the right tee box needs to know
    # which end of the hole the green is at.
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

    # ── tee position ──────────────────────────────────────────────────────────
    pin_hint = (pin_x, pin_z) if pin_x is not None else None
    tee_xz = _select_tee_xz(h, line_pts, pin_hint, origin_lat, origin_lon)

    if tee_xz is None:
        if line_pts:
            tee_xz = line_pts[0]
        elif h["fairways"]:
            fw_all = _to_xz_list(h["fairways"], origin_lat, origin_lon)
            tee_xz = min(fw_all, key=lambda p: math.hypot(p[0], p[1])) if fw_all else None
        if tee_xz is None:
            print(f"  [warn] hole {hole_num}: no tee, using course origin", file=sys.stderr)
            tee_xz = (0.0, 0.0)

    tee_x, tee_z = tee_xz

    if pin_x is None:
        print(f"  [warn] hole {hole_num}: no pin/green, estimating 100m ahead", file=sys.stderr)
        pin_x, pin_z = tee_x, tee_z + 100.0

    # ── fairway spline ────────────────────────────────────────────────────────
    fw_pts = _to_xz_list(h["fairways"], origin_lat, origin_lon)
    pin_xz = (pin_x, pin_z)

    ctrl_xz, width, rough_width, ctrl_source = _hole_centerline(
        h, line_pts, fw_pts, tee_xz, pin_xz, hole_config)
    if ctrl_source == "straight" and not h["fairways"]:
        print(f"  [warn] hole {hole_num}: no fairway or hole way, using straight spline",
              file=sys.stderr)

    # ── elevation ─────────────────────────────────────────────────────────────
    # OSM has no usable height data for golf features, so heights come from a
    # DEM instead. Only the spline carries elevation (heights and side slope):
    # the game builds terrain as a ribbon swept along these control points and
    # samples zone and tree heights off that mesh, so baking y anywhere else
    # would be ignored.
    ctrl_y = [0.0] * len(ctrl_xz)
    pin_y = 0.0
    tee_elevation = None
    bank = None
    if elevation is not None:
        elevation_config = (config or DEFAULT_CONFIG).get("elevation", {})
        samples = elevation.elevations(
            [_xz_to_latlon(x, z, origin_lat, origin_lon) for x, z in ctrl_xz] +
            [_xz_to_latlon(pin_x, pin_z, origin_lat, origin_lon)])
        distances = osm_elevation.polyline_distances(ctrl_xz)
        pin_distance = distances[-1] + math.hypot(pin_x - ctrl_xz[-1][0], pin_z - ctrl_xz[-1][1])
        absolute = osm_elevation.cleaned_profile(
            samples, distances + [pin_distance],
            max_grade=float(elevation_config.get("max_grade", 0.25)),
            smooth_window=int(elevation_config.get("smooth_window", 3)))
        tee_elevation = round(absolute[0], 2)
        profile = osm_elevation.relative_profile(absolute)
        ctrl_y, pin_y = profile[:-1], profile[-1]

        # The land's side slope across the hole, so the ribbon tilts with a
        # hillside instead of burying one edge and floating the other.
        ribbon_width = max(width, rough_width)
        sides = osm_elevation.lateral_offsets(ctrl_xz, ribbon_width * 0.5)
        side_heights = elevation.elevations(
            [_xz_to_latlon(x, z, origin_lat, origin_lon) for pair in sides for x, z in pair])
        bank = osm_elevation.bank_profile(side_heights[0::2], side_heights[1::2], ribbon_width,
                                          max_bank=float(elevation_config.get("max_bank", 0.2)),
                                          smooth_window=int(elevation_config.get("smooth_window", 3)))

    # Control points relative to this hole's tee (which is [0,0,0])
    def rel_xyz(xz, y=0.0): return [_r(xz[0]-tee_x), _r(y), _r(xz[1]-tee_z)]

    # ── material zones ────────────────────────────────────────────────────────
    zones = []

    for el in h["greens"]:
        pts = _to_xz_list([el], origin_lat, origin_lon)
        if pts:
            # Anchor the green on the pin when we have one. A shared double
            # green covers two holes, so its centroid sits between them; the
            # pin says which half is this hole's.
            cx, cz, r = _zone_circle(el, pts, anchor=pin_xz,
                                     min_radius=5.0, max_radius=float(
                                         hole_config.get("max_green_radius", 22.0)))
            zones.append({
                "type": "green",
                "center": [_r(cx-tee_x), 0, _r(cz-tee_z)],
                "radius": _r(r)
            })

    for el in h["bunkers"]:
        pts = _to_xz_list([el], origin_lat, origin_lon)
        if pts:
            cx, cz, r = _zone_circle(el, pts, anchor=None,
                                     min_radius=1.5, max_radius=float(
                                         hole_config.get("max_bunker_radius", 18.0)))
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

    # ── par and name ──────────────────────────────────────────────────────────
    # h["tags"] is every tag of every element on the hole merged together, so
    # reading par or name straight out of it can pick up a bunker's name or a
    # neighbouring feature's par. The hole way is the element that actually
    # describes the hole, so ask it first.
    hole_tags = {}
    for el in h["lines"]:
        hole_tags.update(el.get("tags", {}))

    par = _int_tag(hole_tags.get("par")) or _int_tag(h["tags"].get("par")) or 0
    if not (3 <= par <= 6):
        dist = max(math.hypot(pin_x-tee_x, pin_z-tee_z), _polyline_length(ctrl_xz))
        par = 3 if dist < 200 else (4 if dist < 430 else 5)

    name = hole_tags.get("name") or f"Hole {hole_num}"
    if _parse_hole_numbers({"name": name}):
        # A name that is only a hole number ("#17", "3/15") is a label, not a
        # name; the generated "Hole 17" reads better in the HUD.
        name = f"Hole {hole_num}"

    trees = [{
        "position": [_r(x - tee_x), 0.0, _r(z - tee_z)],
        "trunk_radius": float(tree_config.get("trunk_radius", 0.65)),
        "trunk_height": float(tree_config.get("trunk_height", 5.0)),
        "leaf_radius": float(tree_config.get("leaf_radius", 4.7)),
        "leaf_height": float(tree_config.get("leaf_height", 6.0))
    } for x, z in h.get("trees_abs", [])]

    return {
        "id": f"{course_id}_h{hole_num:02d}",
        "name": name,
        "par": par,
        "wind_seed": _stable_int_seed(course_id, hole_num, "wind") % 9999 + 1,
        "tee": [0.0, 0.0, 0.0],
        "pin": [_r(pin_x-tee_x), _r(pin_y), _r(pin_z-tee_z)],
        "spline": {
            "control_points": [rel_xyz(p, y) for p, y in zip(ctrl_xz, ctrl_y)],
            **({"bank": bank} if bank is not None else {}),
            "width": width,
            "rough_width": rough_width
        },
        "material_zones": zones,
        "trees": trees,
        # Provenance. Keeps the hole traceable back to the map it came from,
        # lets a re-import be compared against the previous one, and gives
        # verify_osm_import.py real coordinates to check the projection
        # against without sharing any code with it.
        "source": {
            "type": "osm",
            "tee_latlon": [round(c, 7) for c in _xz_to_latlon(tee_x, tee_z, origin_lat, origin_lon)],
            "pin_latlon": [round(c, 7) for c in _xz_to_latlon(pin_x, pin_z, origin_lat, origin_lon)],
            "centerline": ctrl_source,
            "elevation": elevation.dataset if elevation is not None else None,
            # The tee's DEM height above sea level. The hole itself is
            # tee-relative; the course world places hole starts at these
            # heights so the holes line up with each other in the hub.
            "tee_elevation": tee_elevation
        }
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


def _hole_fairway_corridor(h: dict,
                           tee_xz: tuple[float, float],
                           pin_xz: tuple[float, float],
                           origin_lat: float,
                           origin_lon: float,
                           config: dict) -> dict:
    hole_config = config.get("hole", {})
    world_config = config.get("world", {})
    fw_pts = _to_xz_list(h["fairways"], origin_lat, origin_lon)
    if len(fw_pts) >= 4:
        line = _fairway_centerline(fw_pts, tee_xz, pin_xz, n=8)
        width = _fairway_width(fw_pts, tee_xz, pin_xz)
    else:
        line = _oriented_hole_line_xz(h, origin_lat, origin_lon)
        if len(line) < 2:
            line = [tee_xz, pin_xz]
        width = float(hole_config.get("fallback_width", 20.0))

    clearance = float(world_config.get("fairway_avoidance_clearance", 8.0))
    return {
        "line": line,
        "radius": max(8.0, width * 0.5 + clearance),
        "width": width,
    }


def _polyline_direction(line: list[tuple[float, float]], from_start: bool) -> tuple[float, float]:
    if len(line) < 2:
        return (0.0, 1.0)
    indices = range(0, len(line) - 1) if from_start else range(len(line) - 2, -1, -1)
    for i in indices:
        a, b = line[i], line[i + 1]
        dx, dz = b[0] - a[0], b[1] - a[1]
        length = math.hypot(dx, dz)
        if length > 0.001:
            return (dx / length, dz / length)
    return (0.0, 1.0)


def _offset_from_fairway(point: tuple[float, float],
                         corridor: dict,
                         side: float,
                         from_start: bool,
                         extra_offset: float) -> tuple[float, float]:
    dx, dz = _polyline_direction(corridor.get("line", []), from_start)
    px, pz = -dz * side, dx * side
    offset = float(corridor.get("radius", 16.0)) + extra_offset
    return (point[0] + px * offset, point[1] + pz * offset)


def _detour_point_away_from_fairways(a: tuple[float, float],
                                     b: tuple[float, float],
                                     fairway_corridors: list[dict],
                                     extra_offset: float) -> tuple[float, float] | None:
    if not fairway_corridors:
        return None
    segment = [a, b]
    if not _route_overlaps_fairway(segment, fairway_corridors):
        return None

    mid = ((a[0] + b[0]) * 0.5, (a[1] + b[1]) * 0.5)
    corridor = min(fairway_corridors,
                   key=lambda c: _point_polyline_distance(mid, c.get("line", [])))
    line = corridor.get("line", [])
    dx, dz = _polyline_direction(line, True)
    px, pz = -dz, dx
    ref = line[0] if line else mid
    side = 1.0 if ((mid[0] - ref[0]) * px + (mid[1] - ref[1]) * pz) >= 0.0 else -1.0
    offset = float(corridor.get("radius", 16.0)) + extra_offset
    return (mid[0] + px * side * offset, mid[1] + pz * side * offset)


def _detour_polyline_away_from_fairways(points: list[tuple[float, float]],
                                        fairway_corridors: list[dict],
                                        extra_offset: float) -> list[tuple[float, float]]:
    if len(points) < 2:
        return points
    out = [points[0]]
    for a, b in zip(points, points[1:]):
        detour = _detour_point_away_from_fairways(a, b, fairway_corridors, extra_offset)
        if detour is not None:
            out.append(detour)
        out.append(b)
    return out


def _route_overlaps_fairway(pts: list[tuple[float, float]],
                            fairway_corridors: list[dict],
                            sample_spacing: float = 4.0) -> bool:
    if not pts or not fairway_corridors:
        return False
    sample_count = max(2, min(120, int(max(1.0, _polyline_length(pts)) / sample_spacing) + 1))
    samples = pts + _resample_polyline(pts, sample_count)
    for sample in samples:
        for corridor in fairway_corridors:
            if _point_polyline_distance(sample, corridor.get("line", [])) <= float(corridor.get("radius", 0.0)):
                return True
    return False


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


def _fallback_cart_roads(spawn: tuple[float, float],
                         hole_anchors: list[dict],
                         width: float = 4.0,
                         fairway_corridors: list[dict] | None = None,
                         extra_offset: float = 8.0) -> list[dict]:
    if not hole_anchors:
        return []

    if fairway_corridors:
        fairway_points = [pt for corridor in fairway_corridors for pt in corridor.get("line", [])]
        max_radius = max(float(corridor.get("radius", 0.0)) for corridor in fairway_corridors)
        spine_x = min([spawn[0]] + [pt[0] for pt in fairway_points]) - max_radius - extra_offset - 12.0
        chain = [(spine_x, spawn[1])]
        for anchor in hole_anchors:
            chain.append((spine_x, anchor["start_xz"][1]))
            chain.append((spine_x, anchor["return_xz"][1]))
        road_points = chain
    else:
        chain = [spawn]
        for anchor in hole_anchors:
            chain.append(anchor.get("road_start_xz", anchor["start_xz"]))
            chain.append(anchor.get("road_return_xz", anchor["return_xz"]))
        road_points = _detour_polyline_away_from_fairways(chain, [], extra_offset)

    return [{
        "id": "fallback_main_cart_loop",
        "surface": "gravel",
        "width": width,
        "source": "generated_fallback",
        "polyline": [_xyz_from_xz(pt) for pt in road_points]
    }]


def fitness_skill_id_for_world() -> str:
    return "fitness"


def _path_near_holes(pts: list[tuple[float, float]], hole_anchors: list[dict], max_distance: float) -> bool:
    if not hole_anchors:
        return True
    route_refs = []
    for anchor in hole_anchors:
        route_refs.append(anchor["start_xz"])
        route_refs.append(anchor["return_xz"])
    return any(_point_polyline_distance(ref, pts) <= max_distance for ref in route_refs)


def _world_paths_from_osm(path_elements: list,
                          origin_lat: float,
                          origin_lon: float,
                          hole_anchors: list[dict],
                          fairway_corridors: list[dict],
                          config: dict) -> tuple[list[dict], list[dict]]:
    cart_roads = []
    shortcuts = []
    world_config = config.get("world", {})
    max_distance = float(world_config.get("max_path_distance_from_holes", 75.0))
    max_shortcut_length = float(world_config.get("max_shortcut_length", 180.0))
    max_cart_road_length = float(world_config.get("max_cart_road_length", 280.0))
    shortcut_width = float(world_config.get("shortcut_width", 2.0))
    max_shortcut_count = int(world_config.get("max_shortcut_count", 12))
    for el in path_elements:
        pts = _path_polyline(el, origin_lat, origin_lon)
        if not pts:
            continue
        if not _path_near_holes(pts, hole_anchors, max_distance):
            continue
        if _route_overlaps_fairway(pts, fairway_corridors):
            continue
        length = _polyline_length(pts)
        tags = el.get("tags", {})
        osm_ref = f"{el.get('type', '?')}/{el.get('id', '?')}"
        if _is_cart_road(el):
            if length > max_cart_road_length:
                continue
            cart_roads.append({
                "id": f"cart_{el.get('type', 'way')}_{el.get('id')}",
                "surface": "asphalt" if tags.get("surface") in ("asphalt", "paved", "concrete") else "gravel",
                "width": float(world_config.get("fallback_cart_width", 4.0)),
                "source": "osm",
                "osm_ref": osm_ref,
                "polyline": [_xyz_from_xz(pt) for pt in pts]
            })
        else:
            if length > max_shortcut_length:
                continue
            shortcuts.append({
                "id": f"shortcut_{el.get('type', 'way')}_{el.get('id')}",
                "surface": tags.get("surface", "dirt"),
                "width": shortcut_width,
                "source": "osm",
                "osm_ref": osm_ref,
                "required_skill_id": fitness_skill_id_for_world(),
                "required_level": 2,
                "polyline": [_xyz_from_xz(pt) for pt in pts]
            })
    shortcuts.sort(key=lambda route: _polyline_length([(pt[0], pt[2]) for pt in route.get("polyline", [])]))
    if max_shortcut_count >= 0:
        shortcuts = shortcuts[:max_shortcut_count]
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
                         origin_lon: float,
                         config: dict | None = None,
                         tee_elevations: dict | None = None) -> dict:
    """
    `tee_elevations` maps hole number to the tee's absolute height (the hole
    JSON's source.tee_elevation). Hole starts are placed at those heights
    relative to the first hole's tee, which sits at y = 0; holes without one
    stay at y = 0.
    """
    config = config or DEFAULT_CONFIG
    world_config = config.get("world", {})
    hole_anchors = []
    fairway_corridors = []
    for output_index, hole_num in enumerate(sorted(holes.keys())):
        tee_xz, pin_xz = _hole_world_anchors(hole_num, holes[hole_num], origin_lat, origin_lon)
        corridor = _hole_fairway_corridor(holes[hole_num], tee_xz, pin_xz, origin_lat, origin_lon, config)
        side = 1.0
        extra_offset = float(world_config.get("fallback_road_extra_offset", 8.0))
        hole_anchors.append({
            "hole_num": hole_num,
            "hole_index": output_index,
            "start_xz": tee_xz,
            "return_xz": pin_xz,
            "road_start_xz": _offset_from_fairway(tee_xz, corridor, side, True, extra_offset),
            "road_return_xz": _offset_from_fairway(pin_xz, corridor, side, False, extra_offset),
        })
        fairway_corridors.append(corridor)

    if hole_anchors:
        first = hole_anchors[0]
        dx, dz = _polyline_direction(fairway_corridors[0].get("line", []), True) if fairway_corridors else (0.0, 1.0)
        road_start = first.get("road_start_xz", first["start_xz"])
        spawn = (road_start[0] - dx * 18.0, road_start[1] - dz * 18.0)
    else:
        spawn = (0.0, 0.0)

    cart_roads, shortcuts = _world_paths_from_osm(path_elements,
                                                  origin_lat,
                                                  origin_lon,
                                                  hole_anchors,
                                                  fairway_corridors,
                                                  config)
    if not cart_roads:
        cart_roads = _fallback_cart_roads(spawn,
                                          hole_anchors,
                                          float(world_config.get("fallback_cart_width", 4.0)),
                                          fairway_corridors,
                                          float(world_config.get("fallback_road_extra_offset", 8.0)))

    tee_elevations = {num: height for num, height in (tee_elevations or {}).items() if height is not None}
    base_elevation = tee_elevations.get(hole_anchors[0]["hole_num"], 0.0) if hole_anchors else 0.0
    hole_starts = []
    for anchor in hole_anchors:
        start_position = _xyz_from_xz(anchor["start_xz"])
        if anchor["hole_num"] in tee_elevations:
            start_position[1] = _r(tee_elevations[anchor["hole_num"]] - base_elevation)
        hole_starts.append({
            "id": f"hole_{anchor['hole_num']:02d}_start",
            "hole_index": anchor["hole_index"],
            "position": start_position,
            "return_position": _xyz_from_xz(anchor["return_xz"]),
            "interaction_radius": float(world_config.get("hole_start_interaction_radius", 4.0))
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


def _make_elevation_sampler(args, config: dict, origin_lat: float, origin_lon: float):
    """Build a DEM sampler for this course, or None when elevation is disabled."""
    elevation_config = config.get("elevation", {})
    if args.no_elevation or not elevation_config.get("enabled", True):
        print("  Elevation sampling disabled; every hole will be flat.", file=sys.stderr)
        return None

    sampler = osm_elevation.ElevationSampler(cache_dir=_CACHE_DIR,
                                             zoom=int(elevation_config.get("zoom", osm_elevation.DEFAULT_ZOOM)),
                                             refresh=_CACHE_REFRESH)
    print(f"  Sampling elevation from {sampler.dataset} tiles at zoom {sampler.zoom}", file=sys.stderr)
    return sampler


def _report_elevation(sampler) -> None:
    if sampler is not None:
        print(f"→ Elevation: {sampler.tiles_downloaded} tile(s) downloaded, "
              f"{sampler.tiles_from_cache} from the cache", file=sys.stderr)


def _course_ground(world: dict, hole_jsons: list[dict], sampler,
                   origin_lat: float, origin_lon: float, config: dict) -> dict:
    ground_config = config.get("ground", {})
    return osm_ground.ground_grid(world, hole_jsons, sampler,
                                  lambda x, z: _xz_to_latlon(x, z, origin_lat, origin_lon),
                                  float(ground_config.get("cell_size", 20.0)),
                                  float(ground_config.get("margin", 120.0)))


def _write_ground_only(args) -> None:
    """Adds or replaces the ground grid of an existing course world."""
    course_path = Path(args.course_out) / f"{args.ground_only}.json"
    with open(course_path, "r", encoding="utf-8") as f:
        course = json.load(f)
    asset_root = course_path.parent.parent
    world_path = asset_root / course["world"]
    with open(world_path, "r", encoding="utf-8") as f:
        world = json.load(f)
    hole_jsons = []
    for hole_ref in course["holes"]:
        with open(asset_root / hole_ref, "r", encoding="utf-8") as f:
            hole_jsons.append(json.load(f))

    config = load_generation_config(args.config, args.ground_only)
    projection = world["projection"]
    origin_lat, origin_lon = float(projection["origin_lat"]), float(projection["origin_lon"])
    elevation = _make_elevation_sampler(args, config, origin_lat, origin_lon)
    world["ground"] = _course_ground(world, hole_jsons, elevation, origin_lat, origin_lon, config)
    with open(world_path, "w", encoding="utf-8") as f:
        json.dump(world, f, indent=2)
        f.write("\n")
    _report_elevation(elevation)
    ground = world["ground"]
    relief = max(ground["heights"]) - min(ground["heights"])
    print(f"→ Ground: {ground['columns']}x{ground['rows']} cells of {ground['cell_size']} m, "
          f"relief {relief:.1f} m, in {world_path}", file=sys.stderr)


# Letters that do not decompose to ASCII; everything else loses its accents.
_SLUG_LETTERS = {"ø": "o", "æ": "ae", "å": "aa", "ß": "ss", "þ": "th", "ð": "d", "ł": "l", "œ": "oe"}


def slugify(name: str) -> str:
    """File-safe id: "Kalø Golf Club" -> "kalo_golf_club"."""
    lowered = "".join(_SLUG_LETTERS.get(c, c) for c in name.lower())
    ascii_only = unicodedata.normalize("NFKD", lowered).encode("ascii", "ignore").decode("ascii")
    return re.sub(r"[^a-z0-9]+", "_", ascii_only).strip("_")


def _project_root() -> Path:
    return Path(__file__).resolve().parent.parent.parent


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
    default_config = Path(__file__).resolve().parent / "osm_golf_config.json"
    ap.add_argument("-o", "--out", default=str(default_holes), metavar="DIR",
                    help=f"Output directory for hole JSON files (default: {default_holes})")
    ap.add_argument("--course-out", default=str(default_courses), metavar="DIR",
                    help=f"Output directory for course manifest JSON (default: {default_courses})")
    ap.add_argument("--world-out", default=str(default_worlds), metavar="DIR",
                    help=f"Output directory for course-world JSON (default: {default_worlds})")
    ap.add_argument("--config", default=str(default_config), metavar="FILE",
                    help=f"Generation tuning JSON (default: {default_config})")
    ap.add_argument("--overpass", metavar="URL",
                    help="Custom Overpass API URL (default: overpass-api.de)")
    default_cache = Path(__file__).resolve().parent / ".osm_cache"
    ap.add_argument("--cache-dir", default=str(default_cache), metavar="DIR",
                    help=f"Cache raw OSM/elevation responses here (default: {default_cache})")
    ap.add_argument("--no-cache", action="store_true",
                    help="Do not read or write the response cache")
    ap.add_argument("--refresh", action="store_true",
                    help="Ignore cached responses and re-download from OSM")
    ap.add_argument("--list", dest="list_courses", action="store_true",
                    help="List matching courses with their --id values and exit")
    ap.add_argument("--no-elevation", action="store_true",
                    help="Leave every control point at y=0 instead of sampling a DEM")
    ap.add_argument("--no-course", action="store_true",
                    help="Skip writing the course manifest JSON")
    ap.add_argument("--no-world", action="store_true",
                    help="Skip writing the course-world JSON")
    ap.add_argument("--ref-prefix", default="", metavar="PREFIX",
                    help="Import the course whose hole refs start with PREFIX when one boundary holds "
                         "several, e.g. P for par-3 holes P1-P9 (default: plain numbers)")
    ap.add_argument("--course-name", metavar="NAME",
                    help="Name (and id) for the imported course instead of the OSM name, "
                         "e.g. \"Kalø Par 3\" with --ref-prefix P")
    ap.add_argument("--ground-only", metavar="COURSE_ID",
                    help="Only (re)write the ground grid of an existing course world, "
                         "e.g. a hand-made one; reads the course and its holes from the asset folders")
    args = ap.parse_args()

    if args.ground_only:
        set_cache(None if args.no_cache else args.cache_dir, refresh=args.refresh)
        _write_ground_only(args)
        return

    if not any([args.name, args.id, args.lat is not None, args.lon is not None]):
        ap.print_help()
        sys.exit(0)
    if (args.lat is None) != (args.lon is None):
        print("--lat and --lon must be provided together.", file=sys.stderr)
        sys.exit(1)

    if args.overpass:
        OVERPASS_INSTANCES.insert(0, args.overpass)

    set_cache(None if args.no_cache else args.cache_dir, refresh=args.refresh)

    # ── 1. Locate the course ─────────────────────────────────────────────────
    print("→ Locating course on OSM...", file=sys.stderr)
    course_el, course_name = _find_course(args)
    course_name = args.course_name or course_name
    course_id = slugify(course_name)
    config = load_generation_config(args.config, course_id)
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
    try:
        elements = select_course_by_ref_prefix(elements, args.ref_prefix, origin_lat, origin_lon)
    except ValueError as e:
        print(f"\n{e}", file=sys.stderr)
        sys.exit(1)
    holes = group_holes(elements)

    if not holes:
        print("\nCould not identify individual holes.\n"
              "OSM data may be missing hole relations or ref tags.", file=sys.stderr)
        sys.exit(1)

    print(f"  Identified {len(holes)} hole(s)", file=sys.stderr)
    assign_trees_to_holes(holes,
                          tree_elements,
                          origin_lat,
                          origin_lon,
                          f"{course_el.get('type')}:{course_el.get('id')}",
                          int(config.get("tree", {}).get("max_per_hole", 60)))

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

    elevation = _make_elevation_sampler(args, config, origin_lat, origin_lon)

    source_counts: dict[str, int] = {}
    tee_elevations: dict[int, float | None] = {}
    hole_jsons = []
    for num in sorted(holes.keys()):
        print(f"  Processing hole {num}...", file=sys.stderr)
        h_json = hole_to_json(num, holes[num], origin_lat, origin_lon, course_id, config, elevation)
        fname = f"{course_id}_h{num:02d}.json"
        fpath = out_dir / fname
        with open(fpath, "w", encoding="utf-8") as f:
            json.dump(h_json, f, indent=2)
        hole_paths.append(f"holes/{fname}")
        tee_elevations[num] = h_json["source"]["tee_elevation"]
        hole_jsons.append(h_json)
        dist = math.hypot(h_json["pin"][0], h_json["pin"][2])
        path_len = _hole_json_path_length(h_json)
        width = h_json["spline"]["width"]
        source = h_json["source"]["centerline"]
        source_counts[source] = source_counts.get(source, 0) + 1
        ys = [p[1] for p in h_json["spline"]["control_points"]]
        relief = f"  relief {max(ys) - min(ys):.0f}m" if elevation else ""
        print(f"    {fname}  par {h_json['par']}  direct {dist:.0f}m  path {path_len:.0f}m  "
              f"width {width:.1f}m  zones {len(h_json['material_zones'])}  "
              f"trees {len(h_json['trees'])}  line:{source}{relief}", file=sys.stderr)
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
                                          origin_lon,
                                          config,
                                          tee_elevations)
        world_json["ground"] = _course_ground(world_json, hole_jsons, elevation, origin_lat, origin_lon, config)
        world_file = world_dir / f"{course_id}.json"
        with open(world_file, "w", encoding="utf-8") as f:
            json.dump(world_json, f, indent=2)
        print(f"\n-> Course world: {world_file}", file=sys.stderr)

    if not args.no_course:
        course_json = {
            "id": course_id,
            "name": course_name,
            "holes": hole_paths
        }
        if not args.no_world:
            course_json["world"] = world_reference
        course_file = course_dir / f"{course_id}.json"
        with open(course_file, "w", encoding="utf-8") as f:
            json.dump(course_json, f, indent=2)
        print(f"\n→ Course manifest: {course_file}", file=sys.stderr)

    if source_counts:
        summary = ", ".join(f"{count}x {name}" for name, count in sorted(source_counts.items()))
        print(f"\n→ Centreline sources: {summary}", file=sys.stderr)
    _report_elevation(elevation)
    print(f"→ Done! {len(holes)} hole(s) in {out_dir}/", file=sys.stderr)


if __name__ == "__main__":
    try:
        main()
    except OverpassUnavailable as e:
        print(f"\n{e}", file=sys.stderr)
        sys.exit(2)
    except KeyboardInterrupt:
        print("\nInterrupted. Whatever downloaded is cached, so a re-run resumes.",
              file=sys.stderr)
        sys.exit(130)
