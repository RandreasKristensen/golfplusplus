"""Generate the shared, hole-free practice hub in assets/world/hub/.

Three short-game greens occupy over five times the original 15 x 10 m ellipse.
The six-bay flat range and the hill range fire north, away from the arrival
and the short game. The hill route approaches behind its tee line.

Elevated-range reference: NSRCC's own Sky Range description documents a real
raised range: https://www.nsrcc.com.sg/golf-sky-range . There is no universal
standard drop; this authored hill uses the owner's 15-25 m brief, targeting
20 m. A lower landing extends carry for a given launch; the target plays
shorter. No physics multiplier is used.

python tooling/practice_ground/make_practice_ground.py [--check]
"""
from __future__ import annotations
import argparse
import json
import math
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
FOLDER = REPO / "assets/world/hub"
# Arrival stays behind the flat firing line, beside the short-game approach.
BOOK = (45.0, 35.0)
# The parked bus faces -Z (heading pi), so its right-hand door opens toward the book.
BUS_STOP = (30.0, 35.0)
HILL = (210.0, -50.0)
GREENS = [(90.0, 45.0, 9.0), (155.0, 55.0, 11.0), (140.0, 115.0, 8.0)]
RADII = (18.0, 14.0)
FLAT_TARGETS = [(-14., -50., 7.), (12., -80., 8.), (-6., -110., 9.),
                (16., -140., 10.), (-12., -175., 11.), (4., -210., 12.)]
HILL_TARGETS = [(210., -130., 9.), (195., -210., 12.), (230., -285., 14.)]
ROUTE = [BOOK, (50., 35.), (50., 145.), (230., 155.), (285., 100.), (285., 10.), (210., 10.), HILL]
LOW, HIGH, CELL = (-80., -350.), (310., 180.), 2.
# Each range's firing line fires -Z from its tee: (tee, length, half width).
RANGES = [((0., 0.), 290., 55.), (HILL, 285., 50.)]


def smooth(value):
    t = min(1., max(0., value))
    return t * t * (3. - 2. * t)


def land_height(x, z):
    across = 1. - smooth((abs(x - HILL[0]) - 65.) / 75.)
    along = smooth((z + 150.) / 80.) * smooth((130. - z) / 160.)
    height = 20. * across * along
    for tx, tz, rise in [(195., -210., 1.2), (230., -285., 3.)]:
        height += rise * (1. - smooth(math.hypot(x-tx, z-tz) / 22.))
    for index, (gx, gz, _) in enumerate(GREENS):
        radius = math.hypot((x-gx) / RADII[0], (z-gz) / RADII[1])
        weight = 1. - smooth((radius - 1.) / .7)
        surface = 0. if index == 0 else (.65 * smooth((z-gz+4.) / 8.) if index == 1
                                         else .035 * (x-gx) + .015 * (z-gz))
        height = height * (1. - weight) + surface * weight
    return height


def point(x, z):
    return [round(x, 2), round(land_height(x, z), 2), round(z, 2)]


def ellipse(x, z, rx, rz, count=32):
    return [[round(x + rx*math.cos(i*math.tau/count), 2),
             round(z + rz*math.sin(i*math.tau/count), 2)] for i in range(count)]


def rectangle(x0, z0, x1, z1):
    # Extra collinear points keep the shared rounded-outline loader's corners tight.
    corners = [(x0,z0), (x1,z0), (x1,z1), (x0,z1)]
    result = []
    for a, b in zip(corners, corners[1:] + corners[:1]):
        for t in (0., .05, .95):
            result.append([round(a[0]+(b[0]-a[0])*t,2), round(a[1]+(b[1]-a[1])*t,2)])
    return result


def grid():
    columns = round((HIGH[0]-LOW[0])/CELL)+1
    rows = round((HIGH[1]-LOW[1])/CELL)+1
    heights = []
    for row in range(rows):
        encoded, last = [], 0
        for col in range(columns):
            cm = round(100*land_height(LOW[0]+col*CELL, LOW[1]+row*CELL))
            encoded.append(cm-last); last = cm
        heights.append(encoded)
    return {"origin": list(LOW), "cell_size": CELL, "columns": columns, "rows": rows, "heights_cm": heights}


def net(corners, height):
    poles = []
    for a, b in zip(corners, corners[1:]):
        steps = math.ceil(math.dist(a,b)/18.)
        poles.extend(point(a[0]+(b[0]-a[0])*i/steps, a[1]+(b[1]-a[1])*i/steps) for i in range(steps))
    poles.append(point(*corners[-1]))
    return {"poles": poles, "height": height}


def outputs():
    zones, mats, cups, targets = [], [], [], []
    zones.append({"type": "fairway", "outline": rectangle(-52., 8., 52., -290.)})
    zones.append({"type": "fairway", "outline": rectangle(168., -80., 252., -330.)})
    for positions in (FLAT_TARGETS, HILL_TARGETS):
        for x, z, radius in positions:
            zones.append({"type": "green", "outline": ellipse(x,z,radius,radius*.8), "green_stimp_feet": 9.5})
            targets.append({"position": point(x,z), "radius": radius})
    for i, (x,z,_) in enumerate(FLAT_TARGETS):
        mats.append({"position": point(-30.+i*12.,0.), "aim": point(x,z), "reach": 3., "tee_box": True})
    for i in range(3):
        mats.append({"position": point(HILL[0]+(i-1)*12.,HILL[1]), "aim": point(*HILL_TARGETS[i][:2]),
                     "reach": 3., "tee_box": True})
    for index, (x,z,speed) in enumerate(GREENS):
        zones.append({"type": "fairway", "outline": ellipse(x,z,RADII[0]+3.,RADII[1]+3.)})
        zones.append({"type": "green", "outline": ellipse(x,z,*RADII), "green_stimp_feet": speed})
        for side in (-1.,1.):
            zones.append({"type": "bunker", "outline": ellipse(x+side*24.,z+4.,5.,4.)})
        cup = point(x-4., z-2.)
        cups.append(cup)
        for dx,dz in [(-9.,-6.),(-30.,-3.)]:
            mats.append({"position": point(x+dx,z+dz), "aim": cup, "reach": 1.5, "tee_box": False})
    zones.append({"type": "fairway", "outline": rectangle(55.,30.,72.,58.)})
    zones.append({"type": "fairway", "outline": rectangle(100.,70.,125.,90.)})
    trees = []
    for x in (-65.,65.,150.,275.):
        for z in range(-300,-20,24):
            trees.append({"position": point(x,float(z)), "leaf_radius": 3.5, "leaf_height": 7., "trunk_height": 3.})
    anchors = {"book": {"position": point(*BOOK), "reach": 2.}}
    hub = {"id": "practice_ground", "name": "Practice Ground", "carts": False,
           "backdrop": {"sky": "sky.bmp", "land": "land.bmp", "haze_color": [.75,.8,.83], "haze_amount": 1., "haze_distance": 700.},
           "sky": {"date": "06-21", "utc_offset_hours": 2}, "green_stimp_feet": 9.5,
           "air": {"elevation_meters": 30., "mean_temperature_c": 15.5, "daily_temperature_range_c": 8.5},
           "world": "world.json", "arrival": point(BOOK[0], BOOK[1]+4.), "hub": anchors,
           "tee_mats": mats, "targets": targets, "practice_cups": cups}
    world = {"id": "practice_ground", "name": "Practice Ground",
             "source": {"type": "generated", "tool": "tooling/practice_ground/make_practice_ground.py"},
             "projection": {"type": "equirectangular", "origin_lat": 56., "origin_lon": 10., "units": "meters"},
             "ground": grid(), "bus_stop": {"position": point(*BUS_STOP), "heading": round(math.pi, 4)},
             "material_zones": zones, "trees": trees, "collectibles": [],
             "cart_roads": [{"width": 1.8, "polyline": [point(*xz) for xz in ROUTE]},
                            {"width": 1.8, "polyline": [point(*xz) for xz in [BOOK,(45.,20.),(0.,20.),(0.,5.)]]}],
             "fences": [net([(-55.,12.),(-55.,-300.),(55.,-300.),(55.,12.)],18.),
                        net([(160.,-25.),(160.,-335.),(260.,-335.),(260.,-25.)],22.)]}
    return {FOLDER/"hub.json": json.dumps(hub,indent=2)+"\n", FOLDER/"world.json": json.dumps(world,separators=(",",":"))+"\n"}


def check_layout():
    """The walking route stays behind every firing line, the short-game cups
    stay well clear of them, and the hill drops 15-25 m to its targets."""
    for (tx, tz), length, half_width in RANGES:
        for x, z in ROUTE:
            assert tz - z <= .1 or abs(x - tx) > half_width, f"route point {(x, z)} is in a firing line"
        for x, z, _ in GREENS:
            cx, cz = x - 4., z - 2.
            assert tz - cz < 0 or tz - cz > length or abs(cx - tx) > half_width + 20, f"cup {(cx, cz)} is in a firing line"
    for x, z, _ in HILL_TARGETS:
        assert 15. <= land_height(*HILL) - land_height(x, z) <= 25., f"hill target {(x, z)} drop"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    check_layout()
    for path, text in outputs().items():
        if args.check:
            if not path.exists() or path.read_text(encoding="utf-8") != text:
                raise SystemExit(f"stale generated hub: {path}")
        else:
            path.parent.mkdir(parents=True,exist_ok=True)
            path.write_text(text,encoding="utf-8")
    print("hub: three short-game greens, six flat bays, three hill bays")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
