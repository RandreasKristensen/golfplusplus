"""Draws golf++'s procedural images: the rough's grass texture and the backdrop
panorama every course in assets/courses names, as uncompressed 24-bit BMPs.
Courses without a theme of their own below (fresh imports) get the default
parkland one, seeded by their id.

Everything is drawn from code and fixed seeds (no source images), so the
output is the same on every run:

    python tooling/art/make_art.py            # writes into assets/
    python tooling/art/make_art.py --check    # fails if assets/ differs

Panoramas wrap once around the horizon and span BACKDROP_BOTTOM_DEG to
BACKDROP_TOP_DEG from their bottom row to their top row; keep those in step
with assets/shaders/backdrop.frag. Below the horizon they fade into the
backdrop ground colour in src/renderer/renderer.cpp, so the far edge of the
drawn ground meets them without a seam.
"""

from __future__ import annotations

import argparse
import json
import math
import zlib
import random
import struct
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]

BACKDROP_WIDTH = 1024
BACKDROP_HEIGHT = 256
BACKDROP_BOTTOM_DEG = -30.0
BACKDROP_TOP_DEG = 60.0
# renderer.cpp backdrop_ground_color.
BACKDROP_GROUND = (0.10, 0.26, 0.13)

GRASS_SIZE = 64

Color = tuple[float, float, float]


# --- BMP -------------------------------------------------------------------

def bmp_bytes(width: int, height: int, pixels: list[Color]) -> bytes:
    """`pixels` row by row from the bottom up, 0..1 RGB."""
    stride = (width * 3 + 3) & ~3
    rows = []
    for row in range(height):
        out = bytearray()
        for column in range(width):
            r, g, b = pixels[row * width + column]
            out += bytes((_byte(b), _byte(g), _byte(r)))
        out += bytes(stride - len(out))
        rows.append(bytes(out))
    image = b"".join(rows)
    header = struct.pack("<2sIHHI", b"BM", 14 + 40 + len(image), 0, 0, 14 + 40)
    info = struct.pack("<IiiHHIIiiII", 40, width, height, 1, 24, 0, len(image), 2835, 2835, 0, 0)
    return header + info + image


def _byte(value: float) -> int:
    return max(0, min(255, int(round(value * 255.0))))


# --- small maths ------------------------------------------------------------

def mix(a: Color, b: Color, t: float) -> Color:
    return (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t)


def smoothstep(edge0: float, edge1: float, value: float) -> float:
    t = max(0.0, min(1.0, (value - edge0) / (edge1 - edge0)))
    return t * t * (3.0 - 2.0 * t)


def scale(color: Color, factor: float) -> Color:
    return (color[0] * factor, color[1] * factor, color[2] * factor)


class periodic_noise_1d:
    """Smooth value noise that repeats every `period` samples."""

    def __init__(self, rng: random.Random, period: int, cells: int):
        self.period = period
        self.cells = cells
        self.values = [rng.random() for _ in range(cells)]

    def at(self, x: float) -> float:
        u = (x / self.period) * self.cells
        i = math.floor(u)
        t = u - i
        a = self.values[i % self.cells]
        b = self.values[(i + 1) % self.cells]
        return a + (b - a) * t * t * (3.0 - 2.0 * t)


def fbm_1d(rng: random.Random, period: int, base_cells: int, octaves: int) -> list[float]:
    layers = [periodic_noise_1d(rng, period, base_cells * (2 ** o)) for o in range(octaves)]
    out = []
    for x in range(period):
        total = 0.0
        weight = 0.0
        amplitude = 1.0
        for layer in layers:
            total += layer.at(x) * amplitude
            weight += amplitude
            amplitude *= 0.5
        out.append(total / weight)
    # Stretched to the full 0..1 so ridges reach their highest and lowest.
    low, high = min(out), max(out)
    return [(v - low) / max(high - low, 1e-6) for v in out]


class periodic_noise_2d:
    """Value noise repeating every `period` along x, free along y."""

    def __init__(self, rng: random.Random, period: int, cells_x: int, cells_y: int):
        self.period = period
        self.cells_x = cells_x
        self.cells_y = cells_y
        self.values = [[rng.random() for _ in range(cells_x)] for _ in range(cells_y + 1)]

    def at(self, x: float, y: float) -> float:
        u = (x / self.period) * self.cells_x
        v = max(0.0, min(float(self.cells_y) - 0.0001, y * self.cells_y))
        i = math.floor(u)
        j = math.floor(v)
        tu = u - i
        tv = v - j
        tu = tu * tu * (3.0 - 2.0 * tu)
        tv = tv * tv * (3.0 - 2.0 * tv)
        row0 = self.values[j]
        row1 = self.values[j + 1]
        a = row0[i % self.cells_x] + (row0[(i + 1) % self.cells_x] - row0[i % self.cells_x]) * tu
        b = row1[i % self.cells_x] + (row1[(i + 1) % self.cells_x] - row1[i % self.cells_x]) * tu
        return a + (b - a) * tv


# --- panoramas --------------------------------------------------------------

def elevation_of_row(row: int) -> float:
    return BACKDROP_BOTTOM_DEG + (row + 0.5) * (BACKDROP_TOP_DEG - BACKDROP_BOTTOM_DEG) / BACKDROP_HEIGHT


def column_wrap_distance(a: float, b: float) -> float:
    d = abs(a - b) % BACKDROP_WIDTH
    return min(d, BACKDROP_WIDTH - d)


def tree_line(rng: random.Random, kind: str, base: float, spread: float, density: int, mask: list[float]) -> list[float]:
    """Top of the nearest tree line per column, in degrees; -inf where none.
    Crowns are round bumps (broadleaf) or narrow spikes (pine)."""
    tops = [-math.inf] * BACKDROP_WIDTH
    for _ in range(density):
        center = rng.uniform(0, BACKDROP_WIDTH)
        if mask[int(center) % BACKDROP_WIDTH] < 0.5:
            continue
        height = base + rng.uniform(0.0, spread)
        if kind == "pine":
            half = rng.uniform(1.6, 3.2)
            reach = int(half * 2) + 2
            for dx in range(-reach, reach + 1):
                column = int(center + dx) % BACKDROP_WIDTH
                top = height - abs(center - (int(center + dx))) * (height * 0.55) / half
                tops[column] = max(tops[column], top)
        else:
            radius = rng.uniform(3.0, 8.0)
            for dx in range(-int(radius) - 1, int(radius) + 2):
                column = int(center + dx) % BACKDROP_WIDTH
                inside = radius * radius - (center - int(center + dx)) ** 2
                if inside > 0.0:
                    top = height - radius * 0.35 + math.sqrt(inside) * 0.35
                    tops[column] = max(tops[column], top)
    return tops


def panorama(theme: dict) -> list[Color]:
    rng = random.Random(theme["seed"])
    width, height = BACKDROP_WIDTH, BACKDROP_HEIGHT

    # Where the sea shows (1) and land (0), per column.
    sea = [0.0] * width
    for center, half_width in theme.get("sea", []):
        for x in range(width):
            distance = column_wrap_distance(x, center * width)
            sea[x] = max(sea[x], 1.0 - smoothstep(half_width * width * 0.8, half_width * width, distance))
    land = [1.0 - s for s in sea]

    hills_shape = fbm_1d(rng, width, 5, 4)
    hill_low, hill_high = theme["hills"]
    hills = [hill_low + (hill_high - hill_low) * h for h in hills_shape]
    coast_low, coast_high = theme.get("far_coast", (0.0, 0.0))
    coast_shape = fbm_1d(rng, width, 9, 3)
    far_coast = [coast_low + (coast_high - coast_low) * c for c in coast_shape]
    # Hills drop to the far coast over open water.
    hills = [hills[x] * land[x] + far_coast[x] * sea[x] for x in range(width)]

    trees = tree_line(rng, theme["trees"], theme["tree_base"], theme["tree_spread"], theme["tree_count"], land)
    tree_wobble = fbm_1d(rng, width, 40, 2)

    clouds = periodic_noise_2d(rng, width, 7, 3)
    cloud_detail = periodic_noise_2d(rng, width, 23, 7)

    zenith = theme["zenith"]
    horizon = theme["horizon"]
    hill_color = mix(theme["hill"], horizon, 0.3)
    tree_color = theme["tree"]
    sea_color = theme.get("sea_color", (0.2, 0.3, 0.4))
    grain = random.Random(theme["seed"] + 1)

    pixels: list[Color] = []
    for row in range(height):
        elevation = elevation_of_row(row)
        sky_t = smoothstep(0.0, 50.0, elevation) ** 0.7
        for x in range(width):
            sky = mix(horizon, zenith, sky_t)
            if elevation > 0.0:
                fraction = elevation / BACKDROP_TOP_DEG
                density = clouds.at(x, fraction) * 0.7 + cloud_detail.at(x, fraction) * 0.3
                band = smoothstep(2.0, 10.0, elevation) * (1.0 - smoothstep(35.0, 58.0, elevation))
                amount = smoothstep(1.0 - theme["cloudiness"], 1.0 - theme["cloudiness"] + 0.18, density) * band
                cloud = scale(theme["cloud"], 0.88 + 0.12 * density)
                sky = mix(sky, cloud, amount * 0.9)
            color = sky

            if elevation < hills[x]:
                shade = 0.9 + 0.1 * smoothstep(hills[x] - 3.0, hills[x], elevation)
                color = scale(hill_color, shade)
            if elevation < trees[x] + (tree_wobble[x] - 0.5) * 0.4:
                color = scale(tree_color, 0.85 + 0.25 * cloud_detail.at(x * 3.0, 0.5 + elevation / 20.0))
            if elevation < 0.0:
                ground = mix(theme["near_land"], BACKDROP_GROUND, smoothstep(-0.2, -2.5, elevation))
                water = mix(sea_color, BACKDROP_GROUND, smoothstep(-1.2, -2.5, elevation))
                color = mix(ground, water, sea[x])
            jitter = (grain.random() - 0.5) * 0.02
            pixels.append((color[0] + jitter, color[1] + jitter, color[2] + jitter))
    return pixels


# Washed-out, slightly warm colours, as a cheap 1989 camcorder saw them.
# Woods and gentle hills all round; seeded per course.
DEFAULT_THEME = {
    "zenith": (0.38, 0.55, 0.78), "horizon": (0.75, 0.80, 0.83),
    "cloud": (0.95, 0.94, 0.90), "cloudiness": 0.4,
    "hills": (1.0, 5.0), "hill": (0.31, 0.44, 0.34),
    "trees": "broadleaf", "tree_base": 1.6, "tree_spread": 2.0, "tree_count": 560, "tree": (0.13, 0.25, 0.13),
    "near_land": (0.20, 0.33, 0.18),
}

THEMES = {
    # Kalø, Djursland: beech woods and low hills, the bay to one side.
    "kalo_golf_club": {
        "seed": 1101, "zenith": (0.38, 0.55, 0.78), "horizon": (0.74, 0.80, 0.84),
        "cloud": (0.95, 0.94, 0.90), "cloudiness": 0.42,
        "hills": (1.5, 6.5), "hill": (0.30, 0.44, 0.34),
        "trees": "broadleaf", "tree_base": 1.8, "tree_spread": 2.0, "tree_count": 520, "tree": (0.13, 0.25, 0.13),
        "sea": [(0.18, 0.09)], "sea_color": (0.36, 0.48, 0.58), "far_coast": (0.0, 0.9),
        "near_land": (0.20, 0.33, 0.18),
    },
    "kalo_par_3": {
        "seed": 1203, "zenith": (0.40, 0.57, 0.80), "horizon": (0.76, 0.81, 0.84),
        "cloud": (0.96, 0.95, 0.91), "cloudiness": 0.36,
        "hills": (1.2, 5.5), "hill": (0.30, 0.44, 0.34),
        "trees": "broadleaf", "tree_base": 2.0, "tree_spread": 2.2, "tree_count": 600, "tree": (0.12, 0.24, 0.12),
        "sea": [(0.22, 0.07)], "sea_color": (0.36, 0.48, 0.58), "far_coast": (0.0, 0.8),
        "near_land": (0.20, 0.33, 0.18),
    },
    # Helsingør: the sound on one side with the far shore low across it, woods
    # on the other.
    "marienlyst_golfklub": {
        "seed": 2207, "zenith": (0.36, 0.52, 0.76), "horizon": (0.76, 0.80, 0.82),
        "cloud": (0.94, 0.94, 0.92), "cloudiness": 0.5,
        "hills": (0.8, 4.0), "hill": (0.32, 0.43, 0.36),
        "trees": "broadleaf", "tree_base": 1.6, "tree_spread": 1.8, "tree_count": 380, "tree": (0.14, 0.26, 0.15),
        "sea": [(0.62, 0.2)], "sea_color": (0.34, 0.46, 0.56), "far_coast": (0.4, 1.8),
        "near_land": (0.21, 0.33, 0.19),
    },
    # A links by the sea: grey northern sky, open bay, dunes and far low hills,
    # hardly a tree.
    "old_course": {
        "seed": 3301, "zenith": (0.46, 0.55, 0.68), "horizon": (0.78, 0.80, 0.80),
        "cloud": (0.88, 0.88, 0.87), "cloudiness": 0.62,
        "hills": (0.6, 3.2), "hill": (0.40, 0.45, 0.38),
        "trees": "broadleaf", "tree_base": 0.3, "tree_spread": 0.6, "tree_count": 60, "tree": (0.22, 0.29, 0.18),
        "sea": [(0.35, 0.22)], "sea_color": (0.38, 0.47, 0.54), "far_coast": (0.0, 1.4),
        "near_land": (0.30, 0.36, 0.20),
    },
    # Tall pines all round under a warm, hazy southern sky.
    "augusta_national_golf_club": {
        "seed": 4409, "zenith": (0.36, 0.53, 0.80), "horizon": (0.82, 0.83, 0.80),
        "cloud": (0.97, 0.95, 0.90), "cloudiness": 0.3,
        "hills": (1.0, 4.0), "hill": (0.32, 0.42, 0.32),
        "trees": "pine", "tree_base": 3.0, "tree_spread": 3.5, "tree_count": 900, "tree": (0.10, 0.20, 0.11),
        "near_land": (0.20, 0.30, 0.16),
    },
}


# --- grass ------------------------------------------------------------------

def grass_texture() -> list[Color]:
    """Short blades of grass as single-pixel lines over neutral mid grey
    (128 leaves the ground colour alone in terrain.frag). Tiles both ways."""
    rng = random.Random(77)
    size = GRASS_SIZE
    neutral = 128.0 / 255.0
    values = [neutral] * (size * size)
    for _ in range(150):
        x = rng.uniform(0, size)
        y = rng.uniform(0, size)
        angle = math.radians(rng.uniform(-28.0, 28.0))
        length = rng.uniform(3.0, 7.0)
        tone = rng.choice((rng.uniform(0.27, 0.40), rng.uniform(0.58, 0.68)))
        for step in range(int(length * 2) + 1):
            t = step / 2.0
            px = int(x + math.sin(angle) * t) % size
            py = int(y + math.cos(angle) * t) % size
            values[py * size + px] = tone
    return [(v * 0.96, v, v * 0.94) for v in values]


# --- main -------------------------------------------------------------------

def course_theme(course_id: str) -> dict:
    if course_id in THEMES:
        return THEMES[course_id]
    return {**DEFAULT_THEME, "seed": zlib.crc32(course_id.encode("utf-8"))}


def outputs() -> dict[Path, bytes]:
    assets = REPO / "assets"
    files = {assets / "textures" / "rough_grass.bmp": bmp_bytes(GRASS_SIZE, GRASS_SIZE, grass_texture())}
    for course_file in sorted((assets / "courses").glob("*.json")):
        course = json.loads(course_file.read_text(encoding="utf-8"))
        backdrop = assets / course["backdrop"]
        if backdrop not in files:
            files[backdrop] = bmp_bytes(BACKDROP_WIDTH, BACKDROP_HEIGHT, panorama(course_theme(course["id"])))
    return files


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--check", action="store_true", help="fail if any file in assets/ differs")
    args = parser.parse_args()
    stale = []
    for path, data in outputs().items():
        if args.check:
            if not path.exists() or path.read_bytes() != data:
                stale.append(path)
            continue
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        print(f"wrote {path.relative_to(REPO)}")
    for path in stale:
        print(f"differs: {path.relative_to(REPO)}")
    return 1 if stale else 0


if __name__ == "__main__":
    sys.exit(main())
