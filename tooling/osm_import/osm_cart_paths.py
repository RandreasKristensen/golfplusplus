#!/usr/bin/env python3
"""
osm_cart_paths.py — a course's one cart path network, in the importer's
local metres (x east, z south).

Paths are routed on a grid over the course, leg by leg. A course whose
mapped paths (OSM's, and those read from the aerial photos where OSM maps
none near) already join most greens to the next tee has its own network: it
keeps the mapped paths that serve play (from the clubhouse to the first tee,
down each hole, from each green to the next tee), bridging only short gaps
in them. So does a par-3 course, which gets no made paths. Every other
course, as most Danish courses are, has no network of its own: carts drive
the holes on the grass, and only a short connector is made from each green
to the next tee, along mapped paths where they lead that way.

A made leg is the cheapest way across the grid, where a path OSM maps costs
least, a path read from the aerial photos next, and plain rough more;
fairways and tees cost much more (a path crosses one only when it must),
ponds more still (a bridge), and greens and bunkers are never crossed.

Each stretch of the network records its source: "osm", "aerial",
"generated", or "bus_stop" for the path made from the club bus's stop to the
first tee (made on every course, so it says nothing of the network's kind).
Pure Python.
"""

import heapq
import math
import sys
from collections import Counter, defaultdict

import osm_outline

OSM, AERIAL, GENERATED, BUS_STOP = "osm", "aerial", "generated", "bus_stop"
BLOCKED = math.inf


class _Grid:
    """Cells `cell` metres wide from (x0, z0), `columns` by `rows`; index = row * columns + column."""

    def __init__(self, low, high, cell: float):
        self.cell = cell
        self.x0, self.z0 = low
        self.columns = max(1, int(math.ceil((high[0] - low[0]) / cell)))
        self.rows = max(1, int(math.ceil((high[1] - low[1]) / cell)))

    def index(self, x: float, z: float):
        column, row = int((x - self.x0) // self.cell), int((z - self.z0) // self.cell)
        if 0 <= column < self.columns and 0 <= row < self.rows:
            return row * self.columns + column
        return None

    def centre(self, index: int):
        row, column = divmod(index, self.columns)
        return self.x0 + (column + 0.5) * self.cell, self.z0 + (row + 0.5) * self.cell

    def fill_ring(self, ring, out: set) -> None:
        """Adds the cells whose centres are inside `ring`, row by row."""
        zs = [p[1] for p in ring]
        first = max(0, int((min(zs) - self.z0) // self.cell))
        last = min(self.rows - 1, int((max(zs) - self.z0) // self.cell))
        edges = list(zip(ring, ring[1:] + ring[:1]))
        for row in range(first, last + 1):
            z = self.z0 + (row + 0.5) * self.cell
            crossings = sorted(a[0] + (z - a[1]) * (b[0] - a[0]) / (b[1] - a[1])
                               for a, b in edges if (a[1] > z) != (b[1] > z))
            for left, right in zip(crossings[::2], crossings[1::2]):
                c0 = max(0, int(math.ceil((left - self.x0) / self.cell - 0.5)))
                c1 = min(self.columns - 1, int(math.floor((right - self.x0) / self.cell - 0.5)))
                out.update(range(row * self.columns + c0, row * self.columns + c1 + 1))

    def trace_line(self, line, out: dict, value) -> None:
        """Sets the cells `line` passes through to `value` (keeping any already set)."""
        step = 0.5 * self.cell
        for a, b in zip(line, line[1:]):
            n = max(1, int(math.ceil(math.dist(a, b) / step)))
            for i in range(n + 1):
                index = self.index(a[0] + (b[0] - a[0]) * i / n, a[1] + (b[1] - a[1]) * i / n)
                if index is not None:
                    out.setdefault(index, value)

    def grown(self, cells: set, reach: float) -> set:
        """`cells` and every cell within `reach` metres of one."""
        k = int(math.ceil(reach / self.cell))
        disk = [(dr, dc) for dr in range(-k, k + 1) for dc in range(-k, k + 1) if math.hypot(dr, dc) * self.cell <= reach]
        out = set(cells)
        columns, rows = self.columns, self.rows
        for index in cells:
            row, column = divmod(index, columns)
            edge = any(not (0 <= row + dr < rows and 0 <= column + dc < columns) or
                       (row + dr) * columns + column + dc not in cells
                       for dr, dc in ((1, 0), (-1, 0), (0, 1), (0, -1)))
            if not edge:
                continue
            for dr, dc in disk:
                r, c = row + dr, column + dc
                if 0 <= r < rows and 0 <= c < columns:
                    out.add(r * columns + c)
        return out


def _route(grid: _Grid, cost_at, start: int, goal: int, window, least_cost: float):
    """The cheapest 8-connected cells from `start` to `goal` within `window`
    (row and column ranges), or None."""
    (r0, r1), (c0, c1) = window
    columns, cell = grid.columns, grid.cell
    gx, gz = grid.centre(goal)
    best = {start: 0.0}
    came = {}
    queue = [(0.0, 0.0, start)]
    steps = [(dr, dc, cell * math.hypot(dr, dc)) for dr in (-1, 0, 1) for dc in (-1, 0, 1) if dr or dc]
    while queue:
        _, spent, index = heapq.heappop(queue)
        if index == goal:
            cells = [goal]
            while cells[-1] != start:
                cells.append(came[cells[-1]])
            return cells[::-1]
        if spent > best.get(index, math.inf):
            continue
        row, column = divmod(index, columns)
        here = cost_at(index)
        for dr, dc, length in steps:
            r, c = row + dr, column + dc
            if not (r0 <= r <= r1 and c0 <= c <= c1):
                continue
            nxt = r * columns + c
            there = cost_at(nxt)
            if there == BLOCKED:
                continue
            total = spent + 0.5 * (here + there) * length
            if total < best.get(nxt, math.inf):
                best[nxt] = total
                came[nxt] = index
                x, z = grid.centre(nxt)
                heapq.heappush(queue, (total + least_cost * math.hypot(gx - x, gz - z), total, nxt))
    return None


def _open_ground(grid: _Grid, blocked: set) -> set:
    """The largest 8-connected piece of the grid outside `blocked`: where a
    path can go and get anywhere (not a pocket between a green and its bunkers)."""
    columns, rows = grid.columns, grid.rows
    seen = bytearray(columns * rows)
    for index in blocked:
        seen[index] = 1
    best = set()
    for start in range(columns * rows):
        if seen[start]:
            continue
        seen[start] = 1
        piece, todo = [start], [start]
        while todo:
            index = todo.pop()
            row, column = divmod(index, columns)
            for dr in (-1, 0, 1):
                for dc in (-1, 0, 1):
                    r, c = row + dr, column + dc
                    if 0 <= r < rows and 0 <= c < columns and not seen[r * columns + c]:
                        seen[r * columns + c] = 1
                        piece.append(r * columns + c)
                        todo.append(r * columns + c)
        if len(piece) > len(best):
            best = set(piece)
    return best


def _nearest_free(grid: _Grid, point, ok) -> int | None:
    """The cell nearest `point` that `ok` accepts, searched outwards ring by ring."""
    column, row = int((point[0] - grid.x0) // grid.cell), int((point[1] - grid.z0) // grid.cell)
    for radius in range(0, max(grid.columns, grid.rows)):
        found = []
        for r in range(row - radius, row + radius + 1):
            for c in range(column - radius, column + radius + 1):
                if max(abs(r - row), abs(c - column)) != radius or not (0 <= r < grid.rows and 0 <= c < grid.columns):
                    continue
                index = r * grid.columns + c
                if ok(index):
                    found.append((math.dist(grid.centre(index), point), index))
        if found:
            return min(found)[1]
    return None


def _pruned(edges: set, keep: set, grid: _Grid, least: float) -> set:
    """`edges` without the spurs the grid leaves: dead ends shorter than
    `least` metres back to where paths meet, but those ending at a cell of `keep`."""
    edges = set(edges)
    while True:
        links = defaultdict(set)
        for a, b in edges:
            links[a].add(b)
            links[b].add(a)
        cut = set()
        for leaf in sorted(links):
            if len(links[leaf]) != 1 or leaf in keep:
                continue
            spur, at, length = [], leaf, 0.0
            previous = None
            while len(links[at]) <= 2:
                onward = [n for n in links[at] if n != previous]
                if not onward:
                    break
                spur.append((min(at, onward[0]), max(at, onward[0])))
                length += math.dist(grid.centre(at), grid.centre(onward[0]))
                previous, at = at, onward[0]
                if at in keep or length >= least:
                    break
            if len(links[at]) > 2 and length < least:
                cut.update(spur)
        if not cut:
            return edges
        edges -= cut


def _without_triangles(edges: set) -> set:
    """`edges` with each three-cell loop (two legs through neighbouring
    cells) opened at its longest side."""
    links = defaultdict(set)
    for a, b in edges:
        links[a].add(b)
        links[b].add(a)
    out = set(edges)
    for a, b in sorted(edges, key=lambda e: (-abs(e[1] - e[0]), e)):
        if (a, b) in out and any((min(a, c), max(a, c)) in out and (min(b, c), max(b, c)) in out
                                 for c in links[a] & links[b]):
            out.discard((a, b))
    return out


def _chains(edges: set, source: dict, least_run: int):
    """The network's edges (cell pairs) as chains of cells, broken where
    three or more meet, at dead ends and where the source changes; a run of
    one source shorter than `least_run` cells takes its neighbours'."""
    links = defaultdict(set)
    for a, b in edges:
        links[a].add(b)
        links[b].add(a)
    seen = set()
    whole = []

    def walk(chain):
        while len(links[chain[-1]]) == 2:
            onward = [n for n in sorted(links[chain[-1]]) if n != chain[-2]]
            if not onward or (chain[-1], onward[0]) in seen:
                break
            seen.update({(chain[-1], onward[0]), (onward[0], chain[-1])})
            chain.append(onward[0])
        return chain

    for start in sorted(links):
        if len(links[start]) == 2:
            continue
        for nxt in sorted(links[start]):
            if (start, nxt) not in seen:
                seen.update({(start, nxt), (nxt, start)})
                whole.append(walk([start, nxt]))
    # Loops with no junction on them.
    for a, b in sorted(edges):
        if (a, b) not in seen:
            seen.update({(a, b), (b, a)})
            whole.append(walk([a, b]))

    chains = []
    for chain in whole:
        runs = []  # [source, first index, last index]
        for i, cell in enumerate(chain):
            if runs and runs[-1][0] == source[cell]:
                runs[-1][2] = i
            else:
                runs.append([source[cell], i, i])
        while len(runs) > 1:
            short = min(range(len(runs)), key=lambda k: runs[k][2] - runs[k][1])
            if runs[short][2] - runs[short][1] + 1 >= least_run:
                break
            into = short - 1 if short > 0 and (short + 1 == len(runs) or
                                               runs[short - 1][2] - runs[short - 1][1] >= runs[short + 1][2] - runs[short + 1][1]) else short + 1
            first, last = min(runs[short][1], runs[into][1]), max(runs[short][2], runs[into][2])
            runs[into][1], runs[into][2] = first, last
            del runs[short]
        for kind, first, last in runs:
            piece = chain[max(0, first - 1) if first else 0:last + 1]
            if len(piece) >= 2:
                chains.append((kind, piece))
    return chains


def _turn(a, b, c) -> float:
    """How far the way turns at `b`, radians (0 straight on, pi back)."""
    return abs((math.atan2(c[1] - b[1], c[0] - b[0]) - math.atan2(b[1] - a[1], b[0] - a[0]) + math.pi)
               % (2.0 * math.pi) - math.pi)


def _smoothed(points, sigma: float, sharpest: float):
    """`points` resampled every metre and smoothed as by a Gaussian `sigma`
    metres wide (repeated (1/4, 1/2, 1/4) averaging, the ends kept), so its
    corners become curves a cart takes at speed; but cut where it turns
    sharper than `sharpest` radians (a path doubling back, where a cart stops
    and turns), each side smoothed on its own."""
    pieces, piece = [], [points[0]]
    for i in range(1, len(points)):
        piece.append(points[i])
        if i + 1 < len(points) and _turn(points[i - 1], points[i], points[i + 1]) > sharpest:
            pieces.append(piece)
            piece = [points[i]]
    pieces.append(piece)
    passes = int(round(2.0 * sigma * sigma))
    out = []
    for piece in pieces:
        line = osm_outline.resample(piece, 1.0) if len(piece) >= 2 else list(piece)
        for _ in range(passes if len(line) >= 3 else 0):
            line = [line[0]] + [(0.25 * a[0] + 0.5 * b[0] + 0.25 * c[0], 0.25 * a[1] + 0.5 * b[1] + 0.25 * c[1])
                                for a, b, c in zip(line, line[1:], line[2:])] + [line[-1]]
        out += line if not out else line[1:]
    return osm_outline.simplify_line(out, 0.05)


def _cut_at_bends(line, sharpest: float, reach: float):
    """`line` cut where, over `reach` metres either side of a point, it turns
    sharper than `sharpest` radians: a hook no cart drives round at speed,
    where the stretches meet instead (sharing the point)."""
    points = osm_outline.resample(line, 1.0)
    k = max(1, int(round(reach)))
    def heading(a, b):
        return math.atan2(b[1] - a[1], b[0] - a[0])

    # The way it heads `reach` before the point against `reach` after it.
    turns = [abs((heading(points[i + k - 1], points[i + k]) - heading(points[i - k], points[i - k + 1]) + math.pi)
                 % (2.0 * math.pi) - math.pi) if k <= i < len(points) - k else 0.0
             for i in range(len(points))]
    # One cut in the middle of each run of points that turn too sharply.
    cuts, run = [], []
    for i, turn in enumerate(turns + [0.0]):
        if turn > sharpest:
            run.append(i)
        elif run:
            cuts.append(run[len(run) // 2])
            run = []
    pieces, first = [], 0
    for i in cuts:
        if i - first >= 2:
            pieces.append(points[first:i + 1])
            first = i
    pieces.append(points[first:])
    return [piece for piece in pieces if len(piece) >= 2]


def build_network(holes, osm_paths, aerial_points, start, rules: dict, bus_stop=None) -> list[dict]:
    """The course's cart paths (course world `cart_roads`, polylines in local
    metres as [x, 0, z]) linking `start` (the clubhouse, or None) to the first
    tee, each hole's tee to its green and each green to the next tee, the
    first tee reached from beside or behind it, never across its first shot. A
    `bus_stop` (osm_bus_stop.choose) always gets a path from its approach to
    the first tee, along mapped paths where they lead there, and no path runs
    under the parked bus.

    `holes`, in play order: {"tee": (x, z), "pin": (x, z), "par", "line": tee
    to pin, "greens", "bunkers", "waters", "fairways", "tees": rings}. `osm_paths`: {"line", "ref", "surface", "cartpath"} for each path
    OSM maps; `aerial_points`: the (x, z) of every cell the aerial photos read
    as path (osm_aerial.paths), or None."""
    if not holes:
        return []
    cell = float(rules["cell_meters"])
    width = float(rules["width_meters"])
    pts = [p for h in holes for p in h["line"]] + ([start] if start else []) + ([bus_stop["approach"]] if bus_stop else [])
    margin = float(rules["margin_meters"])
    grid = _Grid((min(p[0] for p in pts) - margin, min(p[1] for p in pts) - margin),
                 (max(p[0] for p in pts) + margin, max(p[1] for p in pts) + margin), cell)

    def filled(key):
        out = set()
        for h in holes:
            for ring in h[key]:
                if len(ring) >= 3:
                    grid.fill_ring(ring, out)
        return out

    half = 0.5 * width
    away = rules["clearance_meters"]
    # Clear of the greens and bunkers always; of the water but for a bridge.
    greens = filled("greens")
    blocked = grid.grown(greens, half + float(away["green"][0])) | grid.grown(filled("bunkers"), half + float(away["hazard"]))
    # The parked bus is kept two cells clear: past the game's cart path
    # clearance after grid rounding, simplifying and smoothing.
    parked = set()
    if bus_stop:
        grid.fill_ring(bus_stop["footprint"], parked)
        blocked |= grid.grown(parked, half + 2.0 * cell)
    near_green = grid.grown(greens, half + float(away["green"][1]))
    # What a smoothed curve must still keep off: the greens and bunkers themselves.
    on_hazard = grid.grown(greens | filled("bunkers"), half) | grid.grown(parked, half + 2.0 * cell)
    water = grid.grown(filled("waters"), half + float(away["hazard"]))
    # OSM's paths cross the fairways' edges but never run down them; every
    # tee, OSM's and the tee box the game lays at each hole's, is kept clear
    # of like a fairway.
    tee_pads = filled("tees")
    for h in holes:
        grid.fill_ring(osm_outline.circle(h["tee"], float(rules["tee_pad_radius_meters"])), tee_pads)
    fairways = filled("fairways")
    fairway_core = fairways | tee_pads
    fairway = grid.grown(fairways, half + float(away["fairway"][0])) | grid.grown(tee_pads, half + float(away["tee"][0]))
    near_fairway = grid.grown(fairways, half + float(away["fairway"][1])) | grid.grown(tee_pads, half + float(away["tee"][1]))

    osm = {}
    for path in osm_paths:
        grid.trace_line(path["line"], osm, path)
    aerial = set()
    if aerial_points:
        osm_near = grid.grown(set(osm), float(rules["aerial_beside_osm_meters"]))
        for x, z in aerial_points:
            index = grid.index(x, z)
            if index is not None and index not in osm_near:
                aerial.add(index)

    costs = rules["costs"]
    network = {}

    def base_cost(index):
        if index in blocked:
            return BLOCKED
        if index in fairway_core:
            return float(costs["fairway"])
        if index in osm:
            return float(costs["osm_cartpath"] if osm[index]["cartpath"] else costs["osm_path"])
        if index in network:
            return float(costs["network"])
        if index in water:
            return float(costs["water"])
        if index in aerial:
            return float(costs["aerial"])
        if index in fairway:
            return float(costs["near_fairway"])
        if index in near_fairway or index in near_green:
            return float(costs["rough"])
        return float(costs["rough"]) + float(costs["outside_band"])

    least = min(float(v) for v in costs.values())
    pad = float(rules["window_meters"])

    def window(points):
        xs, zs = [p[0] for p in points], [p[1] for p in points]
        return ((max(0, int((min(zs) - pad - grid.z0) // cell)), min(grid.rows - 1, int((max(zs) + pad - grid.z0) // cell))),
                (max(0, int((min(xs) - pad - grid.x0) // cell)), min(grid.columns - 1, int((max(xs) + pad - grid.x0) // cell))))

    open_ground = _open_ground(grid, blocked)

    def free(index):
        return index in open_ground and base_cost(index) <= float(costs["rough"]) + float(costs["outside_band"])

    def lay(cells, bridging=False, made=GENERATED):
        """Adds `cells` to the network, those on no mapped path as `made`;
        `bridging`, a gap between mapped paths takes the source of the path
        before it."""
        source = made
        for index in cells:
            if index in osm or index in aerial or not bridging:
                source = OSM if index in osm else AERIAL if index in aerial else made
            network.setdefault(index, source)
        edges.update((min(a, b), max(a, b)) for a, b in zip(cells, cells[1:]))

    edges = set()

    def behind_tee(h):
        """Whether a cell is level with or behind `h`'s tee, so a path reaches
        it from beside or behind and never crosses its first shot. Only the
        first tee's: the paths are laid with no idea of the ground's slope, and
        another tee's behind may be a bank no cart climbs."""
        line = h["line"]
        way = (line[1][0] - line[0][0], line[1][1] - line[0][1]) if len(line) >= 2 else (0.0, 0.0)

        def ok(index):
            x, z = grid.centre(index)
            return free(index) and (x - h["tee"][0]) * way[0] + (z - h["tee"][1]) * way[1] <= 0.0
        return ok

    tees = [_nearest_free(grid, h["tee"], free) for h in holes]
    first_end = _nearest_free(grid, holes[0]["tee"], behind_tee(holes[0]))
    if first_end is not None:
        tees[0] = first_end
    greens = [_nearest_free(grid, h["pin"], free) for h in holes]
    # Each leg: the cells it runs between, and the places they stand for.
    connectors = [(greens[i], tees[i + 1], holes[i]["pin"], holes[i + 1]["tee"]) for i in range(len(holes) - 1)]
    every_leg = []
    if start is not None:
        every_leg.append((_nearest_free(grid, start, free), tees[0], start, holes[0]["tee"]))
    for i in range(len(holes)):
        every_leg.append((tees[i], greens[i], holes[i]["tee"], holes[i]["pin"]))
        if i + 1 < len(holes):
            every_leg.append(connectors[i])

    # Along the mapped paths only: off them (short of a green or bunker
    # itself) a cell costs `gap`, so a route takes the paths and bridges
    # what little lies between them.
    mapped = set(osm) | aerial

    def mapped_cost(index):
        if index in mapped:
            if index in on_hazard:
                return BLOCKED
            if index in osm:
                return float(costs["osm_cartpath"] if osm[index]["cartpath"] else costs["osm_path"])
            return float(costs["aerial"])
        return BLOCKED if index in blocked else float(costs["gap"])

    most_gap = float(rules["most_gap_meters"])
    end_reach = float(rules["mapped_end_reach_meters"])

    def run_length(cells, a, b):
        return sum(math.dist(grid.centre(cells[k]), grid.centre(cells[k + 1])) for k in range(a, b))

    def mapped_route(begin, end, begin_at, end_at):
        """The cells from `begin` to `end` along the mapped paths, from where
        they first meet one to where they last leave one, if those are within
        end_reach of `begin_at` and `end_at` and no gap between is longer
        than most_gap; else None."""
        if begin is None or end is None:
            return None
        cells = _route(grid, mapped_cost, begin, end, ((0, grid.rows - 1), (0, grid.columns - 1)), least)
        on = [i for i, index in enumerate(cells or []) if index in mapped]
        if not on:
            return None
        if math.dist(grid.centre(cells[on[0]]), begin_at) > end_reach or math.dist(grid.centre(cells[on[-1]]), end_at) > end_reach:
            return None
        if any(run_length(cells, a, b) > most_gap for a, b in zip(on, on[1:])):
            return None
        return cells[on[0]:on[-1] + 1]

    joined = [leg for leg in connectors if mapped_route(*leg) is not None]
    par_three = all(int(h.get("par", 0)) == 3 for h in holes)
    own_network = bool(connectors) and len(joined) >= float(rules["own_network_share"]) * len(connectors)
    keep_mapped = par_three or own_network
    legs = every_leg if keep_mapped else connectors
    laid_ends = set()

    for begin, end, begin_at, end_at in legs:
        if begin is None or end is None or begin == end:
            continue
        if keep_mapped:
            cells = mapped_route(begin, end, begin_at, end_at)
            if cells is not None and len(cells) >= 2:
                lay(cells, bridging=True)
                laid_ends.update((cells[0], cells[-1]))
            continue
        cells = _route(grid, base_cost, begin, end, window([grid.centre(begin), grid.centre(end)]), least)
        if cells is None:
            cells = _route(grid, base_cost, begin, end, ((0, grid.rows - 1), (0, grid.columns - 1)), least)
        if cells is not None:
            lay(cells)
            laid_ends.update((begin, end))
        else:
            print(f"  [warn] no cart path from {grid.centre(begin)} to {grid.centre(end)}", file=sys.stderr)

    if bus_stop:
        # From beside the door, which may stand in a fairway's or tee's band.
        begin, end = _nearest_free(grid, bus_stop["approach"], lambda index: index in open_ground), tees[0]
        if begin is not None and end is not None and begin != end:
            # Mapped paths cost least, so it takes them where they lead, from the door itself.
            cells = _route(grid, base_cost, begin, end, ((0, grid.rows - 1), (0, grid.columns - 1)), least)
            if cells is not None and len(cells) >= 2:
                lay(cells, made=BUS_STOP)
                laid_ends.update((cells[0], cells[-1]))
            else:
                print("  [warn] no path from the bus stop to the first tee", file=sys.stderr)

    surface_of = {OSM: None, AERIAL: rules["aerial_surface"], GENERATED: rules["generated_surface"],
                  BUS_STOP: rules["generated_surface"]}
    roads = []
    ends = laid_ends
    least_run = max(2, int(round(float(rules["least_source_run_meters"]) / cell)))
    for source, chain in _chains(_without_triangles(_pruned(edges, ends, grid, float(rules["least_spur_meters"]))),
                                 network, least_run):
        line = osm_outline.simplify_line([grid.centre(i) for i in chain], float(rules["simplify_meters"]))
        # A small loop is two legs side by side a cell apart, not a path.
        if chain[0] == chain[-1] and sum(math.dist(a, b) for a, b in zip(line, line[1:])) < float(rules["least_loop_meters"]):
            continue
        sharpest = math.radians(float(rules["sharpest_turn_degrees"]))
        for piece in _cut_at_bends(line, math.radians(float(rules["hook_degrees"])), float(rules["smooth_meters"])):
            road = {"id": f"cart_path_{len(roads) + 1:03d}", "source": source, "surface": surface_of[source],
                    "width": width}
            if source == OSM:
                ways = Counter(osm[i]["ref"] for i in chain if i in osm)
                ref = ways.most_common(1)[0][0]
                road["osm_ref"] = ref
                road["surface"] = next(osm[i]["surface"] for i in chain if i in osm and osm[i]["ref"] == ref)
            # Smoothed as far as it can be without drawing a curve onto a green or bunker.
            sigma = float(rules["smooth_meters"])
            smooth = _smoothed(piece, sigma, sharpest)
            while sigma > 0.5 and any(grid.index(x, z) in on_hazard for x, z in osm_outline.resample(smooth, 1.0)):
                sigma *= 0.5
                smooth = _smoothed(piece, sigma, sharpest)
            road["polyline"] = [[round(x, 2), 0.0, round(z, 2)] for x, z in smooth]
            roads.append(road)
    return roads


def connects(roads, a, b, reach: float) -> bool:
    """Whether the network `roads` (course world cart_roads) runs from within
    `reach` of point `a` to within `reach` of point `b` (x, z), stretches
    joining where their points meet."""
    points, links = [], defaultdict(set)
    key = {}
    for road in roads:
        prev = None
        for p in road["polyline"]:
            at = (round(p[0], 1), round(p[2], 1))
            if at not in key:
                key[at] = len(points)
                points.append((p[0], p[2]))
            index = key[at]
            if prev is not None:
                links[prev].add(index)
                links[index].add(prev)
            prev = index

    def near(target):
        found = set()
        for road in roads:
            line = [(p[0], p[2]) for p in road["polyline"]]
            for i, (p, q) in enumerate(zip(line, line[1:])):
                if osm_outline.segment_distance(target, p, q) <= reach:
                    found.update({key[(round(p[0], 1), round(p[1], 1))], key[(round(q[0], 1), round(q[1], 1))]})
        return found

    starts, goals = near(a), near(b)
    seen, todo = set(starts), list(starts)
    while todo:
        index = todo.pop()
        if index in goals:
            return True
        for n in links[index]:
            if n not in seen:
                seen.add(n)
                todo.append(n)
    return False


def lengths_by_source(roads) -> dict:
    out = Counter()
    for road in roads:
        line = road["polyline"]
        out[road["source"]] += sum(math.hypot(b[0] - a[0], b[2] - a[2]) for a, b in zip(line, line[1:]))
    return {k: round(v) for k, v in sorted(out.items())}
