"""
osm_outline.py — OSM green, bunker, water and fairway polygons as the game's
outline zones: simplified, grown to a least size, streams as strips, close
bunkers joined into one, and fairways generated where OSM has none.

Points are (x, z) metres. No dependencies beyond the standard library.
"""

import math


def polygon_area(pts) -> float:
    """Signed area (shoelace); positive when the points run counter-clockwise."""
    twice = 0.0
    for i, (x0, z0) in enumerate(pts):
        x1, z1 = pts[(i + 1) % len(pts)]
        twice += x0 * z1 - x1 * z0
    return twice * 0.5


def centroid(pts):
    """The area centroid of a ring (its mean point when it has no area)."""
    area = polygon_area(pts)
    if abs(area) < 1e-9:
        return (sum(p[0] for p in pts) / len(pts), sum(p[1] for p in pts) / len(pts))
    cx = cz = 0.0
    for i, (x0, z0) in enumerate(pts):
        x1, z1 = pts[(i + 1) % len(pts)]
        cross = x0 * z1 - x1 * z0
        cx += (x0 + x1) * cross
        cz += (z0 + z1) * cross
    return (cx / (6.0 * area), cz / (6.0 * area))


def point_in_polygon(pt, poly) -> bool:
    """Even-odd: a ray along +x crosses the edge an odd number of times (as the game tests)."""
    x, z = pt
    inside = False
    j = len(poly) - 1
    for i in range(len(poly)):
        xi, zi = poly[i]
        xj, zj = poly[j]
        if (zi > z) != (zj > z) and x < xi + (z - zi) * (xj - xi) / (zj - zi):
            inside = not inside
        j = i
    return inside


def segment_distance(p, a, b) -> float:
    ax, az = b[0] - a[0], b[1] - a[1]
    length2 = ax * ax + az * az
    t = 0.0 if length2 == 0.0 else max(0.0, min(1.0, ((p[0] - a[0]) * ax + (p[1] - a[1]) * az) / length2))
    return math.hypot(p[0] - (a[0] + ax * t), p[1] - (a[1] + az * t))


def edge_distance(pt, poly) -> float:
    """How far `pt` is from the ring's edge, inside or out."""
    return min(segment_distance(pt, poly[i], poly[(i + 1) % len(poly)]) for i in range(len(poly)))


def rings_gap(a, b) -> float:
    """The gap between two rings: 0 when they overlap or one holds the other."""
    if point_in_polygon(a[0], b) or point_in_polygon(b[0], a):
        return 0.0
    return min(min(edge_distance(p, b) for p in a), min(edge_distance(p, a) for p in b))


def _simplify_open(pts, tolerance):
    """Douglas–Peucker on an open polyline, keeping both ends."""
    if len(pts) < 3:
        return list(pts)
    worst, index = 0.0, 0
    for i in range(1, len(pts) - 1):
        d = segment_distance(pts[i], pts[0], pts[-1])
        if d > worst:
            worst, index = d, i
    if worst <= tolerance:
        return [pts[0], pts[-1]]
    return _simplify_open(pts[:index + 1], tolerance)[:-1] + _simplify_open(pts[index:], tolerance)


def simplify(ring, tolerance: float):
    """The ring with every point closer than `tolerance` to the line through its neighbours dropped (at least 3 kept)."""
    if len(ring) <= 3 or tolerance <= 0.0:
        return list(ring)
    # Cut the ring at its two farthest-apart points and simplify each half.
    first = 0
    far = max(range(len(ring)), key=lambda i: math.dist(ring[0], ring[i]))
    halves = ring[first:far + 1], ring[far:] + ring[:1]
    out = _simplify_open(halves[0], tolerance)[:-1] + _simplify_open(halves[1], tolerance)[:-1]
    return out if len(out) >= 3 else list(ring)


def with_min_area(ring, min_area: float):
    """The ring grown about its centroid to at least `min_area` square metres, its shape kept."""
    area = abs(polygon_area(ring))
    if area >= min_area or area <= 0.0:
        return list(ring)
    cx, cz = centroid(ring)
    k = math.sqrt(min_area / area)
    return [(cx + (x - cx) * k, cz + (z - cz) * k) for x, z in ring]


def circle(center, radius: float, points: int = 32):
    return [(center[0] + radius * math.cos(2.0 * math.pi * i / points),
             center[1] + radius * math.sin(2.0 * math.pi * i / points)) for i in range(points)]


def _offset_sides(line, half_widths):
    """Left and right edges of a strip along `line`, `half_widths[i]` either side at point i."""
    left, right = [], []
    for i, (x, z) in enumerate(line):
        before = line[max(0, i - 1)]
        after = line[min(len(line) - 1, i + 1)]
        dx, dz = after[0] - before[0], after[1] - before[1]
        length = math.hypot(dx, dz) or 1.0
        nx, nz = -dz / length, dx / length
        w = half_widths[i]
        left.append((x + nx * w, z + nz * w))
        right.append((x - nx * w, z - nz * w))
    return left, right


def strip(line, half_widths):
    """The outline of a strip along `line`: its left edge out, its right edge back."""
    left, right = _offset_sides(line, half_widths)
    return left + right[::-1]


def resample(line, step: float):
    """Points every `step` metres along `line`, its ends kept."""
    out = [line[0]]
    carry = 0.0
    for a, b in zip(line, line[1:]):
        seg = math.dist(a, b)
        d = step - carry
        while d < seg:
            t = d / seg
            out.append((a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t))
            d += step
        carry = (carry + seg) % step
    if out[-1] != line[-1]:
        out.append(line[-1])
    return out


def stream_pieces(line, half_width: float, piece_length: float):
    """A stream mapped as a line, as strips `half_width` either side of it, each
    about `piece_length` long and running a half width into the next, so they
    overlap and stay one water."""
    pts = resample(line, min(2.0, piece_length / 4.0))
    step = math.dist(pts[0], pts[1]) if len(pts) > 1 else 1.0
    per_piece = max(2, int(round(piece_length / step)))
    reach = max(1, int(math.ceil(half_width / step)))
    pieces = []
    for start in range(0, len(pts) - 1, per_piece):
        piece = pts[max(0, start - reach):min(len(pts), start + per_piece + reach + 1)]
        if len(piece) >= 2:
            pieces.append(strip(piece, [half_width] * len(piece)))
    return pieces


# ── joining close bunkers ─────────────────────────────────────────────────────

def _trace_outer(filled, columns, rows):
    """The longest closed boundary round the filled cells, as grid corners (column, row)."""
    def at(c, r):
        return 0 <= c < columns and 0 <= r < rows and filled[r][c]
    # Directed boundary edges with the filled cell on their left.
    nxt = {}
    for r in range(rows):
        for c in range(columns):
            if not filled[r][c]:
                continue
            if not at(c, r - 1):
                nxt[(c + 1, r)] = nxt.get((c + 1, r), []) + [(c, r)]
            if not at(c + 1, r):
                nxt[(c + 1, r + 1)] = nxt.get((c + 1, r + 1), []) + [(c + 1, r)]
            if not at(c, r + 1):
                nxt[(c, r + 1)] = nxt.get((c, r + 1), []) + [(c + 1, r + 1)]
            if not at(c - 1, r):
                nxt[(c, r)] = nxt.get((c, r), []) + [(c, r + 1)]
    loops = []
    while nxt:
        start = next(iter(nxt))
        loop, here = [start], start
        while True:
            ends = nxt[here]
            to = ends.pop()
            if not ends:
                del nxt[here]
            here = to
            if here == start:
                break
            loop.append(here)
            if here not in nxt:
                break
        loops.append(loop)
    return max(loops, key=lambda loop: abs(polygon_area(loop))) if loops else []


def join_rings(rings, cell: float, gap: float, tolerance: float):
    """One outline round `rings`, their gaps under `gap` closed: the rings are
    drawn on a grid of `cell`, grown by half the gap and shrunk back
    (closing), and the outer edge traced and simplified."""
    xs = [p[0] for ring in rings for p in ring]
    zs = [p[1] for ring in rings for p in ring]
    pad = gap + 2.0 * cell
    x0, z0 = min(xs) - pad, min(zs) - pad
    columns = int(math.ceil((max(xs) + pad - x0) / cell))
    rows = int(math.ceil((max(zs) + pad - z0) / cell))
    filled = [[any(point_in_polygon((x0 + (c + 0.5) * cell, z0 + (r + 0.5) * cell), ring) for ring in rings)
               for c in range(columns)] for r in range(rows)]
    reach = int(math.ceil(0.5 * gap / cell))
    disk = [(dc, dr) for dr in range(-reach, reach + 1) for dc in range(-reach, reach + 1)
            if dc * dc + dr * dr <= reach * reach]

    def morph(grid, grow):
        out = [[False] * columns for _ in range(rows)]
        for r in range(rows):
            for c in range(columns):
                hits = (grid[r + dr][c + dc] if 0 <= r + dr < rows and 0 <= c + dc < columns else False
                        for dc, dr in disk)
                out[r][c] = any(hits) if grow else all(hits)
        return out

    closed = morph(morph(filled, True), False)
    corners = _trace_outer(closed, columns, rows)
    if len(corners) < 3:
        return None
    ring = [(x0 + c * cell, z0 + r * cell) for c, r in corners]
    return simplify(ring, max(tolerance, cell))


def join_close(rings, gap: float, cell: float, tolerance: float):
    """`rings` with every group closer than `gap` to each other joined into
    one (join_rings); a ring with none that close is kept as it is."""
    group = list(range(len(rings)))

    def root(i):
        while group[i] != i:
            i = group[i]
        return i

    for i in range(len(rings)):
        for j in range(i + 1, len(rings)):
            if root(i) != root(j) and rings_gap(rings[i], rings[j]) < gap:
                group[root(j)] = root(i)
    out = []
    for i in range(len(rings)):
        if root(i) != i:
            continue
        members = [rings[k] for k in range(len(rings)) if root(k) == i]
        joined = join_rings(members, cell, gap, tolerance) if len(members) > 1 else None
        out += [joined] if joined else members
    return out


# ── generated fairways ────────────────────────────────────────────────────────

def generated_fairway(line, par: int, design: dict):
    """A fairway along a hole's `line` (tee to pin) from real design numbers
    (`design`, the config's `generated_fairway`, with its sources): it starts
    past the carry from the tees, as wide as the landing area for its par,
    narrowing from `narrow_from_meters` to its approach width at the pin. A
    par 3 gets only its approach. None when the hole is too short for one."""
    length = sum(math.dist(a, b) for a, b in zip(line, line[1:]))
    if par <= 3:
        start = length - float(design["par3_approach_meters"])
    else:
        start = float(design["start_meters"])
    if length - start < 10.0:
        return None
    pts = resample(line, 5.0)
    along, kept = 0.0, []
    for i, p in enumerate(pts):
        if i:
            along += math.dist(pts[i - 1], p)
        if along >= start:
            kept.append((p, along))
    if len(kept) < 2:
        return None
    approach = 0.5 * float(design["approach_width_meters"])
    if par <= 3:
        return strip([p for p, _ in kept], [approach] * len(kept))
    landing = 0.5 * float(design["landing_width_meters"].get(str(par), design["landing_width_meters"]["4"]))
    # A short hole narrows from half way along its fairway.
    narrow_from = min(float(design["narrow_from_meters"]), 0.5 * (start + length))
    widths = [landing + (approach - landing) * max(0.0, d - narrow_from) / (length - narrow_from) for _, d in kept]
    return strip([p for p, _ in kept], widths)


def zone_json(zone_type: str, ring, offset=(0.0, 0.0), digits: int = 2) -> dict:
    """A hole file's zone: its outline relative to `offset` (the tee)."""
    return {"type": zone_type,
            "outline": [[round(x - offset[0], digits), round(z - offset[1], digits)] for x, z in ring]}
