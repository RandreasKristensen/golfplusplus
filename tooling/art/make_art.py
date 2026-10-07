"""Draws golf++'s procedural images: the rough's grass texture, the fences'
net and the two backdrop panoramas every course in assets/courses names (a sky, and the land
in front of it), as uncompressed BMPs. Courses without a theme of their own
below (fresh imports) get the default parkland one, seeded by their id.

Everything is drawn from code and fixed seeds (no source images), so the
output is the same on every run:

    python tooling/art/make_art.py            # writes into assets/
    python tooling/art/make_art.py --check    # fails if assets/ differs

Panoramas wrap once around the horizon and span BACKDROP_BOTTOM_DEG to
BACKDROP_TOP_DEG from their bottom row to their top row; keep those and
MIN_LAND_VISIBILITY in step with assets/shaders/backdrop.frag.

The sky (24-bit) fades to the course's `haze_color` at the horizon. The land
(32-bit with alpha) is the far ground, hills and treelines in their own
colours; its alpha is how much of each shows through the haze (0 where the sky
shows), so the game can change the haze colour, and the sky, with the time of
day. The further back something sits the hazier it is: the ground towards the
horizon, then each treeline band, then the hills. Below the horizon the land
fades into the backdrop ground colour in src/renderer/renderer.cpp at
FAR_GROUND_HAZE, which the renderer reads back from the bottom row and fades
the drawn ground to, so the far edge of the drawn ground meets it without a
seam.
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
# Land never shows less than this through the haze, so the shader can tell
# hazy land from sky.
MIN_LAND_VISIBILITY = 0.15
# Haze, 0..1, of the ground under the drawn ground's far edge, then of the
# sea at the horizon (the land there is as hazy as the nearest treeline on it),
# each treeline band from the back and the hills.
FAR_GROUND_HAZE = 0.4
SEA_HORIZON_HAZE = 0.8
# Treeline bands from the back: (crown size, share of the theme's tree count,
# haze, how far up the hills they stand). Further lines are smaller, hazier
# and higher up the far hills, so each shows over the one in front.
TREE_BANDS = ((0.45, 0.9, 0.72, 0.85), (0.7, 0.8, 0.62, 0.45), (1.0, 0.65, 0.5, 0.0))
HILL_HAZE = 0.78

GRASS_SIZE = 64

Color = tuple[float, float, float]


# --- BMP -------------------------------------------------------------------

def bmp_bytes(width: int, height: int, pixels: list[Color]) -> bytes:
    """24-bit, `pixels` row by row from the bottom up, 0..1 RGB."""
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


def bmp_bytes_with_alpha(width: int, height: int, pixels: list[tuple[float, float, float, float]]) -> bytes:
    """32-bit with straight alpha (BITMAPV4HEADER, BI_BITFIELDS with an alpha
    mask, as src/renderer/bmp_image.cpp reads it), `pixels` row by row from
    the bottom up, 0..1 RGBA."""
    image = b"".join(bytes((_byte(b), _byte(g), _byte(r), _byte(a))) for r, g, b, a in pixels)
    info_size = 108
    header = struct.pack("<2sIHHI", b"BM", 14 + info_size + len(image), 0, 0, 14 + info_size)
    info = struct.pack("<IiiHHIIiiII", info_size, width, height, 1, 32, 3, len(image), 2835, 2835, 0, 0)
    masks = struct.pack("<IIII", 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000)
    # sRGB colour space; its end points and gammas are unused.
    colour_space = struct.pack("<I", 0x73524742) + bytes(36 + 12)
    return header + info + masks + colour_space + image


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


def tree_line(rng: random.Random, kind: str, base: float, spread: float, density: int, mask: list[float],
              size: float) -> list[float]:
    """Top of one tree line per column, in degrees; -inf where none. Crowns are
    round bumps (broadleaf) or narrow spikes (pine), `size` times as big as
    the nearest line's (further lines look smaller)."""
    tops = [-math.inf] * BACKDROP_WIDTH
    for _ in range(density):
        center = rng.uniform(0, BACKDROP_WIDTH)
        if mask[int(center) % BACKDROP_WIDTH] < 0.5:
            continue
        height = (base + rng.uniform(0.0, spread)) * size
        if kind == "pine":
            half = rng.uniform(1.6, 3.2) * size
            reach = int(half * 2) + 2
            for dx in range(-reach, reach + 1):
                column = int(center + dx) % BACKDROP_WIDTH
                top = height - abs(center - (int(center + dx))) * (height * 0.55) / half
                tops[column] = max(tops[column], top)
        else:
            radius = rng.uniform(3.0, 8.0) * size
            for dx in range(-int(radius) - 1, int(radius) + 2):
                column = int(center + dx) % BACKDROP_WIDTH
                inside = radius * radius - (center - int(center + dx)) ** 2
                if inside > 0.0:
                    top = height - radius * 0.35 + math.sqrt(inside) * 0.35
                    tops[column] = max(tops[column], top)
    return tops


def sea_columns(theme: dict) -> list[float]:
    """Where the sea shows (1) and land (0), per column."""
    sea = [0.0] * BACKDROP_WIDTH
    for center, half_width in theme.get("sea", []):
        for x in range(BACKDROP_WIDTH):
            distance = column_wrap_distance(x, center * BACKDROP_WIDTH)
            sea[x] = max(sea[x], 1.0 - smoothstep(half_width * BACKDROP_WIDTH * 0.8, half_width * BACKDROP_WIDTH, distance))
    return sea


def sky_panorama(theme: dict) -> list[Color]:
    """Horizon haze up to the zenith, with clouds that thin and haze over
    towards the horizon."""
    rng = random.Random(theme["seed"] + 2)
    width, height = BACKDROP_WIDTH, BACKDROP_HEIGHT
    clouds = periodic_noise_2d(rng, width, 7, 3)
    cloud_detail = periodic_noise_2d(rng, width, 23, 7)
    zenith = theme["zenith"]
    haze = theme["haze_color"]
    grain = random.Random(theme["seed"] + 3)

    pixels: list[Color] = []
    for row in range(height):
        elevation = elevation_of_row(row)
        sky_t = smoothstep(0.0, 50.0, elevation) ** 0.7
        far = 1.0 - smoothstep(2.0, 16.0, elevation)
        for x in range(width):
            sky = mix(haze, zenith, sky_t)
            if elevation > 0.0:
                fraction = elevation / BACKDROP_TOP_DEG
                density = clouds.at(x, fraction) * 0.7 + cloud_detail.at(x, fraction) * 0.3
                band = smoothstep(2.0, 10.0, elevation) * (1.0 - smoothstep(35.0, 58.0, elevation))
                amount = smoothstep(1.0 - theme["cloudiness"], 1.0 - theme["cloudiness"] + 0.18, density) * band
                cloud = mix(scale(theme["cloud"], 0.88 + 0.12 * density), haze, far * 0.6)
                sky = mix(sky, cloud, amount * 0.9)
            jitter = (grain.random() - 0.5) * 0.02
            pixels.append((sky[0] + jitter, sky[1] + jitter, sky[2] + jitter))
    return pixels


def land_panorama(theme: dict) -> list[tuple[float, float, float, float]]:
    """Far ground, hills and treelines in their own colours, alpha how much of
    each shows through the haze. Where the sky shows (alpha 0) the colour is
    the land's just below, so filtering never bleeds anything else in."""
    rng = random.Random(theme["seed"])
    width, height = BACKDROP_WIDTH, BACKDROP_HEIGHT

    sea = sea_columns(theme)
    land = [1.0 - s for s in sea]

    hills_shape = fbm_1d(rng, width, 5, 4)
    hill_low, hill_high = theme["hills"]
    hills = [hill_low + (hill_high - hill_low) * h for h in hills_shape]
    coast_low, coast_high = theme.get("far_coast", (0.0, 0.0))
    coast_shape = fbm_1d(rng, width, 9, 3)
    far_coast = [coast_low + (coast_high - coast_low) * c for c in coast_shape]
    # Hills drop to the far coast over open water.
    hills = [hills[x] * land[x] + far_coast[x] * sea[x] for x in range(width)]

    # From the back, so nearer lines cover further ones.
    bands = []
    for size, share, haze, lift in TREE_BANDS:
        tops = tree_line(rng, theme["trees"], theme["tree_base"], theme["tree_spread"],
                         int(theme["tree_count"] * share), land, size)
        wobble = fbm_1d(rng, width, 40, 2)
        bands.append(([tops[x] + hills[x] * lift + (wobble[x] - 0.5) * 0.4 * size for x in range(width)], haze))
    texture = periodic_noise_2d(rng, width, 23, 7)

    hill_color = theme["hill"]
    tree_color = theme["tree"]
    sea_color = theme.get("sea_color", (0.2, 0.3, 0.4))
    grain = random.Random(theme["seed"] + 1)
    below = [BACKDROP_GROUND] * width

    pixels: list[tuple[float, float, float, float]] = []
    for row in range(height):
        elevation = elevation_of_row(row)
        for x in range(width):
            color = None
            haze = 0.0
            if elevation < hills[x]:
                shade = 0.9 + 0.1 * smoothstep(hills[x] - 3.0, hills[x], elevation)
                color = scale(hill_color, shade)
                haze = HILL_HAZE
            for tops, band_haze in bands:
                if elevation < tops[x]:
                    color = scale(tree_color, 0.85 + 0.25 * texture.at(x * 3.0, 0.5 + elevation / 20.0))
                    haze = band_haze
            if elevation < 0.0:
                ground = mix(theme["near_land"], BACKDROP_GROUND, smoothstep(-0.2, -2.5, elevation))
                water = mix(sea_color, BACKDROP_GROUND, smoothstep(-1.2, -2.5, elevation))
                color = mix(ground, water, sea[x])
                horizon_haze = SEA_HORIZON_HAZE * sea[x] + TREE_BANDS[-1][2] * land[x]
                haze = FAR_GROUND_HAZE + (horizon_haze - FAR_GROUND_HAZE) * smoothstep(-2.5, 0.0, elevation)
            if color is None:
                pixels.append((*below[x], 0.0))
                continue
            jitter = (grain.random() - 0.5) * 0.02
            color = (color[0] + jitter, color[1] + jitter, color[2] + jitter)
            below[x] = color
            pixels.append((*color, max(MIN_LAND_VISIBILITY, 1.0 - haze)))
    return pixels


# Washed-out, slightly warm colours, as a cheap 1989 camcorder saw them. The
# horizon is each course's `haze_color`. Woods and gentle hills all round;
# seeded per course.
DEFAULT_THEME = {
    "zenith": (0.38, 0.55, 0.78), "cloud": (0.95, 0.94, 0.90), "cloudiness": 0.4,
    "hills": (1.0, 5.0), "hill": (0.31, 0.44, 0.34),
    "trees": "broadleaf", "tree_base": 1.6, "tree_spread": 2.0, "tree_count": 560, "tree": (0.13, 0.25, 0.13),
    "near_land": (0.20, 0.33, 0.18),
}

THEMES = {
    # Kalø, Djursland: beech woods and low hills, the bay to one side.
    "kalo_golf_club": {
        "seed": 1101, "zenith": (0.38, 0.55, 0.78), "cloud": (0.95, 0.94, 0.90), "cloudiness": 0.42,
        "hills": (1.5, 6.5), "hill": (0.30, 0.44, 0.34),
        "trees": "broadleaf", "tree_base": 1.8, "tree_spread": 2.0, "tree_count": 520, "tree": (0.13, 0.25, 0.13),
        "sea": [(0.18, 0.09)], "sea_color": (0.36, 0.48, 0.58), "far_coast": (0.0, 0.9),
        "near_land": (0.20, 0.33, 0.18),
    },
    "kalo_par_3": {
        "seed": 1203, "zenith": (0.40, 0.57, 0.80), "cloud": (0.96, 0.95, 0.91), "cloudiness": 0.36,
        "hills": (1.2, 5.5), "hill": (0.30, 0.44, 0.34),
        "trees": "broadleaf", "tree_base": 2.0, "tree_spread": 2.2, "tree_count": 600, "tree": (0.12, 0.24, 0.12),
        "sea": [(0.22, 0.07)], "sea_color": (0.36, 0.48, 0.58), "far_coast": (0.0, 0.8),
        "near_land": (0.20, 0.33, 0.18),
    },
    # Himmerland: heath and pine plantations on rolling inland hills, a lake
    # to one side.
    "himmerland_new_course": {
        "seed": 3307, "zenith": (0.40, 0.56, 0.78), "cloud": (0.95, 0.94, 0.90), "cloudiness": 0.45,
        "hills": (1.4, 6.0), "hill": (0.33, 0.42, 0.33),
        "trees": "pine", "tree_base": 2.2, "tree_spread": 2.6, "tree_count": 700, "tree": (0.11, 0.21, 0.12),
        "sea": [(0.28, 0.06)], "sea_color": (0.34, 0.45, 0.52), "far_coast": (0.0, 0.5),
        "near_land": (0.22, 0.32, 0.18),
    },
    "himmerland_par_3": {
        "seed": 3311, "zenith": (0.42, 0.57, 0.79), "cloud": (0.96, 0.95, 0.91), "cloudiness": 0.4,
        "hills": (1.2, 5.0), "hill": (0.33, 0.42, 0.33),
        "trees": "pine", "tree_base": 2.4, "tree_spread": 2.6, "tree_count": 760, "tree": (0.11, 0.21, 0.12),
        "sea": [(0.18, 0.10)], "sea_color": (0.34, 0.45, 0.52), "far_coast": (0.0, 0.5),
        "near_land": (0.22, 0.32, 0.18),
    },
    # Helsingør: the sound on one side with the far shore low across it, woods
    # on the other.
    "marienlyst_golfklub": {
        "seed": 2207, "zenith": (0.36, 0.52, 0.76), "cloud": (0.94, 0.94, 0.92), "cloudiness": 0.5,
        "hills": (0.8, 4.0), "hill": (0.32, 0.43, 0.36),
        "trees": "broadleaf", "tree_base": 1.6, "tree_spread": 1.8, "tree_count": 380, "tree": (0.14, 0.26, 0.15),
        "sea": [(0.62, 0.2)], "sea_color": (0.34, 0.46, 0.56), "far_coast": (0.4, 1.8),
        "near_land": (0.21, 0.33, 0.19),
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


# --- fence net --------------------------------------------------------------

# The net texture covers NET_TILE_METRES square of net; keep it in step with
# net_tile_metres in src/renderer/fence_batch.h.
NET_SIZE = 32
NET_TILE_METRES = 1.0
NET_MESH = 8  # texels between strands: a 25 cm diamond mesh
NET_COLOUR = (0.13, 0.15, 0.13)


def net_texture() -> list[tuple[float, float, float, float]]:
    """Dark strands crossing on both diagonals, opaque, with clear holes
    between (alpha 0), so mipmaps fade a distant net to a faint grey veil.
    Tiles both ways."""
    pixels = []
    for y in range(NET_SIZE):
        for x in range(NET_SIZE):
            strand = (x + y) % NET_MESH == 0 or (x - y) % NET_MESH == 0
            pixels.append((*NET_COLOUR, 1.0) if strand else (*NET_COLOUR, 0.0))
    return pixels


# --- main -------------------------------------------------------------------

def course_theme(course: dict) -> dict:
    theme = THEMES.get(course["id"], {**DEFAULT_THEME, "seed": zlib.crc32(course["id"].encode("utf-8"))})
    return {**theme, "haze_color": tuple(course["backdrop"]["haze_color"])}


def outputs() -> dict[Path, bytes]:
    assets = REPO / "assets"
    files = {assets / "textures" / "rough_grass.bmp": bmp_bytes(GRASS_SIZE, GRASS_SIZE, grass_texture()),
             assets / "textures" / "fence_net.bmp": bmp_bytes_with_alpha(NET_SIZE, NET_SIZE, net_texture())}
    for course_file in sorted((assets / "courses").glob("*.json")):
        course = json.loads(course_file.read_text(encoding="utf-8"))
        theme = course_theme(course)
        sky = assets / course["backdrop"]["sky"]
        land = assets / course["backdrop"]["land"]
        if sky not in files:
            files[sky] = bmp_bytes(BACKDROP_WIDTH, BACKDROP_HEIGHT, sky_panorama(theme))
        if land not in files:
            files[land] = bmp_bytes_with_alpha(BACKDROP_WIDTH, BACKDROP_HEIGHT, land_panorama(theme))
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
