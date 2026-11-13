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
stand/roof, tilted A3 spread/title. Never advance a stage before opening all
four PNGs: side, front, three-quarter, all at eye 1.7 m and distance 12 m,
and three-quarter at 25 m through the full camcorder/tape/TV pipeline.
Preview uses 50 degree vertical FOV and 1280x960; all four retain the pipeline.

Destination and title text are not geometry: the renderer draws the label on
the board rectangle (board_center, board_size, board_normal) with the sign text
path. The door is on the bus's right-hand (kerb) side in Denmark: model -X.

py -3 tooling/practice_ground/make_hub_props.py            # both, final stages
py -3 tooling/practice_ground/make_hub_props.py --check    # shipped JSON is current
py -3 tooling/practice_ground/make_hub_props.py hub_bus --stage silhouette
py -3 tooling/art/preview_prop/preview_prop.py hub_bus --out build/prop_preview/bus/silhouette
"""
import argparse
import json
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
            "board_center": [0,2.5,4.26], "board_size": [1.85,.30,0], "board_normal": [0,0,1]},
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
        parts += [box([-1.21,1.35,3.35],[.045,2.,1.02],"brown",tag="door"),
            box([-1.24,1.63,3.09],[.02,1.3,.42],"glass"),
            box([-1.24,1.63,3.61],[.02,1.3,.42],"glass"),
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
    model["points"]["body_min"][2] = -4.36
    model["points"]["body_max"][2] = 4.36
    return model


def book(stage):
    model = {"kind": "hub_book", "colors": {"wood": [.28,.15,.065],
        "roof": [.20,.29,.19], "paper": [.88,.84,.70], "cover": [.16,.25,.14], "ink": [.07,.07,.06]},
        "points": {"approach": [0,0,-1.1], "body_min": [-.65,0,-.55], "body_max": [.65,2.1,.55],
            "board_center": [0,1.70,.185], "board_size": [.90,.28,0], "board_normal": [0,0,-1]},
        "parts": [box([0,1.08,0],[1.,.10,.66],"wood",turn=[-15,0,0]),
            box([0,1.5,.30],[1.,.75,.10],"wood"),
            box([0,1.70,.235],[.96,.32,.035],"paper"),
            box([0,2.0,.04],[1.3,.12,.95],"roof",turn=[-8,0,0])]}
    for x in (-.40,.40):
        for z in (-.22,.22):
            model["parts"].append(box([x,.53,z],[.10,1.06,.10],"wood"))
    # Two back posts carry the notice board between them and end inside the roof.
    for x in (-.55,.55):
        model["parts"].append(box([x,.99,.30],[.10,1.98,.10],"wood"))
    if stage >= 1:
        model["parts"] += [box([0,1.18,-.02],[.64,.045,.46],"cover",turn=[-15,0,0]),
            box([.153,1.216,-.02],[.294,.025,.42],"paper",turn=[-15,0,3]),
            box([-.153,1.216,-.02],[.294,.025,.42],"paper",turn=[-15,0,-3]),
            box([0,1.22,-.02],[.012,.026,.42],"ink",turn=[-15,0,0])]
    return model


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("model", nargs="?", choices=("hub_bus", "tee_book"))
    parser.add_argument("--stage", choices=STAGES+("stand","book"), default=None)
    parser.add_argument("--check", action="store_true", help="fail if shipped JSON differs from the final stage")
    args = parser.parse_args()
    if args.stage and not args.model:
        parser.error("--stage needs a model")
    stale = []
    for name in ([args.model] if args.model else ["hub_bus", "tee_book"]):
        stage = args.stage or ("livery" if name == "hub_bus" else "book")
        model = bus(STAGES.index(stage)) if name == "hub_bus" else book(("stand","book").index(stage))
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
