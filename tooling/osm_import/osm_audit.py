"""
osm_audit.py — What a course's OpenStreetMap data is missing, and how to map it.

The importer guesses wherever OSM is silent: a hole with no golf=hole line gets
a made-up tee, a line without `par` gets a par from its length, holes without
`ref` are numbered by walking distance. Every guess is a place where the import
can be wrong without any check noticing, so this audit runs on the raw OSM
features of every import (and in verify_osm_import.py) and reports each gap as
a coded finding that says what is missing, where, and what to draw or tag in
OSM to fill it. Fixing the gaps in OSM and re-importing improves the game and
the map at once; per-course overrides in osm_golf_config.json are for what OSM
can't hold.

The same findings are written as a checklist (todo_markdown) next to the
course's contact sheet, with a link to each feature and to the OSM editor at
each spot.

Levels: `warn` where the import had to guess (fix it in OSM), `info` where it
is only less complete than it could be.
"""

from __future__ import annotations

import math

import osm_checks
import osm_golf_convert as conv

DEFAULT_AUDIT = {
    # A hole line's end this close to a green (metres, 0 inside it) ends on it.
    "green_reach_m": 15.0,
    # A hole line's start this close to a teeing ground starts on it.
    "tee_reach_m": 15.0,
    # How far a green's candidate tees may be, for a hole with no line.
    "candidate_tee_m": [25.0, 650.0],
    # A fairway this close to a green leads up to it.
    "fairway_reach_m": 30.0,
    # A hole line's end this far outside the course boundary leaves features out.
    "boundary_slack_m": 5.0,
}

_HOLE_TAG = "golf=hole"


def osm_link(el: dict) -> str:
    return f"https://www.openstreetmap.org/{el['type']}/{el['id']}"


def edit_link(lat: float, lon: float) -> str:
    return f"https://www.openstreetmap.org/edit#map=20/{lat:.6f}/{lon:.6f}"


def _ref(el: dict) -> str:
    return conv._format_osm_ref(el)


def _latlon(p) -> str:
    return f"({p[0]:.6f}, {p[1]:.6f})"


class _Course:
    """The raw golf features in local metres (X east, Z south)."""

    def __init__(self, elements: list[dict], course_el: dict):
        pts = [p for el in elements for p in conv._element_geom(el)]
        self.origin = (sum(p[0] for p in pts) / len(pts), sum(p[1] for p in pts) / len(pts)) if pts else (0.0, 0.0)
        self.boundary_el = course_el
        self.boundary = [[conv._latlon_to_xz(lat, lon, *self.origin) for lat, lon in poly]
                         for poly in conv._course_footprint_polygons(course_el)]

        def of(tag):
            return [el for el in elements if el.get("tags", {}).get("golf") == tag and conv._element_geom(el)]

        self.lines = [el for el in of("hole") if len(conv._element_geom(el)) >= 2]
        self.greens = of("green")
        self.tees = of("tee")
        self.fairways = of("fairway")
        self.pins = of("pin") + of("flagstick")
        self._shapes: dict = {}

    def xz(self, el: dict) -> list[tuple[float, float]]:
        return [conv._latlon_to_xz(lat, lon, *self.origin) for lat, lon in conv._element_geom(el)]

    def outside_by(self, pt) -> float:
        """Metres `pt` lies outside the course boundary (0 inside, or with no boundary)."""
        if not self.boundary:
            return 0.0
        return min(conv._point_to_polygon_distance_xz(pt, poly) for poly in self.boundary)

    def latlon(self, pt) -> tuple[float, float]:
        return conv._xz_to_latlon(pt[0], pt[1], *self.origin)

    def distance(self, pt, el: dict) -> float:
        """Metres from `pt` to a feature: 0 inside an area (a closed way or a multipolygon)."""
        if el["type"] == "node":
            shape = self.xz(el)
            return math.hypot(pt[0] - shape[0][0], pt[1] - shape[0][1])
        best = math.inf
        key = conv._element_key(el)
        if key not in self._shapes:
            self._shapes[key] = conv._element_shapes_xz(el, *self.origin)
        for shape, closed in self._shapes[key]:
            if closed:
                best = min(best, conv._point_to_polygon_distance_xz(pt, shape))
            elif len(shape) >= 2:
                best = min(best, conv._point_polyline_distance(pt, shape))
            elif shape:
                best = min(best, math.hypot(pt[0] - shape[0][0], pt[1] - shape[0][1]))
        return best

    def nearest(self, pt, elements: list[dict]):
        """(element, metres) of the feature nearest `pt`, or (None, inf)."""
        best = (None, math.inf)
        for el in elements:
            d = self.distance(pt, el)
            if d < best[1]:
                best = (el, d)
        return best


def _is_practice(el: dict) -> bool:
    tags = el.get("tags", {})
    return bool(conv.PRACTICE_PATTERN.search(str(tags.get("name", "")))) or tags.get("practice") == "yes"


def _direction(from_pt, to_pt) -> str:
    return osm_checks.compass_name(osm_checks.bearing_deg(from_pt, to_pt))


def _expected_holes(scorecard: dict | None, config: dict) -> tuple[int | None, str]:
    if scorecard and scorecard.get("holes"):
        return len(scorecard["holes"]), "the scorecard"
    if config.get("holes"):
        return len(config["holes"]), "the course config"
    return None, ""


def _config_specs(config: dict) -> list[dict]:
    """The course config's `holes` list, numbered in play order."""
    return [{**spec, "number": n} for n, spec in enumerate(config.get("holes") or [], start=1)]


def audit_elements(elements: list[dict], course_el: dict, course_id: str, config: dict,
                   scorecard: dict | None, fences: list[dict] | None = None,
                   driving_ranges: list[dict] | None = None) -> osm_checks.Findings:
    """
    Every gap in one course's raw OSM golf features (after a ref-prefix
    selection), and in its `fences` (with the `driving_ranges` beside them).
    """
    opts = {**DEFAULT_AUDIT, **config.get("audit", {})}
    findings = osm_checks.Findings()
    course = _Course(elements, course_el)
    specs = _config_specs(config)
    spec_by_ref = {}
    for spec in specs:
        for key in ("line", "tee", "green"):
            if key in spec:
                spec_by_ref.setdefault(conv._parse_osm_ref(spec[key]), spec)

    used_greens: set = set()
    used_tees: set = set()
    used_fairways: set = set()
    _skip_second_layout(findings, course, opts, used_greens, used_tees, used_fairways)
    if specs:
        # The config's hole list numbers the course; the import drops every other line.
        for line in [l for l in course.lines if conv._element_key(l) not in spec_by_ref]:
            findings.add("info", "LINE_UNUSED",
                         f"hole line {_ref(line)} is not in the course config's hole list, so the import "
                         f"leaves it out", link=osm_link(line))
        course.lines = [l for l in course.lines if conv._element_key(l) in spec_by_ref]
    _audit_course(findings, course_el, course_id, scorecard, course, config)
    refs: dict[int, list[dict]] = {}
    for line in course.lines:
        _audit_line(findings, line, course, opts, spec_by_ref, used_greens, used_tees, used_fairways, refs)
    _audit_refs(findings, refs, specs)
    no_dist = [line for line in course.lines
               if not any(key == "dist" or key.startswith("dist:") for key in line.get("tags", {}))]
    if no_dist:
        named = sorted(conv._parse_hole_num(line.get("tags", {})) or 0 for line in no_dist)
        which = _holes_text(n for n in named if n) if all(named) else f"{len(no_dist)} of them"
        findings.add("info", "LINE_NO_DIST",
                     f"hole lines without a dist tag: {which}. dist:<tee colour>=<metres> from the "
                     f"scorecard lets the length check use OSM itself")
    _audit_lonely_greens(findings, course, opts, specs, spec_by_ref, used_greens, used_tees,
                         used_fairways, _expected_holes(scorecard, config)[0])
    _audit_config_guesses(findings, course, specs)
    for tee in course.tees:
        if conv._element_key(tee) not in used_tees:
            centre = conv._centroid(conv._element_geom(tee))
            findings.add("info", "TEE_UNUSED",
                         f"tee {_ref(tee)} at {_latlon(centre)} starts no hole line: if it is a hole's "
                         f"tee, draw that hole's {_HOLE_TAG} line from it", link=osm_link(tee))
    _audit_fences(findings, fences or [], driving_ranges or [], course, config)
    if course.lines and not course.pins:
        findings.add("info", "PIN_MISSING", "no golf=pin anywhere: every pin goes in the middle of its green")
    return findings


def _audit_fences(findings, fences, driving_ranges, course, config):
    fence_config = {**conv.DEFAULT_CONFIG["fence"], **config.get("fence", {})}
    for fence in fences:
        height, from_where = conv.fence_height(fence, driving_ranges, *course.origin, fence_config)
        if from_where == "tag":
            continue
        why = "a driving range net beside the range" if from_where == "driving_range" else "the default"
        findings.add("info", "FENCE_NO_HEIGHT",
                     f"fence {_ref(fence)} has no height tag, so it stands {height:g} m ({why}): tag "
                     f"height=<metres>", link=osm_link(fence))


def _audit_course(findings, course_el, course_id, scorecard, course, config):
    if not course_el.get("tags", {}).get("name"):
        findings.add("info", "COURSE_NAME_MISSING",
                     f"the course boundary {_ref(course_el)} has no name", link=osm_link(course_el))
    if not scorecard:
        findings.add("warn", "SCORECARD_MISSING",
                     f"no entry for {course_id} in testdata/scorecards.json, so hole count, pars, lengths "
                     f"and directions are unchecked. Look up the club's scorecard (par and metres per "
                     f"hole, and from which tees) and its course map (which way each hole plays), and add "
                     f"them there")
    else:
        holes = scorecard.get("holes", {})
        no_length = [n for n, h in holes.items() if not h.get("metres")]
        no_bearing = [n for n, h in holes.items() if h.get("bearing_deg") is None and not h.get("faces")]
        if no_length:
            findings.add("warn", "SCORECARD_NO_LENGTHS",
                         f"the scorecard has no length for hole(s) {_holes_text(no_length)}, so a hole "
                         f"that imports too short goes unnoticed: find the lengths (club website, "
                         f"scorecard photo, golf apps' course pages)")
        if no_bearing:
            findings.add("info", "SCORECARD_NO_DIRECTIONS",
                         f"the scorecard has no direction for hole(s) {_holes_text(no_bearing)}: measure "
                         f"them on the club's course map (bearing_deg, 0 north, 90 east)")
    expected, source = _expected_holes(scorecard, config)
    with_lines = len(course.lines)
    if expected is not None and with_lines < expected:
        findings.add("warn", "HOLE_LINES_MISSING",
                     f"{source} says {expected} holes but OSM has {with_lines} {_HOLE_TAG} line(s); "
                     f"the holes without one are listed below")
    elif expected is None and not course.lines:
        findings.add("warn", "HOLE_LINES_MISSING",
                     f"OSM has no {_HOLE_TAG} lines and nothing says how many holes there are")


def _holes_text(numbers) -> str:
    numbers = sorted(int(n) for n in numbers)
    return ", ".join(str(n) for n in numbers)


def _line_label(line: dict, spec_by_ref: dict) -> str:
    number = conv._parse_hole_num(line.get("tags", {}))
    spec = spec_by_ref.get(conv._element_key(line))
    if number is None and spec:
        number = spec["number"]
    return f"hole {number}'s line {_ref(line)}" if number is not None else f"hole line {_ref(line)}"


def _audit_line(findings, line, course, opts, spec_by_ref, used_greens, used_tees, used_fairways, refs):
    tags = line.get("tags", {})
    pts = course.xz(line)
    label = _line_label(line, spec_by_ref)
    spec = spec_by_ref.get(conv._element_key(line))
    number = conv._parse_hole_num(tags)
    if number is not None:
        refs.setdefault(number, []).append(line)
    hole = number if number is not None else (spec["number"] if spec else None)

    green_start, d_start = course.nearest(pts[0], course.greens)
    green_end, d_end = course.nearest(pts[-1], course.greens)
    reach = opts["green_reach_m"]
    if min(d_start, d_end) > reach:
        # No green at either end: guess the green end from the tees.
        _, tee_start = course.nearest(pts[0], course.tees)
        _, tee_end = course.nearest(pts[-1], course.tees)
        green_pt = pts[0] if tee_end < tee_start else pts[-1]
        tee_pt = pts[-1] if tee_end < tee_start else pts[0]
        where = course.latlon(green_pt)
        findings.add("warn", "GREEN_MISSING",
                     f"{label} ends at {_latlon(where)} with no golf=green within {reach:.0f} m: draw the "
                     f"green there (the import makes a round one)", hole, link=edit_link(*where))
    else:
        reversed_line = d_start < d_end
        green, green_pt, tee_pt = ((green_start, pts[0], pts[-1]) if reversed_line
                                   else (green_end, pts[-1], pts[0]))
        used_greens.add(conv._element_key(green))
        if reversed_line:
            findings.add("info", "HOLE_LINE_REVERSED",
                         f"{label} is drawn from the green to the tee: reverse the way in OSM (golf=hole "
                         f"lines run tee to green; the import turns it round for now)", hole, link=osm_link(line))

    for end, end_name in ((tee_pt, "tee"), (green_pt, "green")):
        outside = course.outside_by(end)
        if outside > opts["boundary_slack_m"]:
            where = course.latlon(end)
            findings.add("warn", "LINE_OUTSIDE_COURSE",
                         f"{label}'s {end_name} end at {_latlon(where)} is {outside:.0f} m outside the "
                         f"course boundary {_ref(course.boundary_el)}, so what is mapped there (the "
                         f"{end_name}, water, bunkers) is left out of the import: extend the boundary "
                         f"(leisure=golf_course) to take it in", hole, link=edit_link(*where))
    _use_along(course, pts, course.tees, opts["tee_reach_m"], used_tees)
    _use_along(course, pts, course.fairways, 0.0, used_fairways)
    tee, d_tee = course.nearest(tee_pt, course.tees)
    if tee is None or d_tee > opts["tee_reach_m"]:
        where = course.latlon(tee_pt)
        findings.add("info", "TEE_MISSING",
                     f"{label} starts at {_latlon(where)} with no golf=tee there (the hole plays from the "
                     f"line's start): draw the teeing ground, an area tagged golf=tee", hole,
                     link=edit_link(*where))

    par = conv._int_tag(tags.get("par"))
    if par is None:
        if spec and "par" in spec:
            findings.add("info", "LINE_NO_PAR", f"{label} has no par tag; the course config gives par "
                         f"{spec['par']}: tag par={spec['par']} in OSM", hole, link=osm_link(line))
        else:
            length = conv._polyline_length(pts)
            guess = 3 if length < 200 else (4 if length < 430 else 5)
            findings.add("warn", "LINE_NO_PAR", f"{label} has no par tag, so par {guess} was guessed from "
                         f"its {length:.0f} m: tag par= from the scorecard", hole, link=osm_link(line))
    if number is None:
        how = (f"numbered {spec['number']} by the course config" if spec
               else "numbered by the import from where it lies")
        findings.add("warn" if not spec else "info", "LINE_NO_REF",
                     f"{label} has no ref, so it was {how}: tag ref=<hole number>", hole, link=osm_link(line))
    if (par or 0) >= 4 and not _along(course, pts, course.fairways, 0.0):
        findings.add("info", "FAIRWAY_MISSING",
                     f"{label} crosses no golf=fairway: draw the fairway along it", hole, link=osm_link(line))


def _samples(pts, spacing: float = 5.0):
    return osm_checks.resample(pts, spacing) if len(pts) >= 2 else pts


def _along(course, pts, elements, reach: float) -> list[dict]:
    """The features within `reach` of any point along the polyline `pts`."""
    samples = _samples(pts)
    return [el for el in elements if any(course.distance(p, el) <= reach for p in samples)]


def _use_along(course, pts, elements, reach, used: set):
    used.update(conv._element_key(el) for el in _along(course, pts, elements, reach))


def _skip_second_layout(findings, course, opts, used_greens, used_tees, used_fairways):
    """
    Leave out the lines of a second layout in the same boundary (Augusta's
    Par 3 Course inside Augusta National) the way the import does, with the
    greens, tees and fairways along them.
    """
    kept = {conv._element_key(el) for el in conv._resolve_duplicate_hole_lines(course.lines)}
    other = [line for line in course.lines if conv._element_key(line) not in kept]
    if not other:
        return
    for line in other:
        pts = course.xz(line)
        _use_along(course, pts, course.greens, opts["green_reach_m"], used_greens)
        _use_along(course, pts, course.tees, opts["tee_reach_m"], used_tees)
        _use_along(course, pts, course.fairways, 0.0, used_fairways)
    course.lines = [line for line in course.lines if conv._element_key(line) in kept]
    findings.add("info", "SECOND_LAYOUT",
                 f"{len(other)} hole line(s) belong to another layout inside this boundary and are left "
                 f"out, with their greens and tees ({', '.join(_ref(l) for l in other[:3])}"
                 f"{', ...' if len(other) > 3 else ''})")


def _audit_refs(findings, refs: dict, specs: list):
    if specs or not refs:
        return  # the config numbers the holes
    for number, lines in sorted(refs.items()):
        if len(lines) > 1:
            findings.add("info", "REF_SHARED",
                         f"{len(lines)} hole lines have ref={number} ({', '.join(_ref(l) for l in lines)}): "
                         f"one per tee set is fine, otherwise give each its own ref", number)
    missing = [n for n in range(1, max(refs) + 1) if n not in refs]
    if missing:
        findings.add("warn", "REF_GAP", f"no hole line has ref {_holes_text(missing)}: that hole has no "
                     f"line, or its line has no ref (see LINE_NO_REF)")


def _audit_lonely_greens(findings, course, opts, specs, spec_by_ref, used_greens, used_tees,
                         used_fairways, expected):
    lonely = [g for g in course.greens
              if conv._element_key(g) not in used_greens and not _is_practice(g)]
    all_lines_there = expected is not None and len(course.lines) >= expected
    lo, hi = opts["candidate_tee_m"]
    for green in lonely:
        centre_ll = conv._centroid(conv._element_geom(green))
        centre = conv._latlon_to_xz(*centre_ll, *course.origin)
        spec = spec_by_ref.get(conv._element_key(green))
        hole = spec["number"] if spec else None
        name = f"hole {hole}'s green" if hole else "green"
        if all_lines_there and not spec:
            findings.add("info", "GREEN_UNUSED",
                         f"{name} {_ref(green)} at {_latlon(centre_ll)} ends no hole line, but every hole "
                         f"has one: if it is a practice green, name it (e.g. 'Putting green') so the "
                         f"import skips it", link=osm_link(green))
            continue
        hints = []
        tees = sorted(((course.distance(centre, t), t) for t in course.tees
                       if conv._element_key(t) not in used_tees), key=lambda dt: dt[0])
        for d, tee in [dt for dt in tees if lo <= dt[0] <= hi][:2]:
            tee_centre = conv._latlon_to_xz(*conv._centroid(conv._element_geom(tee)), *course.origin)
            hints.append(f"the unused tee {_ref(tee)} is {d:.0f} m to the {_direction(centre, tee_centre)}")
        for fairway in course.fairways:
            if conv._element_key(fairway) in used_fairways:
                continue
            if course.distance(centre, fairway) <= opts["fairway_reach_m"]:
                far = max(course.xz(fairway), key=lambda p: math.hypot(p[0] - centre[0], p[1] - centre[1]))
                hints.append(f"fairway {_ref(fairway)} leads to it from the {_direction(centre, far)}, so "
                             f"the tee is beyond its far end at {_latlon(course.latlon(far))}, often well "
                             f"beyond")
        if not hints:
            hints.append("no unused tee or fairway leads to it, so its tee isn't mapped at all: find it on "
                         "the club's course map or aerial imagery")
        stop_gap = ""
        if spec:
            stop_gap = " The course config pairs it for now, but a config pairing can't know the real tee."
        findings.add("warn", "HOLE_LINE_MISSING",
                     f"{name} {_ref(green)} at {_latlon(centre_ll)} has no {_HOLE_TAG} line, so its tee, "
                     f"length and direction are guessed. Draw a {_HOLE_TAG} line from the middle of the "
                     f"tee to the middle of this green, tagged ref=<hole number> and par=<par>. "
                     f"{_sentence('; '.join(hints))}.{stop_gap}", hole, link=edit_link(*centre_ll))


def _sentence(text: str) -> str:
    return text[:1].upper() + text[1:]


def _audit_config_guesses(findings, course, specs):
    by_key = {conv._element_key(el): el for el in course.tees + course.fairways}
    for spec in specs:
        if "tee" not in spec:
            continue
        tee = by_key.get(conv._parse_osm_ref(spec["tee"]))
        if tee is not None and tee.get("tags", {}).get("golf") == "fairway":
            findings.add("warn", "TEE_GUESSED",
                         f"hole {spec['number']}'s tee is the far end of fairway {_ref(tee)}, a guess: real "
                         f"tees usually sit well behind the fairway, so the hole plays short. Map its "
                         f"golf=tee and {_HOLE_TAG} line, then drop it from the course config",
                         spec["number"], link=osm_link(tee))


def todo_markdown(course_name: str, course_el: dict, findings: osm_checks.Findings) -> str:
    """The audit as a checklist of OSM edits, worst first."""
    items = [i for i in findings.items if i["level"] in ("warn", "info")]
    lines = [f"# OpenStreetMap to-do: {course_name}", "",
             f"Course boundary: [{_ref(course_el)}]({osm_link(course_el)}). Edit in OSM, then re-import; "
             f"the import's audit should then come back clean.", ""]
    if not items:
        return "\n".join(lines + ["Nothing missing.", ""])
    for level, title in (("warn", "The import guessed (fix these)"), ("info", "Nice to have")):
        chosen = [i for i in items if i["level"] == level]
        if not chosen:
            continue
        lines += [f"## {title}", ""]
        for i in sorted(chosen, key=lambda i: (i["hole"] or 0, i["code"])):
            link = f" [map]({i['link']})" if i.get("link") else ""
            lines.append(f"- [ ] **{i['code']}**: {i['message']}{link}")
        lines.append("")
    return "\n".join(lines)


def audit_from_osm(course_ref: str, course_id: str, config: dict,
                   scorecard: dict | None) -> tuple[osm_checks.Findings, dict] | None:
    """
    Fetch (or read from the cache) the features of the course `course_ref`
    ("way/123", as a course world's source names it) and audit them; None if
    OSM has nothing for it.
    """
    kind, _, oid = course_ref.partition("/")
    found = conv._fetch_elements_by_ref([(kind, int(oid))])
    if not found:
        return None
    course_el = found[0]
    elements, _, _ = conv._fetch_elements(course_el, float(config["tree"]["outside_reach_m"]))
    if not elements:
        return None
    fences, ranges = conv._fetch_fences(course_el, config["fence"])
    origin = conv._centroid([p for el in elements for p in conv._element_geom(el)])
    elements = conv.select_course_by_ref_prefix(elements, config.get("ref_prefix", ""), *origin)
    return audit_elements(elements, course_el, course_id, config, scorecard, fences, ranges), course_el
