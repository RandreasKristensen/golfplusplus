#!/usr/bin/env python3
"""
verify_osm_import.py — Check an imported course and draw its contact sheet.

The converter runs this on every import. Run it on its own to check the
courses already in assets/, after a re-import or after hand edits:

  py -3 verify_osm_import.py mollerup_golf_club
  py -3 verify_osm_import.py --all
  py -3 verify_osm_import.py kalo_par_3 --assets OUT --quiet

It reads the course manifest, its holes and its course world, compares them
with the course's entry in testdata/scorecards.json when there is one, and
prints a per-hole table and coded findings (osm_checks.py). It also audits the
course's OSM features for missing information (osm_audit.py; from the cache,
or OSM when not cached; --no-osm skips it) and writes what to map as a
checklist, <course>_osm_todo.md. The contact sheet (osm_contact_sheet.py) is
written as an SVG to compare with the club's course map. Exit status 1 when
any check is an error.
"""

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import osm_audit
import osm_checks
import osm_contact_sheet

HERE = Path(__file__).resolve().parent
DEFAULT_ASSETS = HERE.parent.parent / "assets"
DEFAULT_SHEETS = HERE / ".osm_cache" / "contact_sheets"


def verify_course(course_id: str, course_name: str, hole_jsons: list[dict], world: dict | None,
                  tolerances: dict, sheet_path: Path | None, quiet: bool = False,
                  out=sys.stderr, audit: "osm_checks.Findings | None" = None) -> "osm_checks.Findings":
    """
    Check one course, print the result, write its contact sheet; the findings,
    with `audit` (osm_audit.py's findings on its OSM data) among them.
    """
    scorecard = osm_checks.load_scorecard(course_id)
    findings = osm_checks.check_course(course_id, hole_jsons, world, scorecard, tolerances)
    if audit is not None:
        findings.extend(audit)
    placed = osm_checks.placed_holes(hole_jsons, world)
    against = (f"scorecard, {scorecard.get('tees', '?')} tees" if scorecard
               else "no scorecard in testdata/scorecards.json")
    print(f"\n=== Verify {course_name} ({course_id}) against {against}", file=out)
    if not quiet:
        for row in osm_checks.hole_table(placed, scorecard):
            print("  " + row, file=out)
    levels = ("error", "warn") if quiet else ("error", "warn", "info")
    for line in findings.lines(levels):
        print("  " + line, file=out)
    if sheet_path is not None:
        sheet_path.parent.mkdir(parents=True, exist_ok=True)
        with open(sheet_path, "w", encoding="utf-8") as f:
            f.write(osm_contact_sheet.render(course_name, placed, scorecard, findings))
        print(f"  Contact sheet: {sheet_path}", file=out)
    print(f"  RESULT: {findings.count('error')} error(s), {findings.count('warn')} warning(s), "
          f"{findings.count('info')} note(s)", file=out)
    return findings


def write_osm_todo(course_name: str, course_el: dict, audit: "osm_checks.Findings", path: Path,
                   out=sys.stderr):
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        f.write(osm_audit.todo_markdown(course_name, course_el, audit))
    print(f"  OSM to-do: {path} ({audit.count('warn')} gap(s) the import guessed around, "
          f"{audit.count('info')} nice to have)", file=out)


def _load_course(assets: Path, course_id: str):
    with open(assets / "courses" / f"{course_id}.json", "r", encoding="utf-8") as f:
        course = json.load(f)
    holes = []
    for ref in course.get("holes", []):
        with open(assets / ref, "r", encoding="utf-8") as f:
            holes.append(json.load(f))
    world = None
    if course.get("world"):
        with open(assets / course["world"], "r", encoding="utf-8") as f:
            world = json.load(f)
    return course, holes, world


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("course", nargs="*", help="Course id(s), the names of files in assets/courses")
    ap.add_argument("--all", action="store_true", help="Every course in assets/courses")
    ap.add_argument("--assets", default=str(DEFAULT_ASSETS), help=f"Asset root (default: {DEFAULT_ASSETS})")
    ap.add_argument("--config", default=str(HERE / "osm_golf_config.json"),
                    help="Generator config whose `checks` section sets the tolerances")
    ap.add_argument("--sheet-out", default=str(DEFAULT_SHEETS), metavar="DIR",
                    help=f"Where contact sheets go (default: {DEFAULT_SHEETS})")
    ap.add_argument("--quiet", action="store_true", help="Errors and warnings only")
    ap.add_argument("--no-osm", action="store_true",
                    help="Skip the audit of the course's OSM data (needs the cache or the network)")
    args = ap.parse_args()

    # Imported here, not at the top: the converter imports this module.
    import osm_golf_convert as conv
    conv.set_cache(HERE / ".osm_cache")

    assets = Path(args.assets)
    ids = sorted(p.stem for p in (assets / "courses").glob("*.json")) if args.all else args.course
    if not ids:
        ap.print_help()
        return 2
    errors = 0
    for course_id in ids:
        course, holes, world = _load_course(assets, course_id)
        config = conv.load_generation_config(args.config, course_id)
        name = course.get("name", course_id)
        audited = None
        course_ref = (world or {}).get("source", {}).get("osm_ref")
        if not args.no_osm and course_ref:
            try:
                audited = osm_audit.audit_from_osm(course_ref, course_id, config,
                                                   osm_checks.load_scorecard(course_id))
            except conv.OverpassUnavailable as e:
                print(f"  [warn] OSM audit skipped for {course_id}: {e}", file=sys.stdout)
        findings = verify_course(course_id, name, holes, world,
                                 config.get("checks", {}), Path(args.sheet_out) / f"{course_id}.svg",
                                 args.quiet, out=sys.stdout, audit=audited[0] if audited else None)
        if audited:
            write_osm_todo(name, audited[1], audited[0], Path(args.sheet_out) / f"{course_id}_osm_todo.md",
                           out=sys.stdout)
        errors += findings.count("error")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
