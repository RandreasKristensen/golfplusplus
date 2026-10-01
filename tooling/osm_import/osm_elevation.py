#!/usr/bin/env python3
"""
osm_elevation.py — Sample real terrain elevation for imported OSM courses.

OpenStreetMap does not carry usable height data for golf features, so the
importer reads a public digital elevation model (DEM) instead and bakes the
result into each hole's spline control points. This is the same idea as the
LiDAR import step in TGC Designer Tools, scaled down to the open DEMs that are
free to query without an account.

Only the standard library is used. `requests` is picked up when it happens to
be installed, matching osm_golf_convert.py.

Datasets (served by api.opentopodata.org):

  mapzen     global merged DEM, ~30 m  — the safe default anywhere on Earth
  eudem25m   Europe, 25 m              — better for Danish/European courses
  ned10m     USA, 10 m                 — better for US courses
  srtm30m    near-global, 30 m

Resolution honesty: a 10–30 m DEM reproduces the landform of a hole — uphill
tee shots, valleys, plateau greens, the slope of a fairway — but it cannot see
green contours, bunker lips, or mounding. Those stay a hole-editor job.
"""

import json
import math
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

try:
    import requests
except ImportError:
    requests = None

OPENTOPODATA_URL = "https://api.opentopodata.org/v1/{dataset}"
HEADERS = {"User-Agent": "osm_golf_convert/2.0 (golf course converter)"}

# Public opentopodata allows 100 locations per call and 1 call/second.
MAX_LOCATIONS_PER_CALL = 100
MIN_SECONDS_BETWEEN_CALLS = 1.05

DATASET_COVERAGE = {
    # dataset: (min_lat, min_lon, max_lat, max_lon) or None for global
    "eudem25m": (34.0, -25.0, 72.0, 45.0),
    "ned10m": (24.0, -125.0, 50.0, -66.0),
    "srtm30m": (-60.0, -180.0, 60.0, 180.0),
    "mapzen": None,
}

DEFAULT_DATASET_PREFERENCE = ["ned10m", "eudem25m", "mapzen"]


def dataset_covers(dataset: str, lat: float, lon: float) -> bool:
    box = DATASET_COVERAGE.get(dataset, None)
    if box is None:
        return True
    min_lat, min_lon, max_lat, max_lon = box
    return min_lat <= lat <= max_lat and min_lon <= lon <= max_lon


def auto_dataset(lat: float, lon: float, preference: list[str] | None = None) -> str:
    """Pick the highest-resolution dataset that covers this coordinate."""
    for dataset in (preference or DEFAULT_DATASET_PREFERENCE):
        if dataset_covers(dataset, lat, lon):
            return dataset
    return "mapzen"


class ElevationSampler:
    """
    Batched DEM sampler with an in-memory cache and optional disk cache.

    Coordinates are rounded to ~1 m before caching so that the many control
    points, zone centres and tees that land on the same DEM cell only cost one
    lookup.
    """

    ROUND_DIGITS = 5  # ~1.1 m of latitude

    def __init__(self, dataset: str = "mapzen", cache: dict | None = None,
                 verbose: bool = True):
        self.dataset = dataset
        self._cache: dict[tuple[float, float], float | None] = dict(cache or {})
        self._last_call = 0.0
        self.verbose = verbose
        self.requests_made = 0
        self.points_resolved = 0
        self.points_missing = 0

    # ── cache plumbing ────────────────────────────────────────────────────────

    def _key(self, lat: float, lon: float) -> tuple[float, float]:
        return (round(lat, self.ROUND_DIGITS), round(lon, self.ROUND_DIGITS))

    def cache_as_dict(self) -> dict:
        return {f"{lat},{lon}": value for (lat, lon), value in self._cache.items()}

    @staticmethod
    def cache_from_dict(raw: dict) -> dict:
        out = {}
        for key, value in (raw or {}).items():
            try:
                lat_s, lon_s = key.split(",")
                out[(float(lat_s), float(lon_s))] = value
            except (ValueError, AttributeError):
                continue
        return out

    # ── network ───────────────────────────────────────────────────────────────

    def _http_get(self, url: str) -> dict:
        if requests is not None:
            r = requests.get(url, timeout=60, headers=HEADERS)
            r.raise_for_status()
            return r.json()
        req = urllib.request.Request(url, headers=HEADERS, method="GET")
        with urllib.request.urlopen(req, timeout=60) as response:
            return json.loads(response.read().decode("utf-8"))

    def _throttle(self):
        elapsed = time.monotonic() - self._last_call
        if elapsed < MIN_SECONDS_BETWEEN_CALLS:
            time.sleep(MIN_SECONDS_BETWEEN_CALLS - elapsed)
        self._last_call = time.monotonic()

    def _fetch_batch(self, batch: list[tuple[float, float]]) -> None:
        locations = "|".join(f"{lat},{lon}" for lat, lon in batch)
        url = OPENTOPODATA_URL.format(dataset=self.dataset) + "?" + urllib.parse.urlencode(
            {"locations": locations, "interpolation": "bilinear"}
        )

        payload = None
        for attempt in range(4):
            self._throttle()
            try:
                payload = self._http_get(url)
                break
            except Exception as e:  # network, HTTP, JSON — all retryable
                wait = 2.0 * (attempt + 1)
                if self.verbose:
                    print(f"  [warn] elevation request failed ({type(e).__name__}); "
                          f"retrying in {wait:.0f}s", file=sys.stderr)
                time.sleep(wait)

        self.requests_made += 1

        if not payload or payload.get("status") != "OK":
            for key in batch:
                self._cache.setdefault(key, None)
                self.points_missing += 1
            return

        results = payload.get("results", [])
        for key, result in zip(batch, results):
            elevation = result.get("elevation")
            if elevation is None:
                self._cache[key] = None
                self.points_missing += 1
            else:
                self._cache[key] = float(elevation)
                self.points_resolved += 1

        # Short reply: mark whatever did not come back as missing.
        for key in batch[len(results):]:
            self._cache.setdefault(key, None)
            self.points_missing += 1

    # ── public API ────────────────────────────────────────────────────────────

    def prefetch(self, latlons: list[tuple[float, float]]) -> None:
        """Resolve every coordinate not already cached, in batched calls."""
        pending = []
        seen = set()
        for lat, lon in latlons:
            key = self._key(lat, lon)
            if key in self._cache or key in seen:
                continue
            seen.add(key)
            pending.append(key)

        for i in range(0, len(pending), MAX_LOCATIONS_PER_CALL):
            batch = pending[i:i + MAX_LOCATIONS_PER_CALL]
            self._fetch_batch(batch)

    def elevation(self, lat: float, lon: float) -> float | None:
        key = self._key(lat, lon)
        if key not in self._cache:
            self._fetch_batch([key])
        return self._cache.get(key)

    def elevations(self, latlons: list[tuple[float, float]]) -> list[float | None]:
        self.prefetch(latlons)
        return [self._cache.get(self._key(lat, lon)) for lat, lon in latlons]


# ── post-processing ───────────────────────────────────────────────────────────

def fill_gaps(values: list[float | None]) -> list[float]:
    """
    Replace None entries by interpolating between known neighbours. An all-None
    series becomes all zeros, which is the pre-elevation behaviour.
    """
    known = [i for i, v in enumerate(values) if v is not None]
    if not known:
        return [0.0] * len(values)

    out: list[float] = []
    for i, value in enumerate(values):
        if value is not None:
            out.append(float(value))
            continue
        before = [k for k in known if k < i]
        after = [k for k in known if k > i]
        if before and after:
            a, b = before[-1], after[0]
            t = (i - a) / (b - a)
            out.append(float(values[a]) + (float(values[b]) - float(values[a])) * t)
        elif before:
            out.append(float(values[before[-1]]))
        else:
            out.append(float(values[after[0]]))
    return out


def smooth(values: list[float], window: int = 3) -> list[float]:
    """
    Small centred moving average. DEM cells are 10–30 m wide, so neighbouring
    control points can straddle a cell boundary and produce a step that is
    sampling noise rather than real terrain.
    """
    if window < 2 or len(values) < 3:
        return list(values)
    half = window // 2
    out = []
    for i in range(len(values)):
        lo = max(0, i - half)
        hi = min(len(values), i + half + 1)
        window_values = values[lo:hi]
        out.append(sum(window_values) / len(window_values))
    return out


def limit_slope(values: list[float], distances: list[float], max_grade: float) -> list[float]:
    """
    Clamp the rise between consecutive points to `max_grade` (rise over run).

    A DEM occasionally picks up a building, a tree canopy or a bridge deck next
    to a fairway. Real golf holes essentially never exceed ~25% sustained grade,
    so anything steeper is treated as a sampling artefact rather than terrain.
    """
    if len(values) < 2:
        return list(values)
    out = [values[0]]
    for i in range(1, len(values)):
        run = max(0.5, distances[i] - distances[i - 1])
        delta = values[i] - out[-1]
        cap = max_grade * run
        out.append(out[-1] + max(-cap, min(cap, delta)))
    return out


def cleaned_profile(values: list[float | None],
                    distances: list[float],
                    max_grade: float = 0.25,
                    smooth_window: int = 3) -> list[float]:
    """
    Raw DEM samples gap-filled, smoothed and slope-limited, still in absolute
    DEM metres. The first entry is the tee's height above sea level, which the
    course world uses to place holes relative to each other.
    """
    filled = fill_gaps(values)
    smoothed = smooth(filled, smooth_window)
    return limit_slope(smoothed, distances, max_grade)


def relative_profile(profile: list[float]) -> list[float]:
    """A cleaned profile shifted so the first point (the tee) sits at y = 0."""
    base = profile[0] if profile else 0.0
    return [round(v - base, 2) for v in profile]


def polyline_distances(points: list[tuple[float, float]]) -> list[float]:
    """Cumulative along-path distance for a list of (x, z) metres."""
    out = [0.0]
    for a, b in zip(points, points[1:]):
        out.append(out[-1] + math.hypot(b[0] - a[0], b[1] - a[1]))
    return out
