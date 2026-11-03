#!/usr/bin/env python3
"""
osm_aerial.py — a course's ground cover from aerial data: fairways, bunkers,
ponds and trees, read from an area's photos and heights where OSM maps
none (aerial_dk.py is Denmark's source; another area adds its own with the
same interface and its numbers under the config's `aerial.sources`).

The course teaches the reading its grass: hole lines run down fairways, so
the photo along them is the course's mown grass, in that photo's light. Its
colour and infrared make a signature (a mean and spread, fitted to the
lines' most alike pixels so the carries over rough are left out), and a
hole's fairway is the ground near its line that matches it, grown into
whole shapes its line passes near (mapped lines run straight from tee to
green, cutting a dogleg's corner through the trees). Sand is ground brighter
than nearly all the course's rough and much less green than its grass;
water is ground far darker in the infrared than the grass and not green
(a shadow is darker but still green); a tree stands at each top of what
stands above the terrain, but for buildings' flat roofs. Shapes are traced
on the grid into outlines in the importer's local metres (x east, z south).

Uses numpy and scipy.
"""

import math
from dataclasses import dataclass

import numpy as np
from scipy import ndimage as ndi

import osm_outline

SIGNATURE = ("ndvi", "bright", "sat", "nir", "green", "blue")


@dataclass
class Layers:
    """An area's aerial layers on a local grid: cell (row, column) covers x
    from x0 + column·cell and z from z0 + row·cell. Photo values run 0 to 1,
    NaN where the source has none."""
    x0: float
    z0: float
    cell: float
    rgb: np.ndarray     # (rows, columns, 3)
    nir: np.ndarray     # near infrared
    red: np.ndarray     # red, from the same infrared photo
    height: np.ndarray  # surface above the terrain, metres
    source: str

    @property
    def shape(self):
        return self.nir.shape

    def cell_of(self, x: float, z: float) -> tuple[float, float]:
        """(row, column) of a point, fractional."""
        return (z - self.z0) / self.cell, (x - self.x0) / self.cell


class Cover:
    """What the whole grid shows, worked out once: per-cell signature values
    (raw and smoothed over `rules["smooth_meters"]`), where the ground is,
    the mown grass's signature read along the hole `lines` (tee to pin, local
    metres), None when they read too little; how bright the rough near them
    gets, and the course's ponds, each whole (labels and their count)."""

    def __init__(self, layers: Layers, rules: dict, lines):
        self.layers, self.rules, self.lines = layers, rules, lines
        r, g, b = layers.rgb[..., 0], layers.rgb[..., 1], layers.rgb[..., 2]
        raw = {
            "ndvi": (layers.nir - layers.red) / (layers.nir + layers.red + 1e-3),
            "bright": layers.rgb.mean(axis=-1),
            "sat": layers.rgb.max(axis=-1) - layers.rgb.min(axis=-1),
            "nir": layers.nir, "green": g, "blue": b,
        }
        self.raw = np.stack([raw[k] for k in SIGNATURE], axis=-1)
        size = max(1, int(round(float(rules["smooth_meters"]) / layers.cell)))
        self.smooth = np.stack([ndi.uniform_filter(raw[k], size) for k in SIGNATURE], axis=-1)
        self.ground = layers.height < float(rules["ground_max_height_meters"])
        mown = rules["mown"]
        on_lines = np.zeros(layers.shape, bool)
        for line in lines:
            middle = _trim(line, float(mown["trim_tee_meters"]), float(mown["trim_green_meters"]))
            if len(middle) >= 2:
                window = _window(layers, middle, 2 * layers.cell)
                on_lines[window] |= _distance_to_line(layers, window, middle) <= layers.cell
        self.signature = _signature(self.smooth[on_lines & self.ground], float(mown["core_share"]))
        self.rough_bright, self.ponds, self.pond_count = np.inf, np.zeros(layers.shape, np.int32), 0
        if self.signature is None:
            return
        mean = self.signature[0]
        alike = _spread(self.smooth, self.signature) <= float(mown["max_spread"])
        near = ndi.distance_transform_edt(~on_lines) * layers.cell <= float(mown["corridor_meters"])
        rough = self.raw[..., self.index("bright")][near & self.ground & ~alike]
        rough = rough[np.isfinite(rough)]
        if len(rough):
            self.rough_bright = float(np.percentile(rough, float(rules["sand"]["rough_percentile"])))
        water_rules = rules["water"]
        water = (self.ground & (self.raw[..., self.index("nir")] <= float(water_rules["infrared_share"]) * mean[self.index("nir")])
                 & (self.raw[..., self.index("ndvi")] <= float(water_rules["most_ndvi"])))
        water = ndi.binary_closing(ndi.binary_opening(water, _disk(1)), _disk(1))
        labels, count = ndi.label(water)
        if count:
            sizes = ndi.sum(np.ones_like(labels), labels, np.arange(count + 1)) * layers.cell ** 2
            keep = sizes >= float(water_rules["least_square_meters"])
            keep[0] = False
            self.ponds, self.pond_count = ndi.label(keep[labels])

    def index(self, key: str) -> int:
        return SIGNATURE.index(key)


def _disk(radius_cells: int) -> np.ndarray:
    span = np.arange(-radius_cells, radius_cells + 1)
    return np.add.outer(span ** 2, span ** 2) <= radius_cells ** 2


def _window(layers: Layers, line, pad: float):
    """Rows and columns (slices) of the grid round `line` and `pad` metres, clipped to it."""
    rows, columns = layers.shape
    cells = [layers.cell_of(x, z) for x, z in line]
    reach = pad / layers.cell
    r0 = max(0, int(min(c[0] for c in cells) - reach)); r1 = min(rows, int(max(c[0] for c in cells) + reach) + 1)
    c0 = max(0, int(min(c[1] for c in cells) - reach)); c1 = min(columns, int(max(c[1] for c in cells) + reach) + 1)
    return slice(r0, max(r0, r1)), slice(c0, max(c0, c1))


def _distance_to_line(layers: Layers, window, line) -> np.ndarray:
    """Metres from each cell of `window` to `line`."""
    rs, cs = window
    on = np.zeros((rs.stop - rs.start, cs.stop - cs.start), bool)
    step = 0.5 * layers.cell
    for (xa, za), (xb, zb) in zip(line, line[1:]):
        n = max(1, int(math.ceil(math.dist((xa, za), (xb, zb)) / step)))
        for i in range(n + 1):
            row, col = layers.cell_of(xa + (xb - xa) * i / n, za + (zb - za) * i / n)
            row, col = int(row) - rs.start, int(col) - cs.start
            if 0 <= row < on.shape[0] and 0 <= col < on.shape[1]:
                on[row, col] = True
    if not on.any():
        return np.full(on.shape, np.inf)
    return ndi.distance_transform_edt(~on) * layers.cell


def _trim(line, start: float, end: float):
    """`line` without its first `start` and last `end` metres; [] when nothing is left."""
    length = sum(math.dist(a, b) for a, b in zip(line, line[1:]))
    if length <= start + end:
        return []
    pts = osm_outline.resample(line, 1.0)
    along, kept = 0.0, []
    for i, p in enumerate(pts):
        if i:
            along += math.dist(pts[i - 1], p)
        if start <= along <= length - end:
            kept.append(p)
    return kept


def _signature(samples: np.ndarray, core: float):
    """Mean and inverse covariance of `samples`' most alike `core` share,
    refitted until it settles; None when there are too few."""
    samples = samples[np.all(np.isfinite(samples), axis=1)]
    if len(samples) < 4 * samples.shape[1]:
        return None
    mean, cov = np.median(samples, axis=0), np.cov(samples.T)
    for _ in range(6):
        inverse = np.linalg.pinv(cov)
        spread = np.einsum("ij,jk,ik->i", samples - mean, inverse, samples - mean)
        kept = samples[spread <= np.quantile(spread, core)]
        mean, cov = kept.mean(axis=0), np.cov(kept.T)
    return mean, np.linalg.pinv(cov)


def _spread(values: np.ndarray, signature) -> np.ndarray:
    mean, inverse = signature
    d = values - mean
    out = np.einsum("...j,jk,...k->...", d, inverse, d)
    return np.where(np.isfinite(out), out, np.inf)


def _fill_small_holes(mask: np.ndarray, least_cells: float) -> np.ndarray:
    labels, count = ndi.label(~mask)
    if count == 0:
        return mask
    sizes = ndi.sum(np.ones_like(labels), labels, np.arange(count + 1))
    small = (sizes < least_cells) & (np.arange(count + 1) > 0)
    # Gaps open to the window's edge are outside, not holes.
    for edge in (labels[0], labels[-1], labels[:, 0], labels[:, -1]):
        small[np.unique(edge)] = False
    return mask | small[labels]


def outlines(layers: Layers, window, mask: np.ndarray, tolerance: float):
    """The outer outline of each shape in `mask` (cells of `window`), in local metres."""
    rs, cs = window
    labels, count = ndi.label(mask)
    rings = []
    for label, found in enumerate(ndi.find_objects(labels), start=1):
        if found is None:
            continue
        piece = labels[found] == label
        rows, columns = piece.shape
        for loop in osm_outline.trace_loops(piece.tolist(), columns, rows):
            if osm_outline.polygon_area(loop) >= 0.0:
                continue
            ring = [(layers.x0 + (cs.start + found[1].start + c) * layers.cell,
                     layers.z0 + (rs.start + found[0].start + r) * layers.cell) for c, r in loop]
            ring = osm_outline.simplify(ring, tolerance)
            if len(ring) >= 3:
                rings.append(ring)
    return rings


def hole_cover(cover: Cover, line, rules: dict) -> dict:
    """The fairway, bunker and water outlines of the hole along `line` (tee to
    pin, local metres): {"fairways", "bunkers", "waters"} lists of rings,
    empty when the photo along the line is not mostly the course's grass."""
    layers = cover.layers
    out = {"fairways": [], "bunkers": [], "waters": []}
    mown_rules, sand_rules, water_rules = rules["mown"], rules["sand"], rules["water"]
    reach = float(mown_rules["corridor_meters"])
    window = _window(layers, line, reach + float(water_rules["beyond_corridor_meters"]) + 2 * layers.cell)
    rs, cs = window
    if rs.stop <= rs.start or cs.stop <= cs.start:
        return out
    distance = _distance_to_line(layers, window, line)
    ground = cover.ground[window]
    smooth, raw = cover.smooth[window], cover.raw[window]
    cell_area = layers.cell ** 2

    # The hole's line, short of the tee and the green, must be mostly that grass.
    middle = _trim(line, float(mown_rules["trim_tee_meters"]), float(mown_rules["trim_green_meters"]))
    signature = cover.signature
    if len(middle) < 2 or signature is None:
        return out
    on_middle = _distance_to_line(layers, window, middle) <= layers.cell
    alike = (_spread(smooth, signature) <= float(mown_rules["max_spread"])) & ground
    snap = _disk(max(1, int(round(float(mown_rules["line_reach_meters"]) / layers.cell))))
    near_alike = ndi.binary_dilation(alike, snap)
    if np.mean(near_alike[on_middle]) < float(mown_rules["least_line_share"]):
        return out

    mown = alike & (distance <= reach)
    mown = ndi.binary_opening(mown, _disk(max(1, int(round(float(mown_rules["open_meters"]) / layers.cell)))))
    mown = ndi.binary_closing(mown, _disk(max(1, int(round(float(mown_rules["close_meters"]) / layers.cell)))))
    mown = _fill_small_holes(mown, float(mown_rules["fill_square_meters"]) / cell_area)
    labels, count = ndi.label(mown)
    if count:
        touched = np.unique(labels[ndi.binary_dilation(on_middle, snap) & mown])
        sizes = ndi.sum(np.ones_like(labels), labels, np.arange(count + 1)) * cell_area
        keep = np.zeros(count + 1, bool)
        keep[touched] = True
        keep &= sizes >= float(mown_rules["least_square_meters"])
        keep[0] = False
        out["fairways"] = outlines(layers, window, keep[labels], float(rules["simplify_meters"]))

    mean = signature[0]
    bright, ndvi = raw[..., cover.index("bright")], raw[..., cover.index("ndvi")]

    # Sand: brighter than nearly all the rough and much less green than the
    # grass, in compact shapes cut into it (sunlit ground at a wood's edge is not).
    sand = (ground & (distance <= reach) & (bright >= cover.rough_bright)
            & (ndvi <= mean[cover.index("ndvi")] - float(sand_rules["less_green_by"])))
    sand = ndi.binary_opening(sand, _disk(1))
    labels, count = ndi.label(sand)
    if count:
        sizes = ndi.sum(np.ones_like(labels), labels, np.arange(count + 1)) * cell_area
        thickness = ndi.maximum(ndi.distance_transform_edt(sand), labels, np.arange(count + 1)) * layers.cell
        keep = ((sizes >= float(sand_rules["least_square_meters"])) & (sizes <= float(sand_rules["most_square_meters"]))
                & (thickness >= float(sand_rules["least_half_width_meters"]))
                & (sizes <= float(sand_rules["most_elongation"]) * math.pi * np.maximum(thickness, 1e-6) ** 2))
        beside = ndi.binary_dilation(alike, _disk(max(1, int(round(float(sand_rules["grass_within_meters"]) / layers.cell)))))
        keep[np.setdiff1d(np.arange(count + 1), np.unique(labels[beside & sand]))] = False
        keep[0] = False
        out["bunkers"] = outlines(layers, window, keep[labels], float(rules["simplify_meters"]))

    # The course's ponds that come within reach, each whole, each with the
    # hole whose line passes nearest it.
    near = distance <= reach + float(water_rules["beyond_corridor_meters"])
    ponds = np.unique(cover.ponds[window][near])
    ponds = [p for p in ponds[ponds > 0] if _nearest_hole(cover, p, line)]
    if ponds:
        whole = (slice(0, layers.shape[0]), slice(0, layers.shape[1]))
        out["waters"] = outlines(layers, whole, np.isin(cover.ponds, ponds), float(rules["simplify_meters"]))
    return out


def _nearest_hole(cover: Cover, pond: int, line) -> bool:
    """Whether `line` passes as near pond `pond`'s middle as any of the
    course's hole lines (within a metre, the lines being drawn alike)."""
    rows, columns = np.nonzero(cover.ponds == pond)
    layers = cover.layers
    middle = (layers.x0 + (columns.mean() + 0.5) * layers.cell, layers.z0 + (rows.mean() + 0.5) * layers.cell)
    def away(points):
        return min(osm_outline.segment_distance(middle, a, b) for a, b in zip(points, points[1:]))
    return away(line) <= min(away(other) for other in cover.lines) + 1.0


def trees(cover: Cover, rules: dict) -> list[tuple[float, float, float, float]]:
    """Every tree standing on the grid: (x, z, height, crown radius) in metres,
    one at each top of what stands above the terrain, buildings left out."""
    layers, tree_rules = cover.layers, rules["trees"]
    height = np.nan_to_num(layers.height, nan=0.0)
    least = float(tree_rules["least_height_meters"])
    standing = height >= least
    # A roof is flat or a plane; a crown is rough all over.
    roughness = ndi.uniform_filter(np.abs(ndi.laplace(height)), 3)
    roof = standing & (roughness < float(tree_rules["roof_roughness_meters"]))
    labels, count = ndi.label(roof)
    if count:
        sizes = ndi.sum(np.ones_like(labels), labels, np.arange(count + 1)) * layers.cell ** 2
        big = sizes >= float(tree_rules["least_roof_square_meters"])
        big[0] = False
        roof = ndi.binary_dilation(big[labels], _disk(max(1, int(round(2.0 / layers.cell)))))
    else:
        roof = np.zeros_like(standing)
    canopy = standing & ~roof
    smoothed = ndi.gaussian_filter(height, float(tree_rules["top_smooth_meters"]) / layers.cell)
    span = max(3, int(round(float(tree_rules["least_spacing_meters"]) / layers.cell)) | 1)
    peaks = canopy & (smoothed == ndi.maximum_filter(smoothed, size=span)) & (smoothed >= least)
    # Cells tied for a top are one top.
    labels, count = ndi.label(peaks, structure=np.ones((3, 3)))
    if count == 0:
        return []
    centres = np.round(np.array(ndi.center_of_mass(peaks, labels, np.arange(1, count + 1)))).astype(int)
    top_rows, top_columns = centres[:, 0], centres[:, 1]
    tops = np.zeros_like(peaks)
    tops[top_rows, top_columns] = True
    ids = np.zeros(height.shape, np.int64)
    ids[top_rows, top_columns] = np.arange(1, len(top_rows) + 1)
    # Each crown cell belongs to its nearest top, within the largest crown.
    distance, (near_rows, near_columns) = ndi.distance_transform_edt(~tops, return_indices=True)
    crown_max = float(tree_rules["most_crown_radius_meters"])
    owner = np.where(canopy & (distance * layers.cell <= crown_max), ids[near_rows, near_columns], 0)
    areas = np.bincount(owner.ravel(), minlength=len(top_rows) + 1)[1:] * layers.cell ** 2
    crown_min = float(tree_rules["least_crown_radius_meters"])
    tall = ndi.maximum_filter(height, size=span)
    found = []
    for i, (row, column) in enumerate(zip(top_rows, top_columns)):
        radius = min(crown_max, max(crown_min, math.sqrt(areas[i] / math.pi)))
        found.append((layers.x0 + (column + 0.5) * layers.cell, layers.z0 + (row + 0.5) * layers.cell,
                      float(tall[row, column]), radius))
    return found
