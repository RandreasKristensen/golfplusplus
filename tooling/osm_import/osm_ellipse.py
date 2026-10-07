"""
osm_ellipse.py — Fit OSM green, bunker and water polygons with ellipses.

The game's material zones are ellipses: a centre, two semi-axes (`radii`) and
a turn about the vertical (`rotation_degrees`), the same turn a course
world's hole start uses (rotate_about_y in src/physics/vector_math.h): the
first radius lies along (cos r, sin r) in x/z, the second at right angles.

One ellipse is fitted from a polygon's area-weighted second moments, so it has
the polygon's centre, orientation and spread, then scaled so its area is the
polygon's. A shape one ellipse cannot follow (a long curved bunker, a lake with
arms, a creek) is cut in half across its long axis, again and again where the
fit is worst, and each piece gets its own ellipse. How well the result covers the polygon is measured as intersection
over union on a sample grid, and the converter reports poor fits.

Pure functions over (x, z) point lists in metres; no I/O.
"""

import math

# Sample grid resolution for the fit measure: cells across the longer side.
FIT_GRID_CELLS = 64


def polygon_area(pts) -> float:
    """Signed shoelace area; positive for counter-clockwise in x/z."""
    total = 0.0
    for (x0, z0), (x1, z1) in zip(pts, pts[1:] + pts[:1]):
        total += x0 * z1 - x1 * z0
    return total * 0.5


def polygon_moments(pts):
    """
    (area, cx, cz, var_x, var_z, cov_xz) of the polygon's interior, or None
    when it has no area. The variances are per unit area about the centroid.
    """
    if len(pts) < 3:
        return None
    # Shifting to the vertex mean first keeps the sums well conditioned.
    mx = sum(p[0] for p in pts) / len(pts)
    mz = sum(p[1] for p in pts) / len(pts)
    local = [(x - mx, z - mz) for x, z in pts]
    a = sx = sz = sxx = szz = sxz = 0.0
    for (x0, z0), (x1, z1) in zip(local, local[1:] + local[:1]):
        cross = x0 * z1 - x1 * z0
        a += cross
        sx += (x0 + x1) * cross
        sz += (z0 + z1) * cross
        sxx += (x0 * x0 + x0 * x1 + x1 * x1) * cross
        szz += (z0 * z0 + z0 * z1 + z1 * z1) * cross
        sxz += (x0 * z1 + 2 * x0 * z0 + 2 * x1 * z1 + x1 * z0) * cross
    a *= 0.5
    if abs(a) < 1e-9:
        return None
    cx, cz = sx / (6 * a), sz / (6 * a)
    var_x = sxx / (12 * a) - cx * cx
    var_z = szz / (12 * a) - cz * cz
    cov = sxz / (24 * a) - cx * cz
    return abs(a), cx + mx, cz + mz, max(var_x, 0.0), max(var_z, 0.0), cov


def ellipse_from_polygon(pts):
    """
    {"center": (x, z), "radii": (a, b), "rotation": radians} with a >= b, the
    ellipse with the polygon's centroid, principal axes and area; None for a
    polygon with no area.
    """
    moments = polygon_moments(pts)
    if moments is None:
        return None
    area, cx, cz, vx, vz, cov = moments
    half_sum = (vx + vz) * 0.5
    spread = math.hypot((vx - vz) * 0.5, cov)
    major, minor = half_sum + spread, max(half_sum - spread, 0.0)
    rotation = 0.5 * math.atan2(2.0 * cov, vx - vz)
    # A filled ellipse with semi-axes a, b has variances a^2/4 and b^2/4.
    a, b = 2.0 * math.sqrt(major), 2.0 * math.sqrt(minor)
    if a * b > 0.0:
        scale = math.sqrt(area / (math.pi * a * b))
        a, b = a * scale, b * scale
    else:
        a = b = math.sqrt(area / math.pi)
    return {"center": (cx, cz), "radii": (a, b), "rotation": rotation}


def point_in_polygon(pt, poly) -> bool:
    x, z = pt
    inside = False
    j = len(poly) - 1
    for i in range(len(poly)):
        xi, zi = poly[i]
        xj, zj = poly[j]
        if (zi > z) != (zj > z) and x < (xj - xi) * (z - zi) / ((zj - zi) or 1e-12) + xi:
            inside = not inside
        j = i
    return inside


def point_in_ellipse(pt, ellipse) -> bool:
    cx, cz = ellipse["center"]
    a, b = ellipse["radii"]
    c, s = math.cos(ellipse["rotation"]), math.sin(ellipse["rotation"])
    dx, dz = pt[0] - cx, pt[1] - cz
    # The same turn the game uses: rotate_about_y(offset, -rotation).
    u = dx * c + dz * s
    v = -dx * s + dz * c
    return (u / a) ** 2 + (v / b) ** 2 <= 1.0 if a > 0 and b > 0 else False


def ellipse_bounds(ellipse):
    cx, cz = ellipse["center"]
    a, b = ellipse["radii"]
    c, s = abs(math.cos(ellipse["rotation"])), abs(math.sin(ellipse["rotation"]))
    hx = math.sqrt((a * c) ** 2 + (b * s) ** 2)
    hz = math.sqrt((a * s) ** 2 + (b * c) ** 2)
    return cx - hx, cz - hz, cx + hx, cz + hz


def fit_iou(pts, ellipses, cells: int = FIT_GRID_CELLS) -> float:
    """Intersection over union of the polygon and the union of the ellipses."""
    if len(pts) < 3 or not ellipses:
        return 0.0
    boxes = [ellipse_bounds(e) for e in ellipses]
    min_x = min([p[0] for p in pts] + [b[0] for b in boxes])
    min_z = min([p[1] for p in pts] + [b[1] for b in boxes])
    max_x = max([p[0] for p in pts] + [b[2] for b in boxes])
    max_z = max([p[1] for p in pts] + [b[3] for b in boxes])
    step = max(max_x - min_x, max_z - min_z) / cells
    if step <= 0.0:
        return 0.0
    both = either = 0
    z = min_z + step * 0.5
    while z < max_z:
        x = min_x + step * 0.5
        while x < max_x:
            in_poly = point_in_polygon((x, z), pts)
            in_fit = any(point_in_ellipse((x, z), e) for e in ellipses)
            both += in_poly and in_fit
            either += in_poly or in_fit
            x += step
        z += step
    return both / either if either else 0.0


def clip_half_plane(pts, normal, offset):
    """Sutherland-Hodgman: the part of the polygon where dot(p, normal) <= offset."""
    out = []
    for i, cur in enumerate(pts):
        prev = pts[i - 1]
        d_cur = cur[0] * normal[0] + cur[1] * normal[1] - offset
        d_prev = prev[0] * normal[0] + prev[1] * normal[1] - offset
        if (d_cur <= 0) != (d_prev <= 0):
            t = d_prev / (d_prev - d_cur)
            out.append((prev[0] + (cur[0] - prev[0]) * t, prev[1] + (cur[1] - prev[1]) * t))
        if d_cur <= 0:
            out.append(cur)
    return out


def _halves(pts, ellipse):
    """The polygon cut in two across the long axis of its fitted ellipse, through its centre."""
    axis = (math.cos(ellipse["rotation"]), math.sin(ellipse["rotation"]))
    middle = ellipse["center"][0] * axis[0] + ellipse["center"][1] * axis[1]
    return [clip_half_plane(pts, axis, middle), clip_half_plane(pts, (-axis[0], -axis[1]), -middle)]


def _resample_line(pts, count):
    """`count` points evenly spaced along a polyline, ends included."""
    lengths = [0.0]
    for a, b in zip(pts, pts[1:]):
        lengths.append(lengths[-1] + math.dist(a, b))
    out, i = [], 0
    for k in range(count):
        target = lengths[-1] * k / max(1, count - 1)
        while i < len(pts) - 2 and lengths[i + 1] < target:
            i += 1
        seg = (lengths[i + 1] - lengths[i]) or 1e-9
        t = min(1.0, max(0.0, (target - lengths[i]) / seg))
        out.append((pts[i][0] + (pts[i + 1][0] - pts[i][0]) * t, pts[i][1] + (pts[i + 1][1] - pts[i][1]) * t))
    return out


def chain_along(line, half_width: float, pieces: int):
    """Ellipses end to end along a polyline, `half_width` either side of it."""
    points = _resample_line(line, max(1, pieces) + 1) if len(line) >= 2 else []
    ellipses = []
    for a, b in zip(points, points[1:]):
        length = math.dist(a, b)
        if length < 0.01:
            continue
        ellipses.append({"center": ((a[0] + b[0]) * 0.5, (a[1] + b[1]) * 0.5),
                         "radii": (length * 0.5 + half_width, half_width),
                         "rotation": math.atan2(b[1] - a[1], b[0] - a[0])})
    return ellipses


def _centreline(pts):
    """
    (centre line, half width) of a long thin polygon (a creek, a snaking
    bunker): its outline split at its two furthest-apart points into two
    banks, averaged pairwise.
    """
    centre = (sum(p[0] for p in pts) / len(pts), sum(p[1] for p in pts) / len(pts))
    i = max(range(len(pts)), key=lambda k: math.dist(pts[k], centre))
    j = max(range(len(pts)), key=lambda k: math.dist(pts[k], pts[i]))
    i, j = min(i, j), max(i, j)
    bank_a = pts[i:j + 1]
    bank_b = (pts[j:] + pts[:i + 1])[::-1]
    if len(bank_a) < 2 or len(bank_b) < 2:
        return None, 0.0
    samples = max(len(bank_a), len(bank_b), 8)
    side_a, side_b = _resample_line(bank_a, samples), _resample_line(bank_b, samples)
    line = [((a[0] + b[0]) * 0.5, (a[1] + b[1]) * 0.5) for a, b in zip(side_a, side_b)]
    widths = [math.dist(a, b) for a, b in zip(side_a, side_b)]
    return line, max(0.5, sum(widths) / len(widths) * 0.5)


def fit_polygon(pts, max_pieces: int = 1, good_iou: float = 0.8,
                min_piece_share: float = 0.03):
    """
    (ellipses, iou): the fewest ellipses, up to `max_pieces`, that cover the
    polygon with an intersection over union of at least `good_iou`, or the best
    fit found when none does.

    Starts from one ellipse and keeps cutting the piece its ellipse fits worst
    in half across its long axis, so a bent bunker becomes two. A long thin
    shape (a creek, a snaking bunker) is also tried as a chain of ellipses
    along its centre line, and whichever covers it better is kept. Slivers
    below `min_piece_share` of the area are dropped.
    """
    first = ellipse_from_polygon(pts)
    if first is None:
        return [], 0.0
    total = abs(polygon_area(pts))
    pieces = [(pts, first)]
    best = ([first], fit_iou(pts, [first]))
    while best[1] < good_iou and len(pieces) < max(1, max_pieces):
        # The piece whose ellipse leaves the most area wrong.
        worst = max(range(len(pieces)), key=lambda i: abs(polygon_area(pieces[i][0])) *
                    (1.0 - fit_iou(pieces[i][0], [pieces[i][1]], cells=24)))
        piece, ellipse = pieces.pop(worst)
        for half in _halves(piece, ellipse):
            if len(half) >= 3 and abs(polygon_area(half)) >= total * min_piece_share:
                fitted = ellipse_from_polygon(half)
                if fitted is not None:
                    pieces.append((half, fitted))
        if not pieces:
            break
        ellipses = [e for _, e in pieces]
        iou = fit_iou(pts, ellipses)
        if iou > best[1]:
            best = (ellipses, iou)
    if best[1] < good_iou and max_pieces > 1:
        line, half_width = _centreline(pts)
        if line:
            chain = chain_along(line, half_width, max_pieces)
            iou = fit_iou(pts, chain)
            if iou > best[1] + 0.05:
                best = (chain, iou)
    return best


def with_min_radius(ellipse, min_radius: float):
    """The ellipse scaled up, keeping its shape, to at least a circle of `min_radius`'s area."""
    a, b = ellipse["radii"]
    mean = math.sqrt(a * b)
    if mean >= min_radius or mean <= 0.0:
        return ellipse
    scale = min_radius / mean
    return {**ellipse, "radii": (a * scale, b * scale)}


def zone_json(zone_type: str, ellipse, offset=(0.0, 0.0), digits: int = 2) -> dict:
    """The hole JSON material zone for an ellipse, its centre moved by -offset."""
    a, b = ellipse["radii"]
    rotation = math.degrees(ellipse["rotation"])
    # The same ellipse turned half a turn; keep the angle in [-90, 90).
    rotation = (rotation + 90.0) % 180.0 - 90.0
    # hole_loader refuses a radius under 0.01, which rounding a sliver could make.
    a, b = max(round(a, digits), 0.01), max(round(b, digits), 0.01)
    if a == b:
        rotation = 0.0
    cx, cz = ellipse["center"]
    return {
        "type": zone_type,
        "center": [round(cx - offset[0], digits) + 0.0, 0, round(cz - offset[1], digits) + 0.0],
        "radii": [a, b],
        "rotation_degrees": round(rotation, 1),
    }
