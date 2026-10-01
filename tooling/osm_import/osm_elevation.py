#!/usr/bin/env python3
"""
osm_elevation.py — Sample real terrain elevation for imported OSM courses.

OpenStreetMap does not carry usable height data for golf features, so the
importer reads a digital elevation model (DEM) instead and bakes the result
into each hole's spline and the course world's ground grid.

The DEM is the Terrarium elevation tiles from the AWS Open Data "Terrain
Tiles" set (https://registry.opendata.aws/terrain-tiles/): 256x256 PNG map
tiles whose pixels encode height. They are free, need no account and have no
rate limit, and each tile is downloaded once and cached on disk, so a
re-import, a moved hole or a new ground grid costs no network at all. The
sources behind them are SRTM (~30 m) worldwide, USGS NED (10 m or better) in
the USA and national lidar in some European countries; see the registry page
for the full list and its attribution requirements.

Resolution honesty: a 10–30 m DEM reproduces the landform of a hole — uphill
tee shots, valleys, plateau greens, the slope of a fairway — but it cannot see
green contours, bunker lips, or mounding. Those stay a hole-editor job.

Only the standard library is used (PNG decoding is zlib plus row filters).
"""

import math
import struct
import sys
import time
import urllib.request
import zlib
from pathlib import Path

TILE_URL = "https://s3.amazonaws.com/elevation-tiles-prod/terrarium/{z}/{x}/{y}.png"
HEADERS = {"User-Agent": "osm_golf_convert/2.0 (golf course converter)"}
DATASET = "terrarium"
TILE_SIZE = 256
# ~5–8 m per pixel at golf-course latitudes: finer than any source DEM outside
# the USA, so nothing is lost, and a course needs only a handful of tiles.
DEFAULT_ZOOM = 14


# ── tiles ─────────────────────────────────────────────────────────────────────

def tile_pixel(lat: float, lon: float, zoom: int) -> tuple[float, float]:
    """Web Mercator position of a coordinate, in pixels of the whole map at `zoom`."""
    scale = TILE_SIZE * (2 ** zoom)
    x = (lon + 180.0) / 360.0 * scale
    lat_rad = math.radians(max(-85.0511, min(85.0511, lat)))
    y = (1.0 - math.asinh(math.tan(lat_rad)) / math.pi) / 2.0 * scale
    return x, y


def decode_png_rgb(data: bytes) -> tuple[int, int, list[bytes]]:
    """
    Width, height and raw RGB rows of an 8-bit, non-interlaced RGB or RGBA
    PNG (the only kinds the tile set uses). Alpha is dropped.
    """
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")
    pos = 8
    width = height = channels = 0
    compressed = bytearray()
    while pos < len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if kind == b"IHDR":
            width, height, depth, color, _comp, _filt, interlace = struct.unpack(">IIBBBBB", body)
            if depth != 8 or color not in (2, 6) or interlace != 0:
                raise ValueError("unsupported PNG format")
            channels = 3 if color == 2 else 4
        elif kind == b"IDAT":
            compressed += body
        elif kind == b"IEND":
            break

    raw = zlib.decompress(bytes(compressed))
    stride = width * channels
    rows: list[bytes] = []
    previous = bytearray(stride)
    for row in range(height):
        start = row * (stride + 1)
        kind = raw[start]
        line = bytearray(raw[start + 1:start + 1 + stride])
        for i in range(stride):
            left = line[i - channels] if i >= channels else 0
            up = previous[i]
            if kind == 1:
                line[i] = (line[i] + left) & 0xFF
            elif kind == 2:
                line[i] = (line[i] + up) & 0xFF
            elif kind == 3:
                line[i] = (line[i] + ((left + up) >> 1)) & 0xFF
            elif kind == 4:
                up_left = previous[i - channels] if i >= channels else 0
                p = left + up - up_left
                pa, pb, pc = abs(p - left), abs(p - up), abs(p - up_left)
                predictor = left if pa <= pb and pa <= pc else (up if pb <= pc else up_left)
                line[i] = (line[i] + predictor) & 0xFF
        previous = line
        if channels == 4:
            line = bytearray(b for i, b in enumerate(line) if i % 4 != 3)
        rows.append(bytes(line))
    return width, height, rows


def terrarium_heights(rows: list[bytes]) -> list[list[float]]:
    """Metres above sea level per pixel: (R * 256 + G + B / 256) - 32768."""
    return [[row[i] * 256.0 + row[i + 1] + row[i + 2] / 256.0 - 32768.0
             for i in range(0, len(row), 3)] for row in rows]


class ElevationSampler:
    """
    Bilinear heights from Terrarium tiles. Tiles are fetched on first use and
    cached under `cache_dir` (when given), so every later lookup in the same
    area, in any run, is local.
    """

    def __init__(self, cache_dir: Path | None = None, zoom: int = DEFAULT_ZOOM,
                 refresh: bool = False, verbose: bool = True):
        self.dataset = DATASET
        self.zoom = zoom
        self.cache_dir = Path(cache_dir) / DATASET / str(zoom) if cache_dir else None
        self.refresh = refresh
        self.verbose = verbose
        self.tiles_downloaded = 0
        self.tiles_from_cache = 0
        self._tiles: dict[tuple[int, int], list[list[float]] | None] = {}

    def _download(self, x: int, y: int) -> bytes | None:
        url = TILE_URL.format(z=self.zoom, x=x, y=y)
        for attempt in range(3):
            try:
                request = urllib.request.Request(url, headers=HEADERS)
                with urllib.request.urlopen(request, timeout=30) as response:
                    return response.read()
            except Exception as e:  # network or HTTP — retry, then give up
                if self.verbose:
                    print(f"  [warn] elevation tile {self.zoom}/{x}/{y} failed ({e}); "
                          f"{'retrying' if attempt < 2 else 'giving up'}", file=sys.stderr)
                time.sleep(1.0 + attempt)
        return None

    def _tile(self, x: int, y: int) -> list[list[float]] | None:
        key = (x, y)
        if key in self._tiles:
            return self._tiles[key]
        path = self.cache_dir / str(x) / f"{y}.png" if self.cache_dir else None
        data = None
        if path is not None and path.exists() and not self.refresh:
            data = path.read_bytes()
            self.tiles_from_cache += 1
        else:
            data = self._download(x, y)
            if data is not None:
                self.tiles_downloaded += 1
                if path is not None:
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_bytes(data)
        heights = terrarium_heights(decode_png_rgb(data)[2]) if data is not None else None
        self._tiles[key] = heights
        return heights

    def _pixel_height(self, px: int, py: int) -> float | None:
        tile = self._tile(px // TILE_SIZE, py // TILE_SIZE)
        return None if tile is None else tile[py % TILE_SIZE][px % TILE_SIZE]

    def elevation(self, lat: float, lon: float) -> float | None:
        # Pixel centres sit at +0.5, so shift before splitting into cell + fraction.
        fx, fy = tile_pixel(lat, lon, self.zoom)
        fx -= 0.5
        fy -= 0.5
        x0, y0 = math.floor(fx), math.floor(fy)
        tx, ty = fx - x0, fy - y0
        corners = [self._pixel_height(x0, y0), self._pixel_height(x0 + 1, y0),
                   self._pixel_height(x0, y0 + 1), self._pixel_height(x0 + 1, y0 + 1)]
        if any(c is None for c in corners):
            return None
        a, b, c, d = corners
        top = a + (b - a) * tx
        bottom = c + (d - c) * tx
        return top + (bottom - top) * ty

    def elevations(self, latlons: list[tuple[float, float]]) -> list[float | None]:
        return [self.elevation(lat, lon) for lat, lon in latlons]


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
