# golf++ — tooling

Design and import courses for golf++, plus the Windows build helper.

| Path | What |
|---|---|
| `hole_editor/hole-editor.html` | Browser-based editor for holes and course worlds |
| `osm_import/` | Converts real courses from OpenStreetMap into hole/course/world JSON |
| `gb.cmd` / `gb.ps1` | Windows release build helper (see below) |

## gb (build helper)

Run from the repo root:

```pwrshl
.\tooling\gb      # configure + clean rebuild release
.\tooling\gb -r   # build release, then launch golf++
.\tooling\gb -rr  # stop the running golf++ from this build, rebuild, relaunch
```

---

## hole_editor/hole-editor.html

A single-file, no-install web tool for authoring hole JSON by hand. Open it
directly in any browser — no server required.

```
open hole_editor/hole-editor.html
```

In Chromium/Edge, use **open project** and choose the golf++ repo root. The
editor will read `assets/courses/*.json`, show the holes referenced by the
selected course manifest, and save the current hole back to its file under
`assets/holes/`. Browsers without the File System Access API can still use the
paste import/export workflow.

### Canvas controls

| Action | Result |
|---|---|
| Scroll | Zoom in / out |
| Drag background | Pan |
| Left-click element | Select |
| Drag selected element | Move |
| Right-click element | Delete |

### Toolbar modes

**select** — default mode. Click any element on the canvas to select it and
edit its properties in the right panel.

**+ point** — click anywhere on the canvas to append a new Catmull-Rom spline
control point. Points are added in order; the live spline and measurement
labels update as you place them. Switch back to select when done.

**measure** — click a point A, then point B to get the distance between them.
Right-click clears the measurement. Useful for checking zone radii or fairway
lengths against real distances before committing.

**+ green / + bunker** — click to place a circular material zone at that
position. Greens default to radius 6 m, bunkers to 3.5 m. Select the zone
afterward to adjust center and radius in the panel.

**+ water** — click to place a rectangular water hazard (16 × 10 m default).
Select it to drag the corner handles, move the whole hazard, or edit origin,
width, and depth directly in the panel.

**+ tree** — click to place a tree with default proportions (trunk 0.35 r ×
2.4 h, leaves 1.6 r × 3.2 h). Tree mode stays active so repeated clicks paint
trees quickly. Select a tree to adjust position and all four dimensions, or use
that tree's dimensions as the default for newly painted trees.

**fit** — resets the viewport to frame the whole hole.

**new** — clears the canvas and starts a blank hole with a short default
spline.

### Right panel

**Hole info** — set the hole `id`, `name`, `par` (1–6), `wind_seed`, fairway
`width`, and `rough_width`. Width and rough width are in metres and render live
on the canvas.

**Control points** — lists every spline point with its X/Y/Z coordinates.
Select a point on the canvas or in the list to edit X/Y/Z numerically. The Y
value controls elevation; leave at 0 for a flat hole. Selecting the tee or pin
also exposes X/Y/Z fields for quick cleanup.

**Material zones** — lists all greens, bunkers, and water hazards. Circular
zones show center and radius; water zones show the two corner bounds.

**Trees** — lists all placed trees. Select one to fine-tune trunk and leaf
dimensions.

**Export / Import** — Export copies the current hole as a JSON string to the
clipboard (and displays it in the textarea). Import accepts a pasted JSON
string and loads it into the editor. Use this to load an OSM-converted hole,
tweak it, then export the final version.

### Status bar

The top-right overlay always shows:

```
{scale}px/m  |  hole: {direct}m direct / {path}m path  |  pts:{N}  |  zones:{N}  |  trees:{N}  |  x:{N} z:{N}
```

The cursor X/Z coordinates update live as you hover — handy for placing zones
at exact positions relative to the tee (which is always at 0, 0).

### Coordinate system

The tee sits at `[0, 0, 0]`. X runs east, Z runs forward (roughly toward the
pin), Y is elevation. All distances are in metres.

---

## osm_import/osm_golf_convert.py

Run the commands below from `tooling/osm_import/`.

Queries OpenStreetMap via the Overpass API and converts a real golf course into
hole JSON files and a course manifest, ready to load directly into the game or
open in the editor.

### Requirements

```bash
pip install requests  # optional; falls back to Python's standard library
```

### Usage

```bash
# Search by course name — resolved through Nominatim, so this is fast now
py -3 osm_golf_convert.py "Marienlyst Golfklub"

# See every course the name matches, with the --id to use for each
py -3 osm_golf_convert.py "St Andrews" --list

# Nearest course to a lat/lon — useful when you're standing on the course
py -3 osm_golf_convert.py --lat 56.180454 --lon 10.156731

# By OSM ID — most reliable when several courses share a name or a site
py -3 osm_golf_convert.py --id W1019045811
py -3 osm_golf_convert.py --id R3456789

# Custom output directory
py -3 osm_golf_convert.py "Aarhus Golf Klub" -o ../../assets/holes --course-out ../../assets/courses

# Skip writing the course manifest
py -3 osm_golf_convert.py "Aarhus Golf Klub" --no-course

# Use generator tuning, including tree dimensions and hub path filtering
py -3 osm_golf_convert.py "Marienlyst Golfklub" --config osm_golf_config.json

# Flat holes and flat ground, no elevation tiles
py -3 osm_golf_convert.py "Aarhus Golf Klub" --no-elevation

# Re-download instead of reusing the cached OSM responses
py -3 osm_golf_convert.py --id W1019045811 --refresh
```

Use `--id` for anything you intend to keep. Names are ambiguous — "St Andrews"
alone matches seven courses on the same site — and `--list` exists to turn a
name into the right id once, so later re-imports are reproducible.

### Elevation

OSM carries no usable height data for golf features, so the converter samples a
digital elevation model and bakes the result into each hole's spline control
points and the course world's ground grid. This is the same idea as the LiDAR
import in TGC Designer Tools.

The DEM is the [Terrarium elevation tiles](https://registry.opendata.aws/terrain-tiles/)
from AWS Open Data: PNG map tiles whose pixels encode height, built from SRTM
(~30 m) worldwide, USGS NED (10 m or better) in the USA and national lidar in
some European countries. They are free, need no account and have no rate limit.
A course needs a handful of tiles at zoom 14 (`elevation.zoom`), about 65 KB
each, downloaded once into `.osm_cache/terrarium/`. A hundred courses is in the
tens of megabytes, and every later import, moved hole or new ground grid in the
same area reads the cache with no network at all. The sources require
attribution: see `docs/steam_todo.md`.

Hole heights are made relative to the tee, so every hole still starts at
`y = 0` (its tee's real height goes in `source.tee_elevation`, and the course
world puts each hole start at that height relative to hole 1, so holes line up
on the course); smoothed, because neighbouring control points can straddle a
DEM cell boundary; and slope-limited, because a DEM occasionally reads a
clubhouse roof or tree canopy next to a fairway as a cliff.

**What this does and does not give you.** A 10–30 m DEM reproduces the landform
of a hole — the uphill second shot, the valley you have to carry, a plateau
green, the general fall of a fairway. It cannot see green contours, bunker
lips, or mounding, because those are smaller than one DEM cell. Expect to keep
doing green and bunker shaping in the hole editor; expect not to have to
rebuild the overall shape of the land.

The course world also gets a `ground` grid: the DEM sampled every
`ground.cell_size` metres (20 by default) over every hole plus
`ground.margin`, relative to hole 1's start. It is the land between the holes;
the game eases it into each hole's edge. To add or refresh it on a course world
that already exists, including a hand-made one (it needs `projection`):

```bash
py -3 osm_golf_convert.py --ground-only marienlyst_golfklub
```

With `--no-elevation` the ground is written flat.

Only the spline carries elevation. The game builds terrain as a ribbon swept
along the control points and samples zone and tree heights off that mesh, so
writing `y` anywhere else in the hole JSON would be ignored.

### Caching and rate limits

Overpass and Nominatim are donated infrastructure. The converter tries to be a
good citizen:

- every response is cached under `tooling/osm_import/.osm_cache/`, so a re-run after a
  config tweak costs zero requests (`--refresh` forces a re-download,
  `--no-cache` disables it)
- it checks the Overpass slot endpoint and waits when the server is busy
- at least two seconds between Overpass calls, and 429/504 back off
  exponentially rather than retrying immediately
- queries are scoped to the course *area* rather than its bounding box, which
  is both cheaper for the server and more accurate — a bbox around the Old
  Course also contains the town of St Andrews and six other courses
- a hard ceiling on requests per run, so a bug cannot turn into a loop

A full 18-hole import is normally under ten requests.

### Generator config

`osm_golf_config.json` controls values that should survive regeneration:

```json
{
  "tree": {
    "trunk_radius": 0.65,
    "trunk_height": 5.0,
    "leaf_radius": 4.7,
    "leaf_height": 6.0,
    "max_per_hole": 60
  },
  "hole": {
    "fallback_width": 20.0,
    "fallback_rough_width": 32.0,
    "rough_width_multiplier": 1.55
  },
  "world": {
    "max_shortcut_length": 180.0,
    "max_cart_road_length": 280.0,
    "max_path_distance_from_holes": 75.0,
    "fairway_avoidance_clearance": 8.0,
    "fallback_road_extra_offset": 8.0,
    "max_shortcut_count": 12
  },
  "courses": {
    "marienlyst_golfklub": {
      "tree": {
        "trunk_height": 5.0,
        "leaf_radius": 4.7
      }
    }
  }
}
```

The top-level values are defaults. Entries under `courses` are keyed by the
generated course id and override only the fields listed there.

The HTML hole editor also loads this file when you open the project root. Use
its generator config panel for tree defaults and path filtering values, then
save config before running the OSM converter again. When a course is selected,
the editor writes the active course override as well as the default value so
regenerating Marienlyst does not silently keep stale course-specific settings.

### Course-world editing

`hole-editor.html` has a world view for course hub data. Open the project root,
select a course with a `world` manifest entry, then switch to world view to edit:

- cart road polylines
- walking shortcut polylines and unlock level
- collectible positions and simple reward fields
- interactable/sign positions

Walking shortcuts, spawn zones and interactables are authored here but the game
does not read them yet; they are placeholders for NPC/interaction work.

Use area select in either hole view or world view to drag a selection rectangle
around editable points/items. The editor highlights everything inside the box
and can bulk-delete selected route points, trees, zones, collectibles, and
interactables. Fixed anchors such as tees, pins, spawn, and hole starts are
selectable for inspection but are not bulk-deleted.

Save hole, save world, and save config are separate on purpose. Hole JSON uses
per-hole local coordinates, while world JSON uses shared course coordinates.

### Output

Running the converter from `tooling/osm_import/` writes directly to the game's asset
folders by default:

```
../../assets/holes/
  aarhus_golf_klub_h01.json
  aarhus_golf_klub_h02.json
  ...
../../assets/courses/
  aarhus_golf_klub.json          ← course manifest
```

The course manifest follows the same format as the hand-authored course files:

```json
{
  "id": "aarhus_golf_klub",
  "name": "Aarhus Golf Klub",
  "holes": [
    "holes/aarhus_golf_klub_h01.json",
    ...
  ]
}
```

### What the converter does

1. Locates the course on OSM and reads its bounding box.
2. Fetches all `golf=*` elements inside that box (fairways, greens, bunkers,
   water hazards, tees, pins).
3. Groups elements by hole using, in order: **hole relations** (if the course
   is fully mapped), **`ref` tags** on individual elements, or **spatial
   proximity** to tee nodes as a fallback.
4. For each hole:
   - Reprojects WGS84 coordinates to local metres with the tee at `[0, 0, 0]`.
   - Extracts the fairway centerline by slicing the polygon perpendicular to
     the tee→pin axis and taking midpoints, producing 5 spline control points.
   - Estimates fairway width from the polygon's cross-section.
   - Fits bounding circles (Ritter's algorithm) to green and bunker polygons.
   - Converts water hazard polygons to axis-aligned bounding boxes.
   - Estimates par from tee-to-pin distance if OSM doesn't have a `par` tag.

### Verifying an import

`verify_osm_import.py` checks a generated course against independent ground
truth and prints a per-hole table plus a pass/fail summary.

```bash
py -3 verify_osm_import.py --id W1019045811 \
    --holes ../../assets/holes --scorecard old_course
```

It runs four checks:

| Check | Question it answers |
|---|---|
| Projection fidelity | Is a metre in the output really a metre on the ground? |
| Scorecard accuracy | Does the hole play its published length? |
| Plausibility | Are widths, green radii and pars in the range real courses occupy? |
| Loader contract | Will `hole_loader.cpp` actually accept this file? |

The projection check recomputes distances from the tee and pin coordinates the
converter recorded in each hole's `source` block, using Vincenty's formula on
the WGS84 ellipsoid — code that shares nothing with the converter's flat-earth
projection. A disagreement there is a converter bug.

The scorecard check compares against `testdata/scorecards.json`, which holds
published hole-by-hole distances in metres. Add a course by adding an entry;
each one records the tee set and the source it was taken from. Note that golf
measures a hole *along the line of play*, so a dogleg's scorecard number is
longer than the straight tee-to-pin distance — the check compares against the
spline path length for that reason.

Current results:

```
Augusta National (way/871993734)
  projection fidelity : mean 0.201%  worst 0.359%
  scorecard accuracy  : mean -1.3%   18/18 holes within 12%
  total par           : 72  (scorecard 72)
  0 failures, 0 warnings

Old Course, St Andrews (way/1019045811)
  projection fidelity : mean 0.117%  worst 0.255%
  scorecard accuracy  : mean -3.8%   17/18 holes within 12%
  total par           : 72  (scorecard 72)
  0 failures, 1 warning

Marienlyst Golfklub (way/1408711156)
  projection fidelity : mean 0.157%  worst 0.226%
  total par           : 18  (6 holes, all par 3)
  0 failures, 0 warnings
```

Marienlyst is a useful check of a different kind: its hole distances reproduce
the hand-corrected course already in `assets/holes/` to within half a metre on
all six holes.

Both courses also import with every hole's par matching the scorecard, and the
Old Course's hole names come through in order — Burn, Dyke, Cartgate (Out) …
Road, Tom Morris — which is a useful independent check that hole numbering and
identity are right.

A systematic few percent short is expected and is not a converter fault: OSM
maps the everyday teeing grounds, while published yardages are from the
championship tees, which are often separate and unmapped. The Old Course's
remaining warning is its 2nd hole, 62 m short for exactly that reason.

### Typical workflow

```bash
# 1. Convert
python osm_golf_convert.py "Skandinavisk Golf Center"

# 2. Open the editor and paste in a hole to review and fix up
open ../hole_editor/hole-editor.html
# → import → paste hole JSON → adjust spline / zones → export

# 3. Save the cleaned JSON back to ../../assets/holes
```

### Two courses on one site

A single `leisure=golf_course` polygon often covers more than one layout.
Augusta National's contains the Par 3 Course, so hole refs 1–9 appear twice —
once for a 410 m par 4 and once for a 125 m par 3 — and the two used to merge
into single impossible holes carrying the wrong par.

The converter detects contested refs and keeps the layout that the
unambiguously-numbered holes belong to, judging each candidate by how close it
sits to them and how well its length matches theirs. Features that carry a
contested ref but sit far from the winning centreline — the other layout's
tees, pins, greens and bunkers — go with it. The run prints what it dropped.

If you actually wanted the other layout, find its own polygon with `--list`
and import that by `--id`.

### OSM data quality

Results depend on how thoroughly the course is mapped in OSM. Well-mapped
courses have full hole relations with tees, pins, fairways, greens, and
hazards — these convert cleanly. Courses with only a boundary outline will
produce holes with straight splines and no hazard zones; use the editor to
fill in the detail.

Check coverage before running by pasting this into
[overpass-turbo.eu](https://overpass-turbo.eu):

```
[out:json];
relation["leisure"="golf_course"]["name"~"Your Course Name",i];
(._;>>;);
out geom;
```

If you only see the outer boundary and no internal features, the course isn't
mapped at hole level yet and manual editing will be needed.

The single most valuable thing a course can have mapped is the `golf=hole`
way — the surveyed centreline from tee to green. It is what a scorecard
measures along, so it fixes both the hole's length and its shape, and the
converter prefers it over everything else. A course with hole ways imports
close to its real yardage; one without falls back to slicing fairway polygons
and will need the editor.

### What still needs the hole editor

Even a cleanly imported course is a starting point, not a finished one:

- **Green and bunker shapes.** OSM polygons become circles, and a DEM cannot
  see green contours or bunker lips. Expect to reshape these.
- **Elevation detail.** A 10–30 m DEM gives the landform, not the mounding.
- **Trees.** Positions come from `natural=tree` nodes and sampled woodland, so
  they are plausible rather than exact, and are capped per hole.
- **Water.** Hazards become axis-aligned boxes, which is rarely the real shape.
- **Fairway width** falls back to 20 m wherever OSM has no fairway polygon, or
  where the polygon is a shared double fairway and therefore not one hole's.

What you should *not* have to redo by hand is the overall layout: hole
positions, lengths, doglegs, par, and the fall of the land.

### Finding an OSM relation ID

Go to [openstreetmap.org](https://openstreetmap.org), search for the course,
click the result, and copy the relation ID from the URL or the sidebar. Pass
it as `--id R<number>`. This is more reliable than name search for courses
with common or ambiguous names.
