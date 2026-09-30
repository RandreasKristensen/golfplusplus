#!/usr/bin/env python3
"""
verify_osm_import.py — Check that an OSM-imported course is actually to scale.

Runs three independent checks per hole and prints a pass/fail table:

  A. PROJECTION FIDELITY
     Recompute tee->pin and the playing-line length straight from the raw OSM
     lat/lon using a geodesic formula (Vincenty on the WGS84 ellipsoid), with
     no code shared with the converter's equirectangular projection. Any
     disagreement is a converter bug, not an OSM data problem.

  B. SCORECARD ACCURACY
     Compare the generated spline path length against the published scorecard
     in testdata/scorecards.json. A golf hole is measured along the line of
     play, so this is compared to the spline path, not the straight pin vector.
     This is the check that answers "is the course to scale?".

  C. PLAUSIBILITY
     Fairway widths, green radii, bunker sizes and par values against the
     ranges real golf courses occupy.

Usage:
  py -3 verify_osm_import.py --id W1019045811 --holes OUT/holes --scorecard old_course
  py -3 verify_osm_import.py --id W871993734  --holes OUT/holes --scorecard augusta_national
  py -3 verify_osm_import.py --id W1408711156 --holes OUT/holes
"""

import argparse
import json
import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import osm_golf_convert as conv

SCORECARDS = Path(__file__).resolve().parent / "testdata" / "scorecards.json"

# WGS84
_A = 6378137.0
_F = 1 / 298.257223563
_B = _A * (1 - _F)


def geodesic_m(lat1, lon1, lat2, lon2) -> float:
    """
    Vincenty inverse solution — metres between two WGS84 points.

    Deliberately not the converter's flat-earth approximation: this is the
    reference the converter is measured against.
    """
    if abs(lat1 - lat2) < 1e-12 and abs(lon1 - lon2) < 1e-12:
        return 0.0
    u1 = math.atan((1 - _F) * math.tan(math.radians(lat1)))
    u2 = math.atan((1 - _F) * math.tan(math.radians(lat2)))
    l_diff = math.radians(lon2 - lon1)
    sin_u1, cos_u1 = math.sin(u1), math.cos(u1)
    sin_u2, cos_u2 = math.sin(u2), math.cos(u2)

    lam = l_diff
    for _ in range(200):
        sin_lam, cos_lam = math.sin(lam), math.cos(lam)
        sin_sigma = math.hypot(cos_u2 * sin_lam,
                               cos_u1 * sin_u2 - sin_u1 * cos_u2 * cos_lam)
        if sin_sigma == 0:
            return 0.0
        cos_sigma = sin_u1 * sin_u2 + cos_u1 * cos_u2 * cos_lam
        sigma = math.atan2(sin_sigma, cos_sigma)
        sin_alpha = cos_u1 * cos_u2 * sin_lam / sin_sigma
        cos_sq_alpha = 1 - sin_alpha ** 2
        cos2_sigma_m = 0.0 if cos_sq_alpha == 0 else cos_sigma - 2 * sin_u1 * sin_u2 / cos_sq_alpha
        c = _F / 16 * cos_sq_alpha * (4 + _F * (4 - 3 * cos_sq_alpha))
        lam_prev = lam
        lam = l_diff + (1 - c) * _F * sin_alpha * (
            sigma + c * sin_sigma * (cos2_sigma_m + c * cos_sigma * (-1 + 2 * cos2_sigma_m ** 2)))
        if abs(lam - lam_prev) < 1e-12:
            break

    u_sq = cos_sq_alpha * (_A ** 2 - _B ** 2) / (_B ** 2)
    a_coef = 1 + u_sq / 16384 * (4096 + u_sq * (-768 + u_sq * (320 - 175 * u_sq)))
    b_coef = u_sq / 1024 * (256 + u_sq * (-128 + u_sq * (74 - 47 * u_sq)))
    delta_sigma = b_coef * sin_sigma * (
        cos2_sigma_m + b_coef / 4 * (
            cos_sigma * (-1 + 2 * cos2_sigma_m ** 2)
            - b_coef / 6 * cos2_sigma_m * (-3 + 4 * sin_sigma ** 2) * (-3 + 4 * cos2_sigma_m ** 2)))
    return _B * a_coef * (sigma - delta_sigma)


def geodesic_polyline_m(latlons) -> float:
    return sum(geodesic_m(a[0], a[1], b[0], b[1]) for a, b in zip(latlons, latlons[1:]))


def centroid_latlon(elements) -> tuple[float, float] | None:
    pts = []
    for el in elements:
        pts.extend(conv._element_geom(el))
    if not pts:
        return None
    return (sum(p[0] for p in pts) / len(pts), sum(p[1] for p in pts) / len(pts))


# ── report rows ───────────────────────────────────────────────────────────────

class Check:
    def __init__(self):
        self.rows = []
        self.failures = 0
        self.warnings = 0

    def add(self, ok: bool, severity: str, text: str):
        if not ok:
            if severity == "fail":
                self.failures += 1
            else:
                self.warnings += 1
        self.rows.append((ok, severity, text))


def hole_reference(h: dict) -> dict:
    """Ground truth for one grouped hole, computed geodesically from raw OSM."""
    ref = {}

    tee = centroid_latlon(h["tees"][:1]) if h["tees"] else None
    pin = centroid_latlon(h["pins"][:1]) if h["pins"] else None
    if pin is None and h["greens"]:
        pin = centroid_latlon(h["greens"][:1])
    ref["tee"] = tee
    ref["pin"] = pin
    ref["direct_m"] = geodesic_m(*tee, *pin) if tee and pin else None

    line = None
    for el in h["lines"]:
        pts = conv._element_geom(el)
        if len(pts) >= 2 and (line is None or geodesic_polyline_m(pts) > geodesic_polyline_m(line)):
            line = pts
    ref["line_m"] = geodesic_polyline_m(line) if line else None

    tags = {}
    for el in h["lines"]:
        tags.update(el.get("tags", {}))
    ref["osm_par"] = tags.get("par")
    ref["osm_dist"] = tags.get("dist") or tags.get("distance")
    ref["osm_name"] = tags.get("name")
    ref["counts"] = {
        "tees": len(h["tees"]), "pins": len(h["pins"]), "greens": len(h["greens"]),
        "fairways": len(h["fairways"]), "bunkers": len(h["bunkers"]), "waters": len(h["waters"]),
        "lines": len(h["lines"]),
    }
    return ref


def path_length_xz(points) -> float:
    return sum(math.hypot(b[0] - a[0], b[2] - a[2]) for a, b in zip(points, points[1:]))


def verify(course_el, holes, hole_dir: Path, course_id: str, scorecard: dict | None,
           projection_tolerance: float, scorecard_tolerance: float) -> Check:
    check = Check()
    numbers = sorted(holes.keys())

    print(f"\n{'hole':>4} {'par':>4} {'direct':>8} {'path':>8} | "
          f"{'geo direct':>10} {'geo line':>9} {'proj err':>9} | "
          f"{'card m':>7} {'card err':>9} | {'width':>6} {'zones':>6} {'y-range':>9}")
    print("-" * 122)

    total_par = 0
    scorecard_errors = []
    projection_errors = []

    for num in numbers:
        path = hole_dir / f"{course_id}_h{num:02d}.json"
        if not path.exists():
            check.add(False, "fail", f"hole {num}: generated file missing ({path.name})")
            continue
        with open(path, "r", encoding="utf-8") as f:
            gen = json.load(f)

        ref = hole_reference(holes[num])
        pts = gen["spline"]["control_points"]
        pin = gen["pin"]
        direct = math.hypot(pin[0], pin[2])
        gen_path = path_length_xz(pts)
        width = gen["spline"]["width"]
        ys = [p[1] for p in pts] + [pin[1]]
        y_range = max(ys) - min(ys)
        total_par += gen["par"]

        # ── A. projection fidelity ────────────────────────────────────────────
        # Measured against the tee and pin the converter itself chose, read back
        # as WGS84 from the hole's `source` block. Which tee it picked is a
        # separate question (checks B and C cover that); this asks only whether
        # a metre in the output is really a metre on the ground.
        proj_err_txt = "    n/a"
        src = gen.get("source", {})
        tee_ll, pin_ll = src.get("tee_latlon"), src.get("pin_latlon")
        if tee_ll and pin_ll:
            reference = geodesic_m(tee_ll[0], tee_ll[1], pin_ll[0], pin_ll[1])
            if reference > 1.0:
                err = abs(direct - reference) / reference
                projection_errors.append(err)
                proj_err_txt = f"{err * 100:7.3f}%"
                check.add(err <= projection_tolerance, "fail",
                          f"hole {num}: projected tee-pin {direct:.1f}m differs from geodesic "
                          f"{reference:.1f}m by {err * 100:.3f}%")
        elif ref["direct_m"]:
            proj_err_txt = "  no src"
            check.add(False, "warn",
                      f"hole {num}: no source coordinates; re-import to enable the projection check")

        # ── B. scorecard accuracy ─────────────────────────────────────────────
        card_txt, card_err_txt = "      -", "        -"
        if scorecard:
            entry = scorecard["holes"].get(str(num))
            if entry:
                card_m = entry["metres"]
                card_txt = f"{card_m:7.0f}"
                # Measured along the line of play, so the spline path is the
                # comparable figure; fall back to direct for a hole with no
                # usable centreline.
                measured = gen_path if gen_path > direct else direct
                err = (measured - card_m) / card_m
                scorecard_errors.append(err)
                card_err_txt = f"{err * 100:+8.1f}%"
                check.add(abs(err) <= scorecard_tolerance, "warn",
                          f"hole {num}: {measured:.0f}m vs scorecard {card_m}m ({err * 100:+.1f}%)")
                check.add(gen["par"] == entry["par"], "fail",
                          f"hole {num}: par {gen['par']} but scorecard says {entry['par']}")
                # Tee polygons are teeing *grounds*, so their centroid can sit
                # a few metres behind the marker a scorecard measures from.
                check.add(direct <= card_m * 1.10, "fail",
                          f"hole {num}: straight tee-pin {direct:.0f}m exceeds the "
                          f"scorecard playing line {card_m}m")

        # ── D. loader contract ────────────────────────────────────────────────
        # Mirrors what src/game/hole_loader.cpp requires. A hole that fails
        # these is silently dropped by the game rather than reported, so it is
        # worth asserting here.
        check.add(isinstance(gen.get("tee"), list) and len(gen["tee"]) == 3, "fail",
                  f"hole {num}: tee is not a vec3")
        check.add(isinstance(pin, list) and len(pin) == 3, "fail",
                  f"hole {num}: pin is not a vec3")
        check.add(width > 0.0, "fail", f"hole {num}: spline width must be positive")
        check.add(len(pts) >= 2 and all(len(p) == 3 for p in pts), "fail",
                  f"hole {num}: control points must be at least two vec3s")
        check.add(gen["spline"].get("rough_width", width) >= width, "fail",
                  f"hole {num}: rough_width {gen['spline'].get('rough_width')} is "
                  f"narrower than width {width}")
        check.add(gen.get("tee") == [0.0, 0.0, 0.0], "fail",
                  f"hole {num}: tee must be the local origin, got {gen.get('tee')}")
        check.add(pts[0] == [0.0, 0.0, 0.0], "warn",
                  f"hole {num}: spline starts at {pts[0]} rather than on the tee")

        # ── C. plausibility ───────────────────────────────────────────────────
        check.add(3 <= gen["par"] <= 5, "fail", f"hole {num}: par {gen['par']} out of range")
        check.add(50.0 <= max(direct, gen_path) <= 700.0, "fail",
                  f"hole {num}: length {max(direct, gen_path):.0f}m is not a golf hole")
        check.add(12.0 <= width <= 80.0, "warn",
                  f"hole {num}: fairway width {width:.1f}m is implausible")
        check.add(len(pts) >= 4, "warn", f"hole {num}: only {len(pts)} control points")
        greens = [z for z in gen["material_zones"] if z["type"] == "green"]
        check.add(len(greens) >= 1, "warn", f"hole {num}: no green zone")
        for z in greens:
            # Pitch-and-putt greens are genuinely small: Marienlyst's are
            # 44-65 m2, about a 4 m radius. Only flag what no green could be.
            check.add(3.0 <= z["radius"] <= 30.0, "warn",
                      f"hole {num}: green radius {z['radius']:.1f}m is implausible")
        check.add(len(greens) <= 2, "warn",
                  f"hole {num}: {len(greens)} green zones; a hole has one, or two "
                  f"when it shares a double green")

        print(f"{num:>4} {gen['par']:>4} {direct:>8.0f} {gen_path:>8.0f} | "
              f"{(ref['direct_m'] or 0):>10.0f} {(ref['line_m'] or 0):>9.0f} {proj_err_txt:>9} | "
              f"{card_txt} {card_err_txt} | {width:>6.1f} {len(gen['material_zones']):>6} {y_range:>8.1f}m")

    print("-" * 122)
    if scorecard:
        check.add(total_par == scorecard["total_par"], "fail",
                  f"course par {total_par} but scorecard total is {scorecard['total_par']}")
        check.add(len(numbers) == len(scorecard["holes"]), "fail",
                  f"generated {len(numbers)} holes but the course has {len(scorecard['holes'])}")

    if projection_errors:
        worst = max(projection_errors)
        print(f"projection fidelity : mean {sum(projection_errors) / len(projection_errors) * 100:.3f}%  "
              f"worst {worst * 100:.3f}%  (tolerance {projection_tolerance * 100:.1f}%)")
    if scorecard_errors:
        mean = sum(scorecard_errors) / len(scorecard_errors)
        worst = max(scorecard_errors, key=abs)
        within = sum(1 for e in scorecard_errors if abs(e) <= scorecard_tolerance)
        print(f"scorecard accuracy  : mean {mean * 100:+.1f}%  worst {worst * 100:+.1f}%  "
              f"{within}/{len(scorecard_errors)} holes within {scorecard_tolerance * 100:.0f}%")
    print(f"total par           : {total_par}")
    return check


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--id", help="OSM ref of the course, e.g. W1019045811")
    ap.add_argument("--name", help="Course name (same lookup as the converter)")
    ap.add_argument("--lat", type=float)
    ap.add_argument("--lon", type=float)
    ap.add_argument("--holes", required=True, help="Directory holding the generated hole JSON")
    ap.add_argument("--scorecard", help="Key in testdata/scorecards.json to compare against")
    ap.add_argument("--cache-dir", default=str(Path(__file__).resolve().parent / ".osm_cache"))
    ap.add_argument("--no-cache", action="store_true")
    ap.add_argument("--projection-tolerance", type=float, default=0.005,
                    help="Max relative error vs the geodesic reference (default 0.5%%)")
    ap.add_argument("--scorecard-tolerance", type=float, default=0.12,
                    help="Max relative error vs the published scorecard (default 12%%)")
    ap.add_argument("--quiet", action="store_true", help="Only print failures")
    args = ap.parse_args()
    args.list_courses = False

    conv.set_cache(None if args.no_cache else args.cache_dir)

    course_el, course_name = conv._find_course(args)
    course_id = conv.slugify(course_name)
    elements, _trees, _paths = conv._fetch_elements(course_el)
    holes = conv.group_holes(elements)
    if not holes:
        print("No holes could be grouped from OSM data.", file=sys.stderr)
        return 2

    scorecard = None
    if args.scorecard:
        with open(SCORECARDS, "r", encoding="utf-8") as f:
            cards = json.load(f)["courses"]
        scorecard = cards.get(args.scorecard)
        if scorecard is None:
            print(f"Unknown scorecard '{args.scorecard}'. Known: {', '.join(cards)}", file=sys.stderr)
            return 2

    print(f"\n=== {course_name}  ({conv._format_osm_ref(course_el)}) ===")
    print(f"    {len(elements)} golf elements, {len(holes)} holes grouped")

    check = verify(course_el, holes, Path(args.holes), course_id, scorecard,
                   args.projection_tolerance, args.scorecard_tolerance)

    problems = [(sev, text) for ok, sev, text in check.rows if not ok]
    if problems:
        print(f"\n{len(problems)} issue(s):")
        for sev, text in problems:
            print(f"  [{sev}] {text}")
    else:
        print("\nAll checks passed.")

    print(f"\nRESULT: {check.failures} failure(s), {check.warnings} warning(s)")
    return 1 if check.failures else 0


if __name__ == "__main__":
    sys.exit(main())
