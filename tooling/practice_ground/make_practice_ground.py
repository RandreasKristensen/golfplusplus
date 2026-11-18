"""Generate the shared, hole-free practice hub in assets/world/hub/.

North is -Z; metres. A road runs east-west along the south and one leaves the
map south. The bus is parked on the stop facing north, its door is the arrival,
and the starter hut and tee book stand ten metres from it. From the door the
walking paths lead north past the putting green (one 40 x 25 m green, nine
cups, a tier and a break), the plaza and clubhouse pad, and the short game (one
chipping green, a deep and a shallow bunker, an uphill and a downhill bank) to
one range tee line of 20 bays facing north into one shared field of target
greens. A hill tee on the ridge at the east end of the same tee line fires into
the same field from 20 m up, reached by a ramp path. The starter hut, clubhouse,
clocks, departure board and the range's furniture (bay dividers, buckets, benches,
bag stands, ball dispensers, distance boards, floodlights) are built where
hub.json says; the caddie shack, cart barn and the clubhouse's rooms are named
pads, set aside and built later. The edges are planted by hub_planting.py:
shelterbelts west and north beyond the range's nets, a wooded crest on the ridge
and hedges along the south roads, which leave the map past a car park of parked
cars. check_layout holds the walking budgets, firing lines and the plants'
clearances.

Elevated-range reference: NSRCC's own Sky Range description documents a real
raised range: https://www.nsrcc.com.sg/golf-sky-range . There is no universal
standard drop; this authored hill uses the owner's 15-25 m brief, targeting
20 m. A lower landing extends carry for a given launch; the target plays
shorter. No physics multiplier is used.

python tooling/practice_ground/make_practice_ground.py [--check]
"""
from __future__ import annotations
import argparse
import functools
import heapq
import json
import math
import random
from pathlib import Path

import hub_planting

REPO = Path(__file__).resolve().parents[2]
FOLDER = REPO / "assets/world/hub"
BUS_PROP = json.loads((REPO / "assets/world/props/hub_bus.json").read_text(encoding="utf-8"))["points"]
BUS_HEADING = round(math.pi, 4)  # parked facing -Z, so its right-hand door opens east

# The bus door (the arrival), 3.2 m ahead of the bus's middle and 2.2 m east of it.
DOOR = (35., 63.)
BUS_STOP = (DOOR[0] + BUS_PROP["approach"][0], DOOR[1] + BUS_PROP["approach"][2])
BOOK = (40., 57.)  # the anchor, north of the book on the starter hut's counter
ROAD_Z = 78.
TEE_Z, REAR_Z = 0., 7.
BAYS = [((i - 9.5) * 3.5, TEE_Z) for i in range(20)]
HILL_BAYS = [(89. + (i - 1) * 3.5, TEE_Z) for i in range(3)]
HILL_HEIGHT, RAMP_FROM, RAMP_TO = 20., 35., 80.
PUTTING = (60., 48., 20., 12.5)  # centre and radii of the 40 x 25 m green
PUTTING_CUPS = [(x, z) for z in (42.5, 48., 53.5) for x in (45., 60., 75.)]
CHIP = (10., 22., 15., 10.)
CHIP_CUP = (9., 21.5)
DEEP_BUNKER = (-2., 34.5, 4.5, 3.)
SHALLOW_BUNKER = (22., 33.5, 4., 2.8)
FLAT_TARGETS = [(-30., -50., 7.), (6., -75., 8.), (-46., -100., 9.), (26., -125., 10.), (-16., -150., 11.),
                (-48., -185., 12.), (14., -215., 13.), (-26., -250., 14.)]
HILL_TARGETS = [(62., -95., 9.), (86., -145., 11.), (60., -200., 13.), (90., -248., 14.)]
FIELD = (-70., -314., 112., 5.)
HUT = (40., 60.55)  # the starter hut's middle: its counter stands behind the book
CLUBHOUSE = (-3., 53.)  # its middle; its gable faces east to the putting green
DEPARTURE_BOARD = (37.2, 66.5)  # by the bus stop, facing north
PADS = [("plaza", 20., 53., 9.), ("clubhouse", CLUBHOUSE[0], CLUBHOUSE[1], 8.), ("clubhouse_pro_shop", -6., 56.5, 3.),
        ("clubhouse_bar", 0., 49.5, 3.), ("clubhouse_simulator", -8., 49.5, 3.),
        ("clubhouse_notice_board", 12., 58., 2.), ("clock_clubhouse", 3., 53., 1.),
        ("starter_hut", HUT[0], HUT[1], 3.), ("clock_starter_hut", HUT[0], HUT[1] - 1.5, 1.),
        ("departure_board", DEPARTURE_BOARD[0], DEPARTURE_BOARD[1], 2.2), ("car_park", 84., 66., 11.),
        ("caddie_shack", 56., 28., 4.), ("cart_barn", 69., 27., 5.)]
FLAT_STRIP = (0., TEE_Z, 39., 3.2)  # the levelled hitting strip under the flat bays: centre x, z, half width, half length
HILL_STRIP = (89., TEE_Z, 6., 3.2)
FLOODLIGHTS = [((x, 10.), (x, -20.)) for x in (-30., -15., 0., 15., 30.)] + [((89., 10.), (89., -20.))]
WALK, ROAD, LOT = 2., 6., 14.
BELTS = [((-98., 66.), (-98., -330.)), ((-104., -338.), (180., -338.))]  # the shelterbelts' lines: west, north
BIRD_SPACING, BIRD_HEIGHT = 70., 6.  # a birds loop every this far along a belt, this high in the canopy
TERRACE_TABLES = ((5.2, 50.8), (5.2, 55.2), (7.6, 53.))
PATHS = [[DOOR, (35., 57.), (35., 49.), (35., 40.), (35., 22.), (35., REAR_Z)],
         [(35., 57.), (38., 57.)], [(35., 49.), (38., 49.)], [(35., 40.), (24., 40.)],
         [(35., REAR_Z), (20., REAR_Z), (0., REAR_Z), (-20., REAR_Z), (-35., REAR_Z)],
         [(35., REAR_Z), (60., REAR_Z), (85.5, REAR_Z), (92.5, REAR_Z)]]
ROADS = [[(-170., ROAD_Z), (-40., ROAD_Z), (70., ROAD_Z), (300., ROAD_Z)], [(70., ROAD_Z), (70., 150.), (70., 280.)]]
BUS_BAY = 4.6  # the bus stop's paved lay-by: the bus's width and a margin either side
LOT_WAYS = [(LOT, [(76., 65.), (92., 65.)]), (ROAD - 1., [(84., 65.), (84., ROAD_Z)]),  # the car park's surface and its drive from the road
            (BUS_BAY, [(BUS_STOP[0], 59.5), (BUS_STOP[0], ROAD_Z)])]  # the bus bay, from past the bus's nose to the road
CARS = [("hub_car_red", 79., 61.7, math.pi), ("hub_car_blue", 82.2, 61.7, math.pi), ("hub_car_cream", 88.6, 61.7, math.pi),
        ("hub_car_silver", 91.6, 61.7, math.pi), ("hub_car_cream", 80.4, 68.4, 0.), ("hub_car_red", 89.6, 68.4, 0.)]
# Ground kept clear of plants (x0, z0, x1, z1): the range with its nets and tee line, the hill tee's terrace with
# its ramp, and the plaza, car park and paths. The shelterbelts and the crest stand outside them.
CLEAR_BOXES = {"range": (-80., -324., 134., 18.), "terrace": (30., -30., 160., 30.), "facilities": (-80., 12., 112., 76.)}
FLIGHT_WIDTH, FLIGHT_SPREAD = 10., .12  # a ball may leave its line by this much plus this share of its length
LOW, HIGH, CELL = (-170., -400.), (300., 280.), 2.
# Walking metres from the door along the paths, to the nearest of each facility.
BUDGETS = {"book": (8., 13.), "putting green": (15., 25.), "short game": (40., 50.),
           "range tee line": (60., 70.), "hill tee": (0., 120.)}
PIECE = 2.  # cart_path.piece_length: the joints the game's path graph is made of


def smooth(value):
    t = min(1., max(0., value))
    return t * t * (3. - 2. * t)


def ramp(t, ease=.2):
    """0 to 1 with a constant slope between eased ends."""
    t = min(1., max(0., t))
    speed = 1. / (1. - ease)
    if t < ease:
        return speed * t * t / (2. * ease)
    if t > 1. - ease:
        return 1. - speed * (1. - t) ** 2 / (2. * ease)
    return speed * (ease / 2. + t - ease)


def outside(x, z, box):
    return math.hypot(max(box[0] - x, 0., x - box[2]), max(box[1] - z, 0., z - box[3]))


def elliptical(x, z, ellipse):
    return math.hypot((x - ellipse[0]) / ellipse[2], (z - ellipse[1]) / ellipse[3])


def flatten(height, x, z, ellipse, level):
    weight = 1. - smooth((elliptical(x, z, ellipse) - 1.) / .25)
    return height * (1. - weight) + level * weight


def putting_surface(x, z):
    tilt = .035 * (z - PUTTING[1]) * (1. - smooth((x - 56.) / 10.))
    return tilt + .4 * smooth((x - 67.) / 5.)


def farmland(x, z):
    """Gentle undulation, calmer toward the facilities."""
    swell = .3 * math.sin(x / 47.) * math.cos(z / 61. + 1.) + .2 * math.sin((x + z) / 83.)
    return swell * smooth(outside(x, z, (-50., -6., 100., 70.)) / 40.)


def ridge_height(x, z):
    """The ridge that closes the east edge."""
    return HILL_HEIGHT * smooth((x - 118.) / 36.) * (1. - smooth((x - 215.) / 55.)) * smooth((z + 340.) / 50.) * (1. - smooth((z - 30.) / 40.))


def land_height(x, z):
    """Farmland, the ridge and the hill tee's spur, the short game's banks, then greens and bunkers."""
    spur = HILL_HEIGHT * ramp((x - RAMP_FROM) / (RAMP_TO - RAMP_FROM)) * (1. - smooth((x - 130.) / 20.)) *         smooth((z + 58.) / 50.) * (1. - smooth((z - 12.) / 10.))
    hill = max(spur, ridge_height(x, z))
    height = farmland(x, z) * (1. - min(1., hill / 1.5)) + hill
    window = smooth((x + 28.) / 12.) * (1. - smooth((x - 34.) / 5.)) * smooth((z - 2.) / 12.) * (1. - smooth((z - 24.) / 20.))
    bank = .8 * smooth((x + 16.) / 12.) + .3 * smooth((x - 26.) / 6.)
    height = height * (1. - window) + bank * window
    height = flatten(height, x, z, CHIP, .8)
    height = flatten(height, x, z, PUTTING, putting_surface(x, z))
    for tx, tz, radius in FLAT_TARGETS + HILL_TARGETS:
        height = flatten(height, x, z, (tx, tz, radius, radius * .8), farmland(tx, tz) + ridge_height(tx, tz))
    for bunker, lump in ((DEEP_BUNKER, -.6), (SHALLOW_BUNKER, .45)):
        height += lump * (1. - smooth((elliptical(x, z, bunker) - .6) / .7))
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


def chipping_mats():
    """A fringe lie, the short rough, an uphill and a downhill bank, and each bunker."""
    return [(10., 33.8), (10., 37.5), (-10., 22.), (31., 22.), (DEEP_BUNKER[0], DEEP_BUNKER[1]), (SHALLOW_BUNKER[0], SHALLOW_BUNKER[1])]


def local(origin, heading, dx, dz):
    """World x, z of the point dx to the left (model +x) and dz ahead (+z) of `origin`, a model turned to `heading`."""
    return (origin[0] + math.cos(heading) * dx + math.sin(heading) * dz,
            origin[1] - math.sin(heading) * dx + math.cos(heading) * dz)


def distance_meters(target, bays):
    """What a target's board says: metres to it from the nearest bay of the tee line that fires at it."""
    return round(min(math.dist(bay, target[:2]) for bay in bays))


def built_props(include_lamps=True):
    """Everything built on the plaza and the range: model, ground position, heading (0 faces +Z, pi faces north), raise."""
    north, east = math.pi, math.pi / 2
    props = []

    def add(model, xz, heading=0., height=0.):
        props.append({"model": model, "position": point(*xz), "heading": round(heading, 4), "height": height})
    add("hub_starter_hut", HUT, north)
    add("hub_clock", local(HUT, north, 0., 1.5), north, 2.35)
    add("hub_pa_pole", (42.9, 59.4), north)
    add("hub_bin", (37.2, 59.6))
    add("hub_bench", (44.4, 59.6), north)
    add("hub_departure_board", DEPARTURE_BOARD, north)
    for row_height in (1.95, 1.50, 1.05, .60):
        add("hub_departure_row", local(DEPARTURE_BOARD, north, 0., .065), north, row_height)
    add("hub_clubhouse", CLUBHOUSE, east)
    add("hub_clock", local(CLUBHOUSE, east, 0., 6.), east, 4.55)
    for xz in TERRACE_TABLES:
        add("hub_terrace_table", xz)
    add("hub_flagpole", (9.4, 58.6), east)
    bays = BAYS + HILL_BAYS
    for x in [(i - 10) * 3.5 for i in range(21)] + [HILL_BAYS[0][0] - 1.75 + 3.5 * i for i in range(4)]:
        add("hub_range_divider", (x, TEE_Z + .6))
    for x, z in bays:
        add("hub_ball_bucket", (x - 1., z + 1.))
    for x in range(-35, 36, 7):
        add("hub_bag_stand", (x, 3.4), north)
    for x in (-24.5, -8.75, 8.75, 24.5, HILL_BAYS[1][0]):
        add("hub_bench", (x, 4.9), north)
    for x in (-37.2, 37.2, 96.2):
        add("hub_ball_dispenser", (x, .8), north)
    for target in FLAT_TARGETS + HILL_TARGETS:
        add("hub_distance_board", board_for(*target)[::2])
    for model, x, z, heading in CARS:
        add(model, (x, z), heading)
    if include_lamps:
        for xz in path_lamps():
            add("hub_path_lamp", xz)
    return props


LAMP_SPACING, LAMP_COVERAGE = 20., 15.
LAMP_ROUTES = PATHS + [[DOOR, (35.,69.)], [(35.,57.), (20.,53.), (10.,53.)],
                       [(40.,64.), (60.,64.), (80.,64.), (96.,64.)],
                       [(82.,48.), (82.,34.), (60.,34.)]]


def lamp_clear(q):
    for p in built_props(False):
        model = json.loads((REPO / "assets/world/props" / (p["model"]+".json")).read_text())
        bounds = model["points"]
        if "body_min" not in bounds: continue
        x, _, z = p["position"]
        h = p["heading"]
        dx, dz = q[0]-x, q[1]-z
        lx, lz = math.cos(h)*dx-math.sin(h)*dz, math.sin(h)*dx+math.cos(h)*dz
        if bounds["body_min"][0]-.4 < lx < bounds["body_max"][0]+.4 and bounds["body_min"][2]-.4 < lz < bounds["body_max"][2]+.4:
            return False
    return abs(q[0]-BUS_STOP[0]) > 1.7 or abs(q[1]-BUS_STOP[1]) > 4.9


def path_lamps():
    lamps = []
    for route in LAMP_ROUTES:
        for a, b in zip(route, route[1:]):
            length = math.dist(a,b)
            count = max(1, math.ceil(length / LAMP_SPACING))
            for k in range(count):
                t = (k+.5)/count
                centre = (a[0]+(b[0]-a[0])*t, a[1]+(b[1]-a[1])*t)
                candidates = [(centre[0]+dx, centre[1]+dz) for dx,dz in
                              ((2.5,0),(-2.5,0),(0,2.5),(0,-2.5),(4.,0),(-4.,0),(0,4.),(0,-4.),(0,10.),(0,-10.),(6.,0),(-6.,0))]
                for q in candidates:
                    if any(near_polyline(q, way) < width/2+.4 for width,way in ways()): continue
                    if any(near_polyline(q,[start,end]) < 3. for start,end in firing_lines()): continue
                    if not lamp_clear(q): continue
                    if math.dist(q,HUT)<3. or math.dist(q,CLUBHOUSE)<9.: continue
                    if math.dist(q,BOOK)<2.5 or math.dist(q,DOOR)<2.: continue
                    if any(math.dist(q,p)<10. for p in lamps): break
                    lamps.append(q)
                    break
    return lamps


def ambience():
    """Looping sounds: birds in the shelterbelts' canopy every BIRD_SPACING, and the clubhouse terrace's chatter."""
    sounds = []
    for start, end in BELTS:
        length = math.dist(start, end)
        for k in range(int(length / BIRD_SPACING) + 1):
            t = (k + .5) * BIRD_SPACING / length
            if t < 1.:
                x, z = start[0] + (end[0] - start[0]) * t, start[1] + (end[1] - start[1]) * t
                sounds.append({"sound": "birds_loop", "position": [round(x, 2), round(land_height(x, z) + BIRD_HEIGHT, 2), round(z, 2)]})
    x = sum(xz[0] for xz in TERRACE_TABLES) / len(TERRACE_TABLES)
    z = sum(xz[1] for xz in TERRACE_TABLES) / len(TERRACE_TABLES)
    sounds.append({"sound": "terrace_chatter_loop", "position": [round(x, 2), round(land_height(x, z) + 1.3, 2), round(z, 2)]})
    return sounds


def board_for(x, z, radius):
    """A target's distance board stands beside it, on the side away from the field's middle."""
    return point(x + (radius + 3.) * (1. if x > 20. else -1.), z)


def outputs():
    zones, mats, targets = [], [], []
    zones.append({"type": "fairway", "outline": rectangle(*FIELD)})
    for x, z, radius in FLAT_TARGETS + HILL_TARGETS:
        zones.append({"type": "green", "outline": ellipse(x, z, radius, radius*.8), "green_stimp_feet": 9.5})
        targets.append({"position": point(x, z), "radius": radius, "board": board_for(x, z, radius),
                        "distance_meters": distance_meters((x, z), BAYS if (x, z, radius) in FLAT_TARGETS else HILL_BAYS)})
    for x, z in BAYS:
        mats.append({"position": point(x, z), "aim": point(x, z - 150.), "reach": 3., "tee_box": True})
    for x, z in HILL_BAYS:
        mats.append({"position": point(x, z), "aim": point(74., -150.), "reach": 3., "tee_box": True})
    cx, cz, rx, rz = CHIP
    zones.append({"type": "fairway", "outline": ellipse(cx, cz, rx + 3., rz + 3.)})
    zones.append({"type": "green", "outline": ellipse(cx, cz, rx, rz), "green_stimp_feet": 9.})
    for bunker in (DEEP_BUNKER, SHALLOW_BUNKER):
        zones.append({"type": "bunker", "outline": ellipse(*bunker)})
    for x, z in chipping_mats():
        mats.append({"position": point(x, z), "aim": point(*CHIP_CUP), "reach": 1.5, "tee_box": False})
    px, pz, prx, prz = PUTTING
    zones.append({"type": "fairway", "outline": ellipse(px, pz, prx + 2.5, prz + 2.5)})
    zones.append({"type": "green", "outline": ellipse(px, pz, prx, prz), "green_stimp_feet": 10.})
    cups = [point(x, z) for x, z in PUTTING_CUPS] + [point(*CHIP_CUP)]
    anchors = {"book": {"position": point(*BOOK), "reach": 2.},
               "pads": [{"name": name, "position": point(x, z), "reach": reach} for name, x, z, reach in PADS],
               "props": built_props()}
    strips = [{"position": point(x, z), "aim": point(x, z - 150.), "half_width": half_width, "half_length": half_length}
              for x, z, half_width, half_length in (FLAT_STRIP, HILL_STRIP)]
    lights = [{"foot": point(*foot), "aim": point(*aim)} for foot, aim in FLOODLIGHTS]
    hub = {"id": "practice_ground", "name": "Practice Ground", "carts": False,
           "rules": {"scored": False, "drops": True, "shot_xp": False, "stations": True, "balls_out": 10},
           "backdrop": {"sky": "sky.bmp", "land": "land.bmp", "haze_color": [.75,.8,.83], "haze_amount": 1., "haze_distance": 700.},
           "sky": {"date": "06-21", "utc_offset_hours": 2}, "green_stimp_feet": 9.5,
           "air": {"elevation_meters": 30., "mean_temperature_c": 15.5, "daily_temperature_range_c": 8.5},
           "world": "world.json", "arrival": {"position": point(*DOOR), "heading": BUS_HEADING}, "hub": anchors,
           "tee_mats": mats, "tee_strips": strips, "floodlights": lights, "targets": targets, "practice_cups": cups,
           "ambience": ambience()}
    roads = [{"width": WALK, "polyline": [point(*xz) for xz in path]} for path in PATHS]
    roads += [{"width": ROAD, "polyline": [point(*xz) for xz in road]} for road in ROADS]
    roads += [{"width": width, "polyline": [point(*xz) for xz in way]} for width, way in LOT_WAYS]
    trees, bushes = planting()
    world = {"id": "practice_ground", "name": "Practice Ground",
             "source": {"type": "generated", "tool": "tooling/practice_ground/make_practice_ground.py"},
             "projection": {"type": "equirectangular", "origin_lat": 56., "origin_lon": 10., "units": "meters"},
             "ground": grid(), "bus_stop": {"position": point(*BUS_STOP), "heading": BUS_HEADING},
             "material_zones": zones, "trees": trees, "bushes": bushes, "collectibles": [], "cart_roads": roads,
             "fences": [net([(-74., 12.), (-74., -318.), (128., -318.), (128., -14.)], 22.)]}
    return {FOLDER/"hub.json": json.dumps(hub,indent=2)+"\n", FOLDER/"world.json": json.dumps(world,separators=(",",":"))+"\n"}


def joints(path):
    """The path's points every PIECE or less, as game/cart_path.cpp lays them."""
    result = []
    for a, b in zip(path, path[1:]):
        pieces = max(1, math.ceil(math.dist(a, b) / PIECE))
        result.extend((a[0] + (b[0] - a[0]) * k / pieces, a[1] + (b[1] - a[1]) * k / pieces) for k in range(pieces))
    return result + [path[-1]]


def walking_distances():
    """Metres along the paths from the door to every joint of the walking paths."""
    key = lambda p: (round(p[0] * 10), round(p[1] * 10))
    nodes, links = {}, {}
    for path in PATHS + ROADS:
        run = joints(path)
        for a, b in zip(run, run[1:]):
            nodes[key(a)], nodes[key(b)] = a, b
            links.setdefault(key(a), set()).add(key(b))
            links.setdefault(key(b), set()).add(key(a))
    spent = {key(DOOR): 0.}
    queue = [(0., key(DOOR))]
    while queue:
        cost, node = heapq.heappop(queue)
        if cost > spent[node]:
            continue
        for other in links[node]:
            total = cost + math.dist(nodes[node], nodes[other])
            if total < spent.get(other, math.inf):
                spent[other] = total
                heapq.heappush(queue, (total, other))
    return {nodes[n]: d for n, d in spent.items()}


def walk_to(distances, place):
    """Path metres to the nearest joint of the paths, then straight on to `place`."""
    joint = min(distances, key=lambda j: math.dist(j, place))
    return distances[joint] + math.dist(joint, place)


def facility_walks():
    distances = walking_distances()
    nearest = lambda places: min(walk_to(distances, p) for p in places)
    return {"book": nearest([BOOK]), "putting green": nearest(PUTTING_CUPS), "short game": nearest(chipping_mats()),
            "range tee line": nearest(BAYS), "hill tee": nearest(HILL_BAYS)}


def firing_lines():
    """(from, to) of every shot the mats invite: bays and the hill tee along their aim, chips to the cup and 10 m past."""
    lines = [((x, z), (x, z - 150.)) for x, z in BAYS] + [((x, z), (74., -150.)) for x, z in HILL_BAYS]
    for x, z in chipping_mats():
        along = ((CHIP_CUP[0] - x), (CHIP_CUP[1] - z))
        length = math.hypot(*along)
        lines.append(((x, z), (CHIP_CUP[0] + along[0] / length * 10., CHIP_CUP[1] + along[1] / length * 10.)))
    return lines


def near_polyline(p, path):
    best = math.inf
    for a, b in zip(path, path[1:]):
        ab = (b[0] - a[0], b[1] - a[1])
        t = max(0., min(1., ((p[0] - a[0]) * ab[0] + (p[1] - a[1]) * ab[1]) / (ab[0] ** 2 + ab[1] ** 2)))
        best = min(best, math.hypot(p[0] - a[0] - ab[0] * t, p[1] - a[1] - ab[1] * t))
    return best


def ways():
    """(width, polyline) of every path, road and the car park."""
    return [(WALK, p) for p in PATHS] + [(ROAD, r) for r in ROADS] + LOT_WAYS


def obstruction(x, z, radius):
    """What a plant of this crown radius at x, z would block, or None: ground kept clear, a path, a pad or a ball's flight."""
    for name, box in CLEAR_BOXES.items():
        if outside(x, z, box) < radius:
            return name
    for width, way in ways():
        if near_polyline((x, z), way) < width / 2 + radius + 1.:
            return "a path"
    for name, px, pz, reach in PADS:
        if math.dist((x, z), (px, pz)) < reach + radius:
            return name
    for start, end in firing_lines():
        if near_polyline((x, z), [start, end]) < radius + FLIGHT_WIDTH + FLIGHT_SPREAD * math.dist(start, end):
            return "a firing line"
    return None


@functools.cache
def planting():
    """(trees, bushes) of the edges: shelterbelts west and north, a wooded crest on the ridge, hedges along the south roads."""
    rng = random.Random(1989)
    trees, bushes = [], []
    clear = lambda x, z, radius: obstruction(x, z, radius) is not None
    hub_planting.shelterbelt(rng, lambda t: 3. * math.sin(t * 9.), *BELTS[0], clear, trees, bushes)
    hub_planting.shelterbelt(rng, lambda t: 2.5 * math.sin(t * 11. + 1.), *BELTS[1], clear, trees, bushes)
    hub_planting.crest_woods(rng, (132., -335., 250., 40.), lambda x, z: ridge_height(x, z) > 9., clear, trees)
    south = ROAD_Z + ROAD / 2. + 3.
    hub_planting.hedge(rng, (-170., south), (300., south), clear, bushes)
    for x in (70. - ROAD / 2. - 3., 70. + ROAD / 2. + 3.):
        hub_planting.hedge(rng, (x, south + 3.), (x, 275.), clear, bushes)
    return trees, bushes


def check_layout():
    """Walking budgets along the paths, firing lines clear of paths and pads, a
    walkable ramp and a 15-25 m drop to every hill target, and the plan's sizes."""
    lamps = path_lamps()
    for lamp in lamps:
        assert lamp_clear(lamp), "lamp blocks a fixture or bus"
        assert all(near_polyline(lamp, way) >= width/2+.4 for width,way in ways()), "lamp blocks path"
        assert all(near_polyline(lamp,[a,b]) >= 3. for a,b in firing_lines()), "lamp in firing line"
    for route in LAMP_ROUTES:
        for joint in joints(route):
            assert min(math.dist(joint,p) for p in lamps) <= LAMP_COVERAGE, f"unlit path at {joint}"
    for name, walk in facility_walks().items():
        low, high = BUDGETS[name]
        assert low <= walk <= high, f"{name}: {walk:.1f} m walk, budget {low}-{high}"
    for start, end in firing_lines():
        for step in range(int(math.dist(start, end)) + 1):
            t = step / max(1., math.dist(start, end))
            p = (start[0] + (end[0] - start[0]) * t, start[1] + (end[1] - start[1]) * t)
            for path in PATHS + ROADS:
                if step > 3:
                    assert near_polyline(p, path) > WALK / 2 + 1., f"shot from {start} crosses a path at {p}"
            for name, x, z, reach in PADS:
                assert math.dist(p, (x, z)) > reach, f"shot from {start} crosses {name}"
    bays_x = [x for x, _ in BAYS]
    assert len(BAYS) == 20 and all(abs(b - a - 3.5) < 1e-6 for a, b in zip(bays_x, bays_x[1:])), "bays"
    for x, z in HILL_BAYS:
        assert z <= TEE_Z and x - max(bays_x) > 40., f"hill tee {(x, z)} must stand beside the bays, never behind"
    assert FIELD[2] - FIELD[0] >= 140. and FIELD[3] - FIELD[1] >= 300., "field size"
    for x, z, _ in FLAT_TARGETS:
        assert 50. <= TEE_Z - z <= 250. and bays_x[0] - 25. < x < bays_x[-1] + 25., f"flat target {(x, z)}"
    for x, z, _ in HILL_TARGETS:
        assert x > bays_x[-1] + 10., f"hill target {(x, z)} lies in the flat bays' field of fire"
        for hx, hz in HILL_BAYS:
            assert 50. <= math.dist((hx, hz), (x, z)) <= 250., f"hill target {(x, z)} distance"
            assert 15. <= land_height(hx, hz) - land_height(x, z) <= 25., f"hill target {(x, z)} drop"
    trees, bushes = planting()
    assert len(trees) > 300 and len(bushes) > 300, "the edges are planted"
    for plant in trees + bushes:
        x, _, z = plant["position"]
        radius = plant.get("leaf_radius", plant.get("radius"))
        assert obstruction(x, z, radius) is None, f"plant at {(x, z)} blocks {obstruction(x, z, radius)}"
    lot = LOT_WAYS[0][1]
    for _, x, z, heading in CARS:
        assert lot[0][0] < x < lot[1][0] and abs(z - lot[0][1]) < LOT / 2. - 1., f"car {(x, z)} stands off the car park"
        assert near_polyline((x, z), LOT_WAYS[1][1]) > 2.5, f"car {(x, z)} blocks the drive"
    bay = LOT_WAYS[2][1]
    assert bay[0][0] == BUS_STOP[0] and bay[0][1] < BUS_STOP[1] - 4.4 and bay[1][1] == ROAD_Z, "the bus bay runs the length of the bus to the road"
    assert PUTTING[2] * 2 == 40. and PUTTING[3] * 2 == 25. and len(PUTTING_CUPS) == 9, "putting green"
    ramp_path = joints(PATHS[-1])
    for a, b in zip(ramp_path, ramp_path[1:]):
        assert abs(land_height(*a) - land_height(*b)) / math.dist(a, b) <= .65, f"ramp too steep at {a}"


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
    walks = ", ".join(f"{name} {walk:.0f} m" for name, walk in facility_walks().items())
    print(f"hub: 20 bays, hill tee, one putting green, one chipping green; walks from the door: {walks}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
