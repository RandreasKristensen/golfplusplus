#!/usr/bin/env python3
"""
osm_dhm.py — Heights from the Danish Elevation Model (Danmarks Højdemodel).

DHM/Terræn is national lidar: a 0.4 m bare-earth grid (no trees, no
buildings) covering all of Denmark. It sees what the Terrarium tiles cannot:
green contours, bunker lips, mounds and swales. The importer uses it for
every course it covers (osm_golf_convert._make_elevation_sampler), and
Terrarium (osm_elevation.py) elsewhere.

It is served by Dataforsyningen's WCS, which needs a free token: put it in
dataforsyning_token.txt next to this file (gitignored) or the
DATAFORSYNINGEN_TOKEN environment variable. Data owner: Klimadatastyrelsen,
licence CC BY 4.0; see docs/steam_todo.md for the credit.

The model is fetched in 1 km squares of the UTM 32N grid at `pixel_size`
metres (the WCS resamples bilinearly), each downloaded once and cached on
disk, so re-imports cost no network. A 1 m square is 4 MB and takes a few
seconds. Only the standard library is used: the WCS answers with an
uncompressed float32 GeoTIFF, read here directly.
"""

import math
import os
import struct
import sys
import time
import urllib.parse
import urllib.request
from array import array
from pathlib import Path

DATASET = "dhm"
WCS_URL = "https://api.dataforsyningen.dk/dhm_wcs_DAF"
COVERAGE = "dhm_terraen"
TOKEN_FILE = Path(__file__).with_name("dataforsyning_token.txt")
TOKEN_ENV = "DATAFORSYNINGEN_TOKEN"
TILE_METRES = 1000
# Where the model has data (its WCS envelope), in degrees.
COVERAGE_LAT = (54.43, 57.77)
COVERAGE_LON = (8.00, 15.60)
# Heights at or below this are no data (the sea and outside Denmark read 0
# or a large negative fill; real Danish land is never this low).
NO_DATA_BELOW = -50.0


# ── UTM 32N (ETRS89 / GRS80), Krüger series ───────────────────────────────────

_A = 6378137.0
_F = 1.0 / 298.257222101
_K0 = 0.9996
_FALSE_EASTING = 500000.0
_CENTRAL_MERIDIAN = math.radians(9.0)
_N = _F / (2.0 - _F)
_RECTIFYING_RADIUS = _A / (1.0 + _N) * (1.0 + _N ** 2 / 4.0 + _N ** 4 / 64.0)
_ALPHA = (
    _N / 2.0 - 2.0 * _N ** 2 / 3.0 + 5.0 * _N ** 3 / 16.0 + 41.0 * _N ** 4 / 180.0,
    13.0 * _N ** 2 / 48.0 - 3.0 * _N ** 3 / 5.0 + 557.0 * _N ** 4 / 1440.0,
    61.0 * _N ** 3 / 240.0 - 103.0 * _N ** 4 / 140.0,
    49561.0 * _N ** 4 / 161280.0,
)
_ECCENTRICITY_TERM = 2.0 * math.sqrt(_N) / (1.0 + _N)


def utm32(lat: float, lon: float) -> tuple[float, float]:
    """Easting and northing (EPSG:25832) of a latitude/longitude, to the millimetre."""
    phi = math.radians(lat)
    d_lambda = math.radians(lon) - _CENTRAL_MERIDIAN
    sin_phi = math.sin(phi)
    t = math.sinh(math.atanh(sin_phi) - _ECCENTRICITY_TERM * math.atanh(_ECCENTRICITY_TERM * sin_phi))
    xi = math.atan2(t, math.cos(d_lambda))
    eta = math.atanh(math.sin(d_lambda) / math.sqrt(1.0 + t * t))
    easting = eta
    northing = xi
    for j, alpha in enumerate(_ALPHA, start=1):
        easting += alpha * math.cos(2.0 * j * xi) * math.sinh(2.0 * j * eta)
        northing += alpha * math.sin(2.0 * j * xi) * math.cosh(2.0 * j * eta)
    return _FALSE_EASTING + _K0 * _RECTIFYING_RADIUS * easting, _K0 * _RECTIFYING_RADIUS * northing


# ── GeoTIFF ───────────────────────────────────────────────────────────────────

def read_float_tiff(data: bytes) -> tuple[int, int, array]:
    """
    Width, height and row-major float32 pixels of an uncompressed, striped,
    single-band float32 TIFF (what the WCS sends).
    """
    order = {b"II": "<", b"MM": ">"}.get(data[:2])
    if order is None or struct.unpack(order + "H", data[2:4])[0] != 42:
        raise ValueError("not a TIFF")
    ifd = struct.unpack(order + "I", data[4:8])[0]
    count = struct.unpack(order + "H", data[ifd:ifd + 2])[0]
    sizes = {3: ("H", 2), 4: ("I", 4)}
    tags: dict[int, list[int]] = {}
    for i in range(count):
        entry = data[ifd + 2 + i * 12:ifd + 14 + i * 12]
        tag, kind, n = struct.unpack(order + "HHI", entry[:8])
        if kind not in sizes:
            continue
        code, size = sizes[kind]
        raw = entry[8:12] if n * size <= 4 else data[struct.unpack(order + "I", entry[8:12])[0]:][:n * size]
        tags[tag] = list(struct.unpack(order + code * n, raw[:n * size]))
    width, height = tags[256][0], tags[257][0]
    if tags.get(258, [0])[0] != 32 or tags.get(259, [1])[0] != 1 or tags.get(339, [1])[0] != 3 \
            or tags.get(277, [1])[0] != 1 or 322 in tags:
        raise ValueError("not an uncompressed, striped, single-band float32 TIFF")
    pixels = array("f")
    for offset, length in zip(tags[273], tags[279]):
        pixels.frombytes(data[offset:offset + length])
    if (order == "<") != (sys.byteorder == "little"):
        pixels.byteswap()
    if len(pixels) != width * height:
        raise ValueError("TIFF strips do not fill the image")
    return width, height, pixels


# ── sampler ───────────────────────────────────────────────────────────────────

def read_token() -> str | None:
    token = os.environ.get(TOKEN_ENV, "").strip()
    if not token and TOKEN_FILE.exists():
        token = TOKEN_FILE.read_text(encoding="utf-8").strip()
    return token or None


def in_coverage(lat: float, lon: float) -> bool:
    return COVERAGE_LAT[0] <= lat <= COVERAGE_LAT[1] and COVERAGE_LON[0] <= lon <= COVERAGE_LON[1]


class DhmSampler:
    """
    Bilinear heights from DHM/Terræn, in metres above sea level; None where
    the model has no data. Same interface as osm_elevation.ElevationSampler.
    """

    def __init__(self, token: str, cache_dir: Path | None = None, pixel_size: float = 1.0,
                 refresh: bool = False, verbose: bool = True):
        self.dataset = DATASET
        self.description = f"the Danish Elevation Model at {pixel_size:g} m"
        self.token = token
        self.pixel_size = pixel_size
        self.tile_pixels = int(round(TILE_METRES / pixel_size))
        self.cache_dir = Path(cache_dir) / DATASET / f"{pixel_size:g}m" if cache_dir else None
        self.refresh = refresh
        self.verbose = verbose
        self.tiles_downloaded = 0
        self.tiles_from_cache = 0
        self._tiles: dict[tuple[int, int], array | None] = {}

    def _download(self, east: int, north: int) -> bytes | None:
        query = urllib.parse.urlencode({
            "service": "WCS", "request": "GetCoverage", "version": "1.0.0", "coverage": COVERAGE,
            "crs": "EPSG:25832", "format": "GTiff", "interpolation": "bilinear",
            "bbox": f"{east},{north - TILE_METRES},{east + TILE_METRES},{north}",
            "width": self.tile_pixels, "height": self.tile_pixels, "token": self.token})
        for attempt in range(3):
            try:
                with urllib.request.urlopen(f"{WCS_URL}?{query}", timeout=120) as response:
                    data = response.read()
                if data[:2] in (b"II", b"MM"):
                    return data
                reason = data[:120].decode("utf-8", "replace").strip()
            except Exception as e:  # network or HTTP — retry, then give up
                reason = str(e).replace(self.token, "<token>")
            if self.verbose:
                print(f"  [warn] DHM square {east},{north} failed ({reason}); "
                      f"{'retrying' if attempt < 2 else 'giving up'}", file=sys.stderr)
            time.sleep(1.0 + attempt)
        return None

    def _tile(self, column: int, row: int) -> array | None:
        """The square whose north-west corner is at (column, -row) km."""
        key = (column, row)
        if key in self._tiles:
            return self._tiles[key]
        east, north = column * TILE_METRES, -row * TILE_METRES
        path = self.cache_dir / f"{east}_{north}.tif" if self.cache_dir else None
        data = None
        if path is not None and path.exists() and not self.refresh:
            data = path.read_bytes()
            self.tiles_from_cache += 1
        else:
            data = self._download(east, north)
            if data is not None:
                self.tiles_downloaded += 1
                if path is not None:
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_bytes(data)
        pixels = None
        if data is not None:
            width, height, pixels = read_float_tiff(data)
            if width != self.tile_pixels or height != self.tile_pixels:
                raise ValueError(f"DHM square {east},{north} is {width}x{height}, not {self.tile_pixels}")
        self._tiles[key] = pixels
        return pixels

    def _pixel_height(self, x: int, y: int) -> float | None:
        """Pixel `x` east of easting 0, `y` south of northing 0."""
        tile = self._tile(x // self.tile_pixels, y // self.tile_pixels)
        if tile is None:
            return None
        height = tile[(y % self.tile_pixels) * self.tile_pixels + x % self.tile_pixels]
        return height if height > NO_DATA_BELOW else None

    def elevation(self, lat: float, lon: float) -> float | None:
        if not in_coverage(lat, lon):
            return None
        east, north = utm32(lat, lon)
        # Pixel centres sit at +0.5, so shift before splitting into cell + fraction.
        fx = east / self.pixel_size - 0.5
        fy = -north / self.pixel_size - 0.5
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
