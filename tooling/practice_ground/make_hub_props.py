"""Code-drawn club shuttle and tee book, inspected with preview_prop.

Reference: Aldridge Transport Museum's 1988 MCW Metrorider F685YOG:
https://www.amrtm.org/collection/OB1115 . Its original silver/red/blue livery
uses broad bands; the preserved green/cream scheme dates to Metrowest in
1994. Our fictional club uses cream, rust and brown, not a replica livery.
Owner proportions: length 8.5 m, width 2.4 m, height 3.0 m; wheelbase 4.2 m,
front overhang 1.9 m; tyres diameter .9 m, width .3 m; floor .35 m;
window band 1.25-2.25 m. Model frame: +X left, +Y up, +Z forward.

Build/inspection checklist, in order: silhouette (two body boxes, rounded
roof); raked front, lamps, grille, bumper and luminous destination board;
continuous side glass, pillars and front-right door; inset cylindrical
wheels and arches; rear glass/lamps/bumper; broad livery blocks. Then book:
the open spread on the starter counter and its wall title. Never advance a stage before opening all
four PNGs: side, front, three-quarter, all at eye 1.7 m and distance 12 m,
and three-quarter at 25 m through the full camcorder/tape/TV pipeline.
Preview uses 50 degree vertical FOV and 1280x960; all four retain the pipeline.

Destination and title text are not geometry: the renderer draws the label on
the board rectangle (board_center, board_size, board_normal) with the sign text
path. The door is on the bus's right-hand (kerb) side in Denmark: model -X.

The plaza's and range's built things (hub_clubhouse, hub_starter_hut, the range furniture, the
clocks and boards, the car park's cars) are one stage each; hub.json places them.

py -3 tooling/practice_ground/make_hub_props.py            # all, final stages
py -3 tooling/practice_ground/make_hub_props.py --check    # shipped JSON is current
py -3 tooling/practice_ground/make_hub_props.py hub_bus --stage silhouette
py -3 tooling/art/preview_prop/preview_prop.py hub_bus --out build/prop_preview/bus/silhouette
"""
import argparse
import json
import math
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
STAGES = ("silhouette", "front", "sides", "wheels", "rear", "livery")


def box(at, size, color, **extra):
    return {"shape": "box", "at": at, "size": size, "color": color, **extra}


def cylinder(a, b, radius, color):
    return {"shape": "cylinder", "from": a, "to": b, "radius": radius, "color": color}


def bus(stage):
    model = {"kind": "hub_bus", "colors": {
        "cream": [.86,.83,.71], "rust": [.59,.22,.10], "brown": [.27,.13,.07],
        "glass": [.035,.065,.075], "rubber": [.022,.025,.026], "arch": [.07,.07,.065],
        "metal": [.56,.58,.56], "lamp": [.91,.87,.65], "red": [.65,.06,.025],
        "amber": [.88,.38,.035], "board": [.83,.82,.56]},
        "points": {"approach": [-2.2,0,3.2], "door": [-1.21,1.2,3.2],
            "body_min": [-1.2,0,-4.25], "body_max": [1.2,3,4.25],
            "wheel_contact_fl": [.99,0,2.35], "wheel_contact_fr": [-.99,0,2.35],
            "wheel_contact_rl": [.99,0,-1.85], "wheel_contact_rr": [-.99,0,-1.85],
            "board_center": [0,2.5,4.26], "board_size": [1.85,.30,0], "board_normal": [0,0,1],
            "engine": [0,.8,-3.6], "horn": [0,.8,4.3], "door_hinge": [-1.21,0,3.86]},
        "numbers": {"door_swing_seconds": 1.6, "door_open_degrees": 100},
        "parts": [box([0,.8,0],[2.4,.9,8.5],"cream",tag="lower_body"),
            box([0,2,-.06],[2.25,1.5,8.24],"cream"),
            box([0,2.76,0],[1.8,.48,8.5],"cream"),
            cylinder([-.9,2.75,-4.1],[-.9,2.75,4.1],.25,"cream"),
            cylinder([.9,2.75,-4.1],[.9,2.75,4.1],.25,"cream")]}
    parts = model["parts"]
    if stage >= 1:
        parts += [box([0,1.78,4.16],[2.02,.96,.055],"glass",turn=[-9,0,0]),
            box([0,1.2,4.24],[2.25,.17,.10],"cream"),
            box([0,2.5,4.21],[1.96,.34,.09],"board",glow="flag"),
            box([0,.79,4.275],[2.12,.35,.06],"arch"),
            box([0,.43,4.29],[2.4,.18,.14],"metal")]
        for x in (-.91,-.63,.63,.91):
            parts.append(box([x,.84,4.28],[.23,.22,.065],"lamp"))
    if stage >= 2:
        for x in (-1.1375,1.1375):
            parts.append(box([x,1.75,-.35],[.02,1.,7.],"glass"))
            for z in (-3.45,-1.9,-.35,1.2,2.75):
                parts.append(box([x*1.01,1.75,z],[.03,1.04,.13],"cream"))
        # The door leaf (tag "door") swings open about the door_hinge point while the bus boards; the dark doorway shows behind it.
        parts += [box([-1.19,1.35,3.35],[.02,1.98,1.0],"arch"),
            box([-1.21,1.35,3.35],[.045,2.,1.02],"cream",tag="door"),
            box([-1.24,1.63,3.09],[.02,1.3,.42],"glass",tag="door"),
            box([-1.24,1.63,3.61],[.02,1.3,.42],"glass",tag="door"),
            box([-1.245,1.12,3.35],[.02,.23,1.02],"rust",tag="door"),
            box([-1.245,.94,3.35],[.02,.13,1.02],"brown",tag="door"),
            box([-1.21,.35,3.35],[.10,.13,.96],"rubber")]
    if stage >= 3:
        parts[0] = box([0,.8,0],[1.68,.9,8.5],"cream",tag="lower_body")
        # Outer panels stop at each arch; the tyre projects past its dark recess.
        for sign in (-1,1):
            for a,b in ((-4.25,-2.42),(-1.28,1.78),(2.92,4.25)):
                parts.append(box([sign*1.05,.8,(a+b)/2],[.30,.9,b-a],"cream",tag="skirt_panel"))
            parts.append(box([sign*1.05,1.10,0],[.30,.30,8.5],"cream"))
            for z in (-1.85,2.35):
                parts.append(box([sign*.86,.64,z],[.06,.66,1.14],"arch"))
                parts.append(cylinder([sign*.84,.45,z],[sign*1.14,.45,z],.45,"rubber"))
                parts.append(cylinder([sign*1.145,.45,z],[sign*1.165,.45,z],.20,"metal"))
    if stage >= 4:
        parts += [box([0,1.75,-4.21],[1.86,.95,.06],"glass"),
            box([0,.43,-4.29],[2.4,.18,.14],"metal"),
            box([0,.80,-4.28],[1.10,.28,.07],"arch")]
        for x in (-.99,.99):
            parts += [box([x,.73,-4.28],[.19,.23,.065],"red"),
                box([x,1.01,-4.28],[.19,.18,.065],"amber")]
    if stage >= 5:
        for sign in (-1,1):
            parts += [box([sign*1.215,1.12,0],[.02,.23,8.5],"rust"),
                box([sign*1.215,.94,0],[.02,.13,8.5],"brown")]
            for a,b in ((-4.25,-2.42),(-1.28,1.78),(2.92,4.25)):
                parts.append(box([sign*1.215,.42,(a+b)/2],[.02,.13,b-a],"rubber"))
        parts += [box([0,1.12,4.30],[2.35,.17,.025],"rust"),
            box([0,1.22,-4.30],[2.35,.17,.025],"rust")]
    if stage >= 2:
        # Cut every fixed box crossing the doorway, including the body's core
        # and livery strips. The recess is set inside the opening.
        cut = []
        for part in parts:
            if part.get("shape") != "box" or part.get("tag") == "door":
                cut.append(part)
                continue
            x,y,z = part["at"]; w,h,d = part["size"]
            if x-w/2 < -.7 and y+h/2 > .36 and y-h/2 < 2.36 and z+d/2 > 2.84 and z-d/2 < 3.86:
                for low,high in ((z-d/2,min(z+d/2,2.84)),(max(z-d/2,3.86),z+d/2)):
                    if high>low:
                        cut.append({**part, "at": [x,y,(low+high)/2], "size": [w,h,high-low]})
            else:
                cut.append(part)
        parts[:] = cut
        model["colors"]["interior"] = [.003,.003,.003]
        parts.append(box([-.72,1.36,3.35],[.02,2.,1.02],"interior"))
    model["points"]["body_min"][2] = -4.36
    model["points"]["body_max"][2] = 4.36
    return model


def book():
    return {"kind": "hub_book", "colors": {"paper": [.88,.84,.70], "cover": [.16,.25,.14], "ink": [.07,.07,.06]},
        "points": {"approach": [0,0,-1.93], "body_min": [-.34,1.05,-.23], "body_max": [.34,1.13,.23],
            "board_center": [0,2.02,.02], "board_size": [.90,.16,0], "board_normal": [0,0,-1]},
        "parts": [box([0,1.07,0],[.68,.04,.46],"cover"),
            box([.16,1.10,0],[.30,.025,.42],"paper",turn=[0,0,3]),
            box([-.16,1.10,0],[.30,.025,.42],"paper",turn=[0,0,-3]),
            box([0,1.11,0],[.012,.026,.42],"ink"),
            box([0,2.02,.04],[.96,.20,.035],"paper")]}


def path_lamp():
    model = fixture("hub_fixture", {"iron": [.20,.21,.20], "glass": [.95,.63,.25]},
        [-.09,0,-.09], [.09,3.6,.09],
        [cylinder([0,0,0],[0,3.35,0],.07,"iron"),
         box([0,3.45,0],[.48,.32,.48],"glass",glow="window"),
         box([0,3.65,0],[.62,.10,.62],"iron")], lamp=[0,3.45,0])
    model["colors"]["light"] = [1.,.57,.23]
    model["numbers"] = {"light_radius": 18., "light_intensity": 1.8, "light_cone_deg": 82., "light_soft_deg": 18.}
    return model


# Built things of the plaza and the range (placed from hub.json by make_practice_ground.py).
# Palette: cream, club green, brown and rust, like the bus and the book.
WALL, ROOF_TILE, TRIM = [.82,.78,.64], [.42,.17,.11], [.86,.85,.80]
GREEN, BROWN, GLASS = [.17,.32,.20], [.27,.13,.07], [.04,.07,.09]
CURTAIN, IRON, CONCRETE = [.62,.50,.25], [.20,.21,.20], [.52,.51,.47]


def fixture(kind, colors, low, high, parts, **points):
    return {"kind": kind, "colors": colors, "points": {"body_min": low, "body_max": high, **points}, "parts": parts}


def board_points(center, size, normal=(0,0,1)):
    return {"board_center": list(center), "board_size": list(size), "board_normal": list(normal)}


def range_divider():
    """A bay divider: a low panel between two golfers, 0.9 m high, 2 m long (a shank stops on it)."""
    return fixture("hub_fixture", {"panel": [.50,.36,.20], "post": BROWN, "cap": TRIM},
        [-.07,0,-1.], [.07,.95,1.], [
        box([0,.46,0],[.08,.84,1.8],"panel"), box([0,.93,0],[.14,.05,2.04],"cap"),
        box([0,.46,-.92],[.14,.92,.14],"post"), box([0,.46,.92],[.14,.92,.14],"post")])


def ball_bucket():
    parts = [cylinder([0,0,0],[0,.30,0],.20,"plastic"), cylinder([0,.28,0],[0,.32,0],.24,"rim")]
    parts += [{"shape": "sphere", "at": [dx,.34,dz], "radius": .05, "color": "ball"}
              for dx, dz in ((0,0),(.09,.04),(-.09,.04),(.04,-.09),(-.04,-.09),(.0,.11),(.11,-.04),(-.11,-.04))]
    return fixture("hub_fixture", {"plastic": [.15,.25,.50], "rim": [.10,.17,.36], "ball": [.92,.92,.90]},
        [-.24,0,-.24], [.24,.40,.24], parts)


def bench():
    parts = [box([0,.45,.0],[1.6,.04,.42],"wood"), box([0,.78,-.2],[1.6,.30,.04],"wood")]
    parts += [box([sx*.72,.22,0],[.06,.44,.42],"iron") for sx in (-1,1)]
    parts += [box([sx*.72,.62,-.2],[.06,.50,.04],"iron") for sx in (-1,1)]
    return fixture("hub_fixture", {"wood": [.40,.24,.12], "iron": IRON}, [-.8,0,-.25], [.8,.95,.25], parts)


def bag_stand():
    """A rack for two golf bags: a low cradle on two uprights."""
    parts = [box([0,.03,0],[1.0,.06,.5],"iron"), box([0,.80,.0],[.9,.05,.05],"iron")]
    parts += [cylinder([sx*.45,0,0],[sx*.45,.85,0],.03,"iron") for sx in (-1,1)]
    for sx, shade in ((-1,"bag_red"),(1,"bag_blue")):
        parts += [cylinder([sx*.2,.12,0],[sx*.2,.88,.06],.13,shade), cylinder([sx*.2,.88,.06],[sx*.2,.92,.06],.14,"iron")]
        parts += [cylinder([sx*.2+dx,.90,.06],[sx*.2+dx*1.4,1.18,.06],.012,"steel") for dx in (-.05,.0,.05)]
    return fixture("hub_fixture", {"iron": IRON, "bag_red": [.55,.10,.07], "bag_blue": [.10,.18,.45], "steel": [.60,.62,.60]},
        [-.55,0,-.3], [.55,1.1,.3], parts)


def ball_dispenser():
    """A coin-op range ball machine: a rust cabinet, a dark chute and a tray."""
    parts = [box([0,.62,0],[.70,1.24,.55],"body"), box([0,1.27,0],[.78,.06,.62],"cap"),
             box([0,.90,.28],[.5,.30,.02],"glass"), box([0,.44,.28],[.34,.20,.04],"chute"),
             box([0,.34,.42],[.50,.05,.26],"tray"), box([.20,.62,.28],[.12,.18,.02],"plate"),
             box([0,.04,0],[.8,.08,.65],"cap")]
    return fixture("hub_fixture", {"body": [.55,.22,.11], "cap": [.27,.13,.07], "glass": GLASS, "chute": [.08,.08,.08],
        "tray": [.55,.57,.55], "plate": [.80,.70,.25]}, [-.4,0,-.33], [.4,1.3,.55], parts)


def clock():
    """A wall clock: a face the renderer writes the hub's time on, 1.7 x 0.34 m, the plate's centre at the origin."""
    return {"kind": "hub_label", "colors": {"frame": [.14,.11,.08], "board": [.86,.83,.62]},
        "points": board_points([0,0,.09],[1.7,.34,0]),
        "parts": [box([0,0,.04],[1.8,.44,.08],"frame"), box([0,0,.07],[1.7,.34,.04],"board")]}


def distance_board():
    """A range distance board on two posts, 3 x 1 m, facing the tee line (+z)."""
    parts = [box([sx*1.3,1.6,-.02],[.12,3.2,.12],"post") for sx in (-1,1)]
    parts += [box([0,2.5,0],[3.2,1.2,.1],"frame"), box([0,2.5,.07],[3.0,1.0,.04],"board")]
    return fixture("hub_sign", {"post": BROWN, "frame": [.12,.20,.14], "board": [.90,.78,.22]},
        [-1.65,0,-.1], [1.65,3.2,.1], parts, **board_points([0,2.5,.095],[3.0,1.0,0]))


def departure_board():
    """The bus stop's departure board: a title strip over a panel that hub_departure_row labels sit in front of."""
    parts = [box([sx*1.55,1.375,-.02],[.12,2.75,.12],"post") for sx in (-1,1)]
    parts += [box([0,1.45,0],[3.5,2.5,.1],"frame"), box([0,2.45,.07],[3.3,.40,.04],"board")]
    return fixture("hub_sign", {"post": BROWN, "frame": [.16,.12,.09], "board": [.86,.83,.62]},
        [-1.75,0,-.1], [1.75,2.75,.1], parts, **board_points([0,2.45,.095],[3.3,.40,0]))


def departure_row():
    """One row of the departure board: a plate 3.1 x 0.4 m, its centre at the origin, that a departure is written on."""
    return {"kind": "hub_label", "colors": {"board": [.86,.83,.62]},
        "points": board_points([0,0,.016],[3.1,.40,0]), "parts": [box([0,0,0],[3.1,.40,.03],"board")]}


def starter_hut():
    """The starter's kiosk, 3.6 x 3.0 m with a flat roof, a serving window and a counter on its front (+z)."""
    w, d, h = 3.6, 3.0, 2.6
    parts = [box([0,.04,0],[w+.3,.08,d+.3],"slab"),
             box([0,.5,d/2-.05],[w,1.0,.1],"wall"), box([0,2.2,d/2-.05],[w,.8,.1],"wall"),
             box([-w/2+.2,1.5,d/2-.05],[.4,1.0,.1],"wall"), box([w/2-.2,1.5,d/2-.05],[.4,1.0,.1],"wall"),
             box([0,1.45,d/2-.12],[w-.8,.8,.04],"glass"),
             box([0,1.88,d/2-.03],[w-.7,.06,.12],"trim"), box([0,1.02,d/2+.12],[w-.2,.06,.34],"counter"),
             box([0,1.3,-d/2+.05],[w,2.6,.1],"wall"),
             box([-w/2+.05,1.3,0],[.1,2.6,d-.1],"wall"), box([w/2-.05,1.3,0],[.1,2.6,d-.1],"wall"),
             box([0,h+.1,.1],[w+.9,.16,d+.8],"roof"), box([0,h-.04,d/2+.35],[w+.9,.12,.06],"trim"),
             box([w/2+.01,1.05,-.2],[.03,2.0,.9],"door"), box([w/2+.02,1.3,-.55],[.02,.12,.02],"trim"),
             box([w/2+.01,1.55,-.2],[.03,.5,.4],"glass")]
    return fixture("hub_fixture", {"slab": CONCRETE, "wall": WALL, "glass": GLASS, "trim": TRIM, "counter": [.40,.24,.12],
        "roof": [.20,.29,.19], "door": BROWN}, [-w/2,0,-d/2], [w/2,h+.2,d/2+.3], parts)


def pa_pole():
    parts = [cylinder([0,0,0],[0,4.6,0],.06,"pole"), box([0,4.45,0],[1.0,.06,.06],"pole")]
    for sx in (-1,1):
        parts += [box([sx*.4,4.45,.18],[.30,.26,.30],"horn"), cylinder([sx*.4,4.45,.3],[sx*.4,4.45,.52],.17,"horn"),
                  cylinder([sx*.4,4.45,.5],[sx*.4,4.45,.54],.19,"dark")]
    return fixture("hub_announcer", {"pole": IRON, "horn": [.50,.52,.50], "dark": [.08,.08,.08]}, [-.1,0,-.1], [.1,4.7,.1], parts,
                   speaker=[0,4.45,.3])


def bin_():
    return fixture("hub_fixture", {"body": [.55,.22,.11], "lid": [.27,.13,.07]}, [-.27,0,-.27], [.27,1.0,.27],
        [cylinder([0,0,0],[0,.88,0],.25,"body"), cylinder([0,.88,0],[0,.94,0],.27,"lid")])


def terrace_table():
    parts = [cylinder([0,.0,0],[0,.72,0],.05,"leg"), cylinder([0,.72,0],[0,.76,0],.55,"top")]
    for dx, dz in ((.85,0),(-.85,0),(0,.85),(0,-.85)):
        parts += [box([dx,.44,dz],[.42,.04,.42],"chair"), box([dx,.22,dz],[.06,.44,.06],"leg")]
    return fixture("hub_fixture", {"leg": IRON, "top": [.86,.85,.80], "chair": [.40,.24,.12]}, [-1.1,0,-1.1], [1.1,.9,1.1], parts)


def flagpole():
    """A flagpole with a Danish flag flying towards +z."""
    parts = [cylinder([0,0,0],[0,9.0,0],.07,"pole"), {"shape": "sphere", "at": [0,9.1,0], "radius": .11, "color": "gold"},
             box([0,8.4,.8],[.02,1.0,1.55],"red"), box([0,8.4,.8],[.04,.22,1.55],"white"), box([0,8.4,.55],[.04,1.0,.22],"white")]
    return fixture("hub_fixture", {"pole": [.82,.83,.80], "gold": [.75,.60,.20], "red": [.65,.07,.07], "white": [.92,.92,.90]},
        [-.12,0,-.12], [.12,9.0,.12], parts)


def clubhouse():
    """The clubhouse shell, 9 x 12 m, a gable roof along z, its gable front (+z) on a terrace."""
    half_w, half_l, wall = 4.5, 6.0, 3.0
    rise, apex = 3.3, 6.3
    parts = [box([0,.1,0],[half_w*2+.3,.2,half_l*2+.3],"slab"), box([0,wall/2+.1,0],[half_w*2,wall,half_l*2],"wall")]
    # The gable end and the roof space: stacked slices as wide as the gable at their middle.
    slices = 14
    for k in range(slices):
        y0, y1 = wall + rise * k / slices, wall + rise * (k + 1) / slices
        width = half_w * 2 * (1. - ((y0 + y1) / 2 - wall) / rise)
        parts.append(box([0, round((y0 + y1) / 2 + .1, 3), 0], [round(width, 3), round(y1 - y0 + .01, 3), half_l * 2], "wall"))
    # Each roof slab lies on the gable's edge, running on past the eave, thick towards the outside.
    theta = math.atan2(rise, half_w)
    reach = half_w + .5
    centre_x = reach / 2
    edge_y = wall + .1 + rise - centre_x * math.tan(theta)
    for sx in (-1, 1):
        parts.append(box([round(sx * (centre_x + .1 * math.sin(theta)), 3), round(edge_y + .1 * math.cos(theta), 3), 0],
                         [round(reach / math.cos(theta), 3), .20, half_l * 2 + .8], "roof",
                         turn=[0, 0, -sx * round(math.degrees(theta), 2)]))
    parts.append(box([0, wall + .1 + rise + .12, 0], [.45, .14, half_l * 2 + .8], "roof"))
    parts.append(box([-2.6, 5.2, -2.4], [.7, 2.8, .7], "brick"))
    # Front: glass doors, two lit windows, a plinth and a step on the terrace.
    front = half_l
    parts += [box([0, 1.2, front + .02], [2.2, 2.0, .06], "glass"), box([0, 1.2, front + .06], [.06, 2.0, .04], "trim"),
              box([0, 2.28, front + .05], [2.4, .12, .10], "trim"), box([0, .2, front + .04], [9.2, .3, .12], "concrete"),
              box([0, .12, front + 1.0], [3.2, .12, .8], "concrete")]
    for sx in (-1, 1):
        parts += [box([sx * 3.2, 1.6, front + .02], [1.6, 1.3, .06], "curtain", glow="window"),
                  box([sx * 3.2, 1.6, front + .06], [1.8, .08, .04], "trim"), box([sx * 3.2, .95, front + .06], [1.8, .08, .06], "trim"),
                  box([sx * 3.2, 2.27, front + .06], [1.8, .08, .04], "trim")]
    # Sides: six windows a side, lit at night.
    for sx in (-1, 1):
        for z in (-4.2, -2.5, -.8, .9, 2.6, 4.3):
            parts += [box([sx * (half_w + .03), 1.7, z], [.05, 1.4, 1.2], "trim"),
                      box([sx * (half_w + .04), 1.7, z], [.05, 1.2, 1.0], "curtain", glow="window")]
    # The terrace deck in front.
    parts += [box([0, .05, front + 3.6], [10.4, .10, 5.6], "deck"), box([0, .12, front + 6.35], [10.4, .06, .10], "trim")]
    return fixture("hub_fixture", {"slab": CONCRETE, "wall": WALL, "roof": ROOF_TILE, "trim": TRIM, "glass": GLASS,
        "curtain": CURTAIN, "brick": [.45,.22,.15], "concrete": [.55,.54,.50], "deck": [.50,.43,.34]},
        [-half_w - .1, 0, -half_l - .1], [half_w + .1, apex + .4, half_l + .1], parts)


def car(body, shape):
    """A parked late-1980s car, 4.2 m (the estate 4.4) long and 1.7 m wide, nose to +z: a boxy hatchback, estate or saloon."""
    tail = -2.3 if shape == "estate" else -2.1
    length = 2.1 - tail
    middle = (2.1 + tail) / 2
    parts = [box([0, .62, middle], [1.7, .62, length], "body"), box([0, .42, 2.12], [1.74, .20, .14], "bumper"),
             box([0, .42, tail - .02], [1.74, .20, .14], "bumper"), box([0, .30, middle], [1.5, .16, length - .3], "dark")]
    roof_back = {"hatch": -1.45, "estate": -2.0, "sedan": -.85}[shape]
    roof_height = 1.58 if shape == "estate" else 1.52
    roof_middle = (.6 + roof_back) / 2
    glass_height = roof_height - .93
    parts += [box([0, roof_height, roof_middle], [1.5, .05, .6 - roof_back], "body"),
              box([0, .93 + glass_height / 2, .78], [1.46, glass_height * .95, .05], "glass", turn=[-35, 0, 0])]
    if shape == "hatch":
        parts.append(box([0, 1.25, -1.6], [1.4, glass_height * .95, .05], "glass", turn=[30, 0, 0]))
    elif shape == "estate":
        parts.append(box([0, 1.27, roof_back - .02], [1.46, glass_height * .9, .04], "glass"))
    else:
        parts += [box([0, 1.2, -.98], [1.4, glass_height * .85, .05], "glass", turn=[40, 0, 0]),
                  box([0, .99, -1.55], [1.68, .10, 1.0], "body")]
    pillars = (.55, -.4, -1.3, roof_back) if shape != "sedan" else (.55, -.15, roof_back)
    for sx in (-1, 1):
        parts.append(box([sx * .74, .93 + glass_height / 2, roof_middle - .05], [.03, glass_height * .85, .55 - roof_back], "glass"))
        parts += [box([sx * .745, .93 + glass_height / 2, z], [.05, glass_height, .08], "body") for z in pillars]
        parts += [box([sx * .855, .75, 0], [.01, .07, length - .3], "trim"), box([sx * .9, 1.07, .72], [.1, .08, .14], "body")]
        parts += [box([sx * .856, .8, z], [.01, .02, .22], "dark") for z in (.2, -.7)]
        parts += [box([sx * .855, .55, z], [.012, .5, .02], "dark") for z in (.55, -.4, -1.3)]
        for z in (1.3, tail + .8):
            parts += [box([sx * .86, .40, z], [.05, .64, .95], "dark"), cylinder([sx * .68, .29, z], [sx * .92, .29, z], .29, "rubber"),
                      cylinder([sx * .915, .29, z], [sx * .93, .29, z], .15, "metal")]
        parts += [box([sx * .58, .80, 2.11], [.4, .16, .05], "lamp"), box([sx * .62, .82, tail], [.34, .18, .05], "tail")]
    parts += [box([0, .72, 2.11], [.5, .14, .04], "dark"), box([0, .46, 2.21], [.5, .12, .02], "plate_front"),
              box([0, .56, tail - .1], [.5, .12, .02], "plate_rear")]
    colors = {"body": body, "glass": [.04,.07,.08], "rubber": [.022,.025,.026], "metal": [.56,.58,.56], "dark": [.05,.05,.05],
              "bumper": [.18,.18,.17], "lamp": [.91,.87,.65], "tail": [.60,.05,.03], "trim": [.12,.12,.12],
              "plate_front": [.9,.9,.88], "plate_rear": [.82,.66,.12]}
    return fixture("hub_fixture", colors, [-.95, 0, tail - .12], [.95, roof_height + .05, 2.22], parts)


BUILT = {"hub_path_lamp": path_lamp, "hub_range_divider": range_divider, "hub_ball_bucket": ball_bucket, "hub_bench": bench, "hub_bag_stand": bag_stand,
         "hub_ball_dispenser": ball_dispenser, "hub_clock": clock, "hub_distance_board": distance_board,
         "hub_departure_board": departure_board, "hub_departure_row": departure_row, "hub_starter_hut": starter_hut,
         "hub_pa_pole": pa_pole, "hub_bin": bin_, "hub_terrace_table": terrace_table, "hub_flagpole": flagpole,
         "hub_clubhouse": clubhouse,
         "hub_car_red": lambda: car([.62,.10,.08], "hatch"), "hub_car_blue": lambda: car([.12,.20,.45], "estate"),
         "hub_car_cream": lambda: car([.80,.76,.62], "sedan"), "hub_car_silver": lambda: car([.60,.62,.62], "hatch")}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("model", nargs="?", choices=("hub_bus", "tee_book") + tuple(BUILT))
    parser.add_argument("--stage", choices=STAGES, default=None)
    parser.add_argument("--check", action="store_true", help="fail if shipped JSON differs from the final stage")
    args = parser.parse_args()
    if args.stage and args.model != "hub_bus":
        parser.error("--stage needs hub_bus")
    stale = []
    for name in ([args.model] if args.model else ["hub_bus", "tee_book"] + list(BUILT)):
        stage = args.stage or ("livery" if name == "hub_bus" else "book" if name == "tee_book" else "built")
        model = (BUILT[name]() if name in BUILT else bus(STAGES.index(stage)) if name == "hub_bus"
                 else book())
        path = ROOT / "assets/world/props" / (name + ".json")
        text = json.dumps(model,indent=2)+"\n"
        if args.check:
            if not path.exists() or path.read_text(encoding="utf-8") != text:
                stale.append(str(path))
            continue
        path.write_text(text,encoding="utf-8")
        print(f"{name}: {stage}, {len(model['parts'])} parts")
    if stale:
        raise SystemExit("stale generated props: " + ", ".join(stale))


if __name__ == "__main__":
    main()
