#!/usr/bin/env python3
"""
aerial_dk.py — Denmark's aerial source for osm_aerial.py: the spring
orthophoto in colour and colour infrared (GeoDanmark, orto_foraar) and the
Danish Elevation Model's terrain and surface (DHM/Terræn and DHM/Overflade),
sampled onto the importer's local grid.

Spring photos are flown before the long grass greens up, so mown grass
stands out from the rough in the near infrared; the surface above the
terrain is the height of trees and buildings. Both come from Dataforsyningen
with the same token as the heights (osm_dhm.py). Data owner: Klimadatastyrelsen,
licence CC BY 4.0; see docs/todo/launch_todo.md for the credit.

Every layer is fetched in squares of the UTM 32N grid, each downloaded once
and cached on disk. Another area's source has the same interface: NAME,
DESCRIPTION, in_coverage, available and layers.
"""

import io
import math
import sys
import time
import urllib.parse
import urllib.request
from pathlib import Path

import numpy as np
from PIL import Image

import osm_aerial
import osm_dhm

NAME = "dk"
DESCRIPTION = "the Danish spring orthophoto and elevation model"
WMS_URL = "https://api.dataforsyningen.dk/orto_foraar_DAF"
WCS_URL = osm_dhm.WCS_URL
# Square side in metres, and the pixel the squares are fetched at.
SQUARE_METRES = 500
PIXEL_METRES = 0.5
PHOTO_LAYERS = {"rgb": "orto_foraar", "cir": "orto_foraar_cir"}
HEIGHT_LAYERS = {"terrain": "dhm_terraen", "surface": "dhm_overflade"}
# The local grid's UTM position is worked out exactly at this spacing and
# interpolated between: over a course the projection is that smooth.
LATTICE_METRES = 25.0


def in_coverage(lat: float, lon: float) -> bool:
    return osm_dhm.in_coverage(lat, lon)


def available() -> bool:
    return osm_dhm.read_token() is not None


def _fetch(url: str, query: dict, token: str, is_image) -> bytes | None:
    full = f"{url}?{urllib.parse.urlencode({**query, 'token': token})}"
    for attempt in range(3):
        try:
            with urllib.request.urlopen(full, timeout=180) as response:
                data = response.read()
            if is_image(data):
                return data
            reason = data[:120].decode("utf-8", "replace").strip()
        except Exception as e:  # network or HTTP — retry, then give up
            reason = str(e).replace(token, "<token>")
        print(f"  [warn] aerial square failed ({reason}); {'retrying' if attempt < 2 else 'giving up'}",
              file=sys.stderr)
        time.sleep(1.0 + attempt)
    return None


class _Squares:
    """One layer's squares, decoded on first use; None where none came."""

    def __init__(self, layer: str, kind: str, token: str, cache_dir: Path | None, refresh: bool):
        self.layer, self.kind, self.token, self.refresh = layer, kind, token, refresh
        self.cache_dir = Path(cache_dir) / "aerial_dk" / layer if cache_dir else None
        self.pixels = int(round(SQUARE_METRES / PIXEL_METRES))
        self._squares: dict[tuple[int, int], np.ndarray | None] = {}

    def _download(self, east: int, north: int) -> bytes | None:
        bbox = f"{east},{north - SQUARE_METRES},{east + SQUARE_METRES},{north}"
        if self.kind == "photo":
            return _fetch(WMS_URL, {"service": "WMS", "version": "1.3.0", "request": "GetMap", "layers": self.layer,
                                    "styles": "", "crs": "EPSG:25832", "bbox": bbox, "width": self.pixels,
                                    "height": self.pixels, "format": "image/png"},
                          self.token, lambda d: d[:8] == b"\x89PNG\r\n\x1a\n")
        return _fetch(WCS_URL, {"service": "WCS", "request": "GetCoverage", "version": "1.0.0", "coverage": self.layer,
                                "crs": "EPSG:25832", "format": "GTiff", "interpolation": "bilinear", "bbox": bbox,
                                "width": self.pixels, "height": self.pixels},
                      self.token, lambda d: d[:2] in (b"II", b"MM"))

    def square(self, column: int, row: int) -> np.ndarray | None:
        """The square whose north-west corner is at easting column·side, northing −row·side."""
        key = (column, row)
        if key in self._squares:
            return self._squares[key]
        east, north = column * SQUARE_METRES, -row * SQUARE_METRES
        suffix = "png" if self.kind == "photo" else "tif"
        path = self.cache_dir / f"{east}_{north}.{suffix}" if self.cache_dir else None
        if path is not None and path.exists() and not self.refresh:
            data = path.read_bytes()
        else:
            data = self._download(east, north)
            if data is not None and path is not None:
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(data)
        pixels = None
        if data is not None:
            if self.kind == "photo":
                pixels = np.asarray(Image.open(io.BytesIO(data)).convert("RGB"), dtype=np.float32) / 255.0
            else:
                width, height, values = osm_dhm.read_float_tiff(data)
                pixels = np.frombuffer(values.tobytes(), dtype=np.float32).reshape(height, width)
                pixels = np.where(pixels > osm_dhm.NO_DATA_BELOW, pixels, np.nan)
        self._squares[key] = pixels
        return pixels

    def sample(self, east: np.ndarray, north: np.ndarray) -> np.ndarray:
        """Nearest pixel at each easting/northing; NaN where no square came."""
        px = np.floor(east / PIXEL_METRES).astype(np.int64)
        py = np.floor(-north / PIXEL_METRES).astype(np.int64)
        columns, rows = px // self.pixels, py // self.pixels
        shape = east.shape + ((3,) if self.kind == "photo" else ())
        out = np.full(shape, np.nan, dtype=np.float32)
        for column, row in set(zip(columns.ravel().tolist(), rows.ravel().tolist())):
            square = self.square(column, row)
            if square is None:
                continue
            here = (columns == column) & (rows == row)
            out[here] = square[py[here] % self.pixels, px[here] % self.pixels]
        return out


def _utm_of_grid(rows: int, columns: int, x0: float, z0: float, cell: float, to_latlon) -> tuple[np.ndarray, np.ndarray]:
    """UTM easting and northing of every cell centre of the local grid."""
    width, depth = columns * cell, rows * cell
    lx = np.linspace(x0, x0 + width, max(2, int(math.ceil(width / LATTICE_METRES)) + 1))
    lz = np.linspace(z0, z0 + depth, max(2, int(math.ceil(depth / LATTICE_METRES)) + 1))
    east = np.empty((len(lz), len(lx))); north = np.empty((len(lz), len(lx)))
    for i, z in enumerate(lz):
        for j, x in enumerate(lx):
            east[i, j], north[i, j] = osm_dhm.utm32(*to_latlon(x, z))
    cx = x0 + (np.arange(columns) + 0.5) * cell
    cz = z0 + (np.arange(rows) + 0.5) * cell
    def across(table):
        by_row = np.stack([np.interp(cx, lx, table[i]) for i in range(len(lz))])
        return np.stack([np.interp(cz, lz, by_row[:, j]) for j in range(columns)], axis=1)
    return across(east), across(north)


def layers(to_latlon, box: tuple[float, float, float, float], cell: float,
           cache_dir: Path | None, refresh: bool = False) -> osm_aerial.Layers:
    """The aerial layers over `box` (x0, z0, x1, z1 in local metres, which
    `to_latlon(x, z)` places) on a grid of `cell` metres, each cell the mean
    of the photo's pixels in it."""
    token = osm_dhm.read_token()
    x0, z0, x1, z1 = box
    columns, rows = int(math.ceil((x1 - x0) / cell)), int(math.ceil((z1 - z0) / cell))
    # Sample at every fetched pixel's spacing and average down to the cell.
    sub = max(1, int(round(cell / PIXEL_METRES)))
    east, north = _utm_of_grid(rows * sub, columns * sub, x0, z0, cell / sub, to_latlon)
    def mean(a):
        return a.reshape(rows, sub, columns, sub, *a.shape[2:]).mean(axis=(1, 3))
    got = {name: mean(_Squares(layer, "photo", token, cache_dir, refresh).sample(east, north))
           for name, layer in PHOTO_LAYERS.items()}
    got.update({name: mean(_Squares(layer, "height", token, cache_dir, refresh).sample(east, north))
                for name, layer in HEIGHT_LAYERS.items()})
    return osm_aerial.Layers(x0=x0, z0=z0, cell=cell, rgb=got["rgb"], nir=got["cir"][..., 0], red=got["cir"][..., 1],
                             height=np.clip(got["surface"] - got["terrain"], 0.0, None), source=NAME)
