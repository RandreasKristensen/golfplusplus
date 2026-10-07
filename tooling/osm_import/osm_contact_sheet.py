"""
osm_contact_sheet.py — A top-down SVG of an imported course, to hold beside
the club's published course map.

North is up. Every hole's line of play is drawn from its tee (a numbered
disc, like a club's course map) to its pin (the number again), over its
greens, bunkers and water as the game will see them (the ellipses, not the
OSM polygons). Where the scorecard gives a direction, a dashed red arrow from
the tee shows where the hole should point; a hole drawn along its arrow faces
the right way. Thin grey dashes walk from each green to the next tee, and the
import's errors and warnings are listed beside the map.

Stdlib only; reads the placed holes from osm_checks.placed_holes.
"""

import math
from xml.sax.saxutils import escape

import osm_checks

_ZONE_STYLE = {
    "green": 'fill="#7bc96f" stroke="#2f7a2a" stroke-width="0.6"',
    "bunker": 'fill="#efe2b0" stroke="#b49a4a" stroke-width="0.5"',
    "water": 'fill="#7fb6e6" stroke="#2f6fa8" stroke-width="0.6"',
}
_LINE_COLOURS = ["#c0392b", "#1f5fa8", "#7d3c98", "#b9770e", "#117a65", "#a93226"]
_PANEL_WIDTH = 520


def _zone_svg(zone, to_px, scale) -> str:
    cx, cy = to_px(zone["center"])
    rx, ry = zone["radii"][0] * scale, zone["radii"][1] * scale
    angle = math.degrees(zone["rotation"])
    style = _ZONE_STYLE.get(zone["type"], 'fill="#ccc"')
    # x/z map straight to SVG x/y (z is south, SVG y is down), so the game's
    # turn about the vertical is the same SVG rotation.
    return (f'<ellipse cx="{cx:.1f}" cy="{cy:.1f}" rx="{rx:.1f}" ry="{ry:.1f}" '
            f'transform="rotate({angle:.1f} {cx:.1f} {cy:.1f})" {style}/>')


def render(course_name: str, placed: list[dict], scorecard: dict | None,
           findings: "osm_checks.Findings | None" = None, width_px: int = 1400) -> str:
    """The contact sheet as an SVG document."""
    xs, zs = [], []
    for p in placed:
        for q in p["line"] + [p["tee"], p["pin"]]:
            xs.append(q[0])
            zs.append(q[1])
        for zone in p["zones"]:
            r = max(zone["radii"])
            xs += [zone["center"][0] - r, zone["center"][0] + r]
            zs += [zone["center"][1] - r, zone["center"][1] + r]
    if not xs:
        xs, zs = [0.0, 100.0], [0.0, 100.0]
    margin = 60.0
    min_x, max_x, min_z, max_z = min(xs) - margin, max(xs) + margin, min(zs) - margin, max(zs) + margin
    scale = width_px / max(max_x - min_x, 1.0)
    height_px = max(int((max_z - min_z) * scale), 400)
    header = 70

    def to_px(q):
        return (q[0] - min_x) * scale, header + (q[1] - min_z) * scale

    holes = (scorecard or {}).get("holes", {})
    out = []
    total_w = width_px + _PANEL_WIDTH
    total_h = height_px + header
    out.append(f'<svg xmlns="http://www.w3.org/2000/svg" width="{total_w}" height="{total_h}" '
               f'viewBox="0 0 {total_w} {total_h}" font-family="Verdana, sans-serif">')
    out.append(f'<rect width="{total_w}" height="{total_h}" fill="#f4f1e6"/>')
    out.append(f'<rect x="0" y="{header}" width="{width_px}" height="{height_px}" fill="#cfe3bd"/>')
    title = escape(course_name)
    out.append(f'<text x="16" y="34" font-size="24" font-weight="bold">{title}</text>')
    out.append(f'<text x="16" y="58" font-size="13" fill="#444">North up. Numbered disc: tee. Number: green. '
               f'Red dashes: the scorecard\'s direction. Grey dashes: walk to the next tee.</text>')

    # Zones first, so lines and labels sit on top.
    for p in placed:
        for zone in sorted(p["zones"], key=lambda z: {"water": 0, "green": 1, "bunker": 2}.get(z["type"], 3)):
            out.append(_zone_svg(zone, to_px, scale))

    # Walks from each green to the next tee.
    for a, b in zip(placed, placed[1:]):
        (x1, y1), (x2, y2) = to_px(a["pin"]), to_px(b["tee"])
        out.append(f'<line x1="{x1:.1f}" y1="{y1:.1f}" x2="{x2:.1f}" y2="{y2:.1f}" stroke="#777" '
                   f'stroke-width="1.2" stroke-dasharray="3 5"/>')

    for p in placed:
        colour = _LINE_COLOURS[(p["number"] - 1) % len(_LINE_COLOURS)]
        entry = holes.get(str(p["number"]), {})
        expected = osm_checks.expected_bearing(entry)
        if expected is not None:
            reach = entry.get("metres") or math.dist(p["tee"], p["pin"])
            end = (p["tee"][0] + math.sin(math.radians(expected)) * reach,
                   p["tee"][1] - math.cos(math.radians(expected)) * reach)
            (x1, y1), (x2, y2) = to_px(p["tee"]), to_px(end)
            out.append(f'<line x1="{x1:.1f}" y1="{y1:.1f}" x2="{x2:.1f}" y2="{y2:.1f}" stroke="#e03030" '
                       f'stroke-width="2" stroke-dasharray="8 6" opacity="0.8"/>')
            out.append(f'<circle cx="{x2:.1f}" cy="{y2:.1f}" r="4" fill="none" stroke="#e03030" stroke-width="2"/>')
        points = " ".join(f"{x:.1f},{y:.1f}" for x, y in map(to_px, p["line"] or [p["tee"], p["pin"]]))
        out.append(f'<polyline points="{points}" fill="none" stroke="{colour}" stroke-width="2.5"/>')
        px, py = to_px(p["pin"])
        out.append(f'<circle cx="{px:.1f}" cy="{py:.1f}" r="3" fill="#111"/>')
        out.append(f'<text x="{px + 6:.1f}" y="{py - 6:.1f}" font-size="15" font-weight="bold" '
                   f'fill="{colour}" stroke="#fff" stroke-width="3" paint-order="stroke">{p["number"]}</text>')
        tx, ty = to_px(p["tee"])
        out.append(f'<circle cx="{tx:.1f}" cy="{ty:.1f}" r="12" fill="{colour}" stroke="#fff" stroke-width="2"/>')
        out.append(f'<text x="{tx:.1f}" y="{ty + 5:.1f}" font-size="13" font-weight="bold" fill="#fff" '
                   f'text-anchor="middle">{p["number"]}</text>')

    # North arrow and a 100 m scale bar.
    nx, ny = width_px - 40, header + 50
    out.append(f'<path d="M {nx} {ny - 30} L {nx - 10} {ny} L {nx + 10} {ny} Z" fill="#222"/>')
    out.append(f'<text x="{nx}" y="{ny + 18}" font-size="14" text-anchor="middle">N</text>')
    bar = 100.0 * scale
    bx, by = 20, header + height_px - 20
    out.append(f'<line x1="{bx}" y1="{by}" x2="{bx + bar:.1f}" y2="{by}" stroke="#222" stroke-width="3"/>')
    out.append(f'<text x="{bx}" y="{by - 6}" font-size="12">100 m</text>')

    # The findings and a per-hole table beside the map.
    px0, line_y = width_px + 16, header + 10
    out.append(f'<text x="{px0}" y="{line_y + 10}" font-size="15" font-weight="bold">Holes</text>')
    line_y += 28
    for row in osm_checks.hole_table(placed, scorecard):
        out.append(f'<text x="{px0}" y="{line_y}" font-size="10.5" font-family="Consolas, monospace" '
                   f'xml:space="preserve">{escape(row)}</text>')
        line_y += 14
    if findings is not None:
        line_y += 16
        out.append(f'<text x="{px0}" y="{line_y}" font-size="15" font-weight="bold">'
                   f'{findings.count("error")} error(s), {findings.count("warn")} warning(s)</text>')
        line_y += 20
        for text in findings.lines(("error", "warn")):
            if line_y > total_h - 14:
                out.append(f'<text x="{px0}" y="{line_y}" font-size="11">…</text>')
                break
            colour = "#b00020" if text.startswith("ERROR") else "#8a5a00"
            for chunk in _wrap(text, 80):
                out.append(f'<text x="{px0}" y="{line_y}" font-size="11" fill="{colour}">{escape(chunk)}</text>')
                line_y += 14
    out.append("</svg>")
    return "\n".join(out) + "\n"


def _wrap(text: str, width: int) -> list[str]:
    words, lines, current = text.split(), [], ""
    for word in words:
        if current and len(current) + 1 + len(word) > width:
            lines.append(current)
            current = "    " + word
        else:
            current = f"{current} {word}" if current else word
    return lines + [current] if current else lines
