# golf++ — tooling

Design and import courses for golf++, plus the Windows build helper and the
local multiplayer server.

| Path | What |
|---|---|
| `hole_editor/hole-editor.html` | Browser-based editor for holes and course worlds |
| `osm_import/` | Converts real courses from OpenStreetMap into hole/course/world JSON |
| `art/make_art.py` | Draws the rough's grass texture and every course's sky and land backdrop panoramas |
| `gb.cmd` / `gb.ps1` | Windows release build helper, and local multiplayer testing (see below) |
| `net/dev.ps1` | The local SpacetimeDB server on demand: start, publish, anonymous logins, stop |
| `net/check_determinism.ps1` | Golden shots natively and in the server's WASM |

## art/make_art.py

Draws `assets/textures/rough_grass.bmp` and the two backdrop panoramas each
course file in `assets/courses/` names, from code and fixed seeds:

```json
"backdrop": {
  "sky": "backdrops/<id>_sky.bmp",
  "land": "backdrops/<id>_land.bmp",
  "haze_color": [0.75, 0.80, 0.83],
  "haze_amount": 1.0,
  "haze_distance": 800
}
```

The sky fades to `haze_color` at the horizon. The land (far ground, hills and
two or three treelines, 32-bit with alpha) is drawn in its own colours, its
alpha saying how much of it shows through the haze: the further back, the
hazier. The game draws the sky, then the land fading into `haze_color`
(`haze_amount` scales the haze, 1 as drawn), and fades its own ground and
trees into the same haze over `haze_distance` metres, so the drawn ground
melts into the backdrop. Change `haze_color` and rerun the script, as the sky
is drawn to meet it. Courses without a hand-tuned theme in the script (fresh
imports) get a default parkland one. Run it after importing a course;
`--check` fails if `assets/` is out of date.

```
python tooling/art/make_art.py
```

## gb (build helper)

Run from the repo root:

```pwrshl
.\tooling\gb      # configure + build release
.\tooling\gb -r   # build release, then launch golf++
.\tooling\gb -rr  # stop the running golf++ from this build, rebuild, relaunch
.\tooling\gb -m   # local server up (net/dev.ps1), build, launch two anonymous clients on it
.\tooling\gb -mm  # the same, stopping the running golf++ from this build first
.\tooling\gb -x   # stop golf++ from this build and the local server
```

### Local multiplayer

`gb -m` is all it takes: `net/dev.ps1` starts `spacetime start` in its own
minimised window when nothing listens on port 3000, builds the server module
(a few seconds when nothing changed), publishes it as `golfpp` when the
database is missing or the module changed since the last publish (then
regenerates the client bindings, before the game builds), and turns on
anonymous logins. Then two clients start, each its own guest account (deleted
when it closes, so its name is free again next time), on
`http://localhost:3000`, database `golfpp`. `gb -x` stops them and the
server.

The server only runs while you test: the database lives in SpacetimeDB's data
directory, so it, its accounts and its settings survive `gb -x`, and the next
`gb -m` starts in a few seconds. Republishing is an in-place update that
never deletes data; a module change that would need that fails with a message,
and then `spacetime publish golfpp --server local --delete-data` (by hand)
starts the local database over.

`net/dev.ps1` on its own: no arguments brings the server up, `-Down` stops it.
It needs the spacetime CLI and the Emscripten SDK (`-Emsdk`, else `EMSDK`,
else `~\emsdk`). Anonymous logins are turned on with `admin_set_config` as the
publisher, keeping the issuer and client id from `assets/online.json` (so
browser sign-in works locally too) and the database's link secret (a random
one for a new database).

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

**+ green / + bunker / + water** — click to place an elliptical material zone
at that position: greens a 6 m circle, bunkers 3.5 m, water 8 × 5 m radii.
Select a zone to drag it, drag the two filled handles to set each radius along
the zone's own axes, and drag the ring handle to turn it (shift snaps to
15°); the panel edits the same centre, radii and rotation. A zone is saved as
`{"type": "green", "center": [x, y, z], "radii": [rx, rz],
"rotation_degrees": d}` (a circle is `[r, r]`); the rotation turns the zone
the way a course world's hole `rotation_degrees` turns a hole, and the two add
up when the hole is placed.

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

**Material zones** — lists all greens, bunkers, and water hazards with their
size, rotation and centre.

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

# One boundary holding two courses told apart by ref (Kalø: 1-18 and P1-P9);
# the course's config entry can set "ref_prefix" instead
py -3 osm_golf_convert.py --id W96706717 --ref-prefix P --course-name "Kalø Par 3"

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

Each control point also gets a `bank`: the land's side slope across the hole
(rise per metre towards the right looking down the hole),
clamped to `elevation.max_bank`, so a hole on a hillside tilts with the hill.
The game keeps the hole's own height across the fairway and eases its rough
into the course's land, so neighbouring holes meet on the land. Hand-made holes
leave `bank` out and stay level; the hole editor shows and edits it per point.

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
    "max_per_hole": 200,
    "outside_reach_m": 80.0,
    "max_distance_from_line_m": 95.0,
    "wood_m2_per_tree": 150.0
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

Trees come from `natural=tree` nodes, `natural=tree_row` lines, and woods and
scrub planted at one tree per `wood_m2_per_tree` (an even jittered grid), both
inside the course and up to `outside_reach_m` beyond its boundary, where a
hole's edge often is. Each tree goes to the hole whose line it is nearest, up
to `max_distance_from_line_m` away, keeping the `max_per_hole` nearest the line;
none stand on the fairway's middle or on greens, bunkers and water.

The top-level values are defaults. Entries under `courses` are keyed by the
generated course id and override only the fields listed there. The file also
holds `elevation`, `ground`, `zones` (ellipse fitting, see below) and `checks`
(verification tolerances, see below).

The HTML hole editor also loads this file when you open the project root. Use
its generator config panel for tree defaults and path filtering values, then
save config before running the OSM converter again. When a course is selected,
the editor writes the active course override as well as the default value so
regenerating Marienlyst does not silently keep stale course-specific settings.

### Course-world editing

`hole-editor.html` has a world view for course hub data. Open the project root,
select a course with a `world` manifest entry, then switch to world view to:

- move and rotate complete holes (drag a hole's outline; Q/E or the panel turns it)
- draw fences: **+ fence** adds a pole where you click, to the end of the
  selected fence or as the first pole of a new one; drag poles to move them,
  right-click one to delete it, and set the fence's height in the panel. A
  fence needs two poles to be saved.

Cart roads, walking shortcuts, collectibles and interactables come from the
importer and are not edited in world view. Walking shortcuts, spawn zones and
interactables are not read by the game yet; they are placeholders for
NPC/interaction work.

Use area select in hole view to drag a selection rectangle around editable
points; the editor highlights everything inside the box and can bulk-delete
selected trees, zones and control points.

Save hole, save world, and save config are separate on purpose. Hole JSON uses
per-hole local coordinates, while world JSON uses shared course coordinates.

### Fences

A course world's `fences` are `{"poles": [[x, 0, z], ...], "height": h}` in
course coordinates: a pole at each point and a net between each pair, from the
ground (poles stand on it; their y is ignored) up to `height` metres. Shots hit
both: poles like tree trunks, nets catching the ball with little bounce
(`fence` in the game's tuning). The importer makes one from every
`barrier=fence` way within `fence.outside_reach_m` of the course: a pole at each
node, and more where two are over `fence.max_pole_spacing_m` apart. Its height
is the way's `height` tag, else `fence.driving_range_height_m` along a
`golf=driving_range` (the tall ball-stop net), else `fence.default_height_m`;
an untagged fence is a `FENCE_NO_HEIGHT` note in the audit.

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

1. Locates the course on OSM and reads its boundary.
2. Fetches every `golf=*` element inside it (fairways, greens, bunkers, water
   hazards, tees, pins, hole ways) and every pond (`natural=water`, played as
   a water hazard), leaving out those of a neighbouring course's boundary
   (see below).
3. Audits them for missing information (below) and writes what to map in OSM.
4. Groups elements by hole using, in order: **hole relations** (if the course
   is fully mapped), **`ref` tags** on individual elements, the course's
   **`holes` list** in the config, or **spatial proximity** as a fallback.
5. For each hole:
   - Reprojects WGS84 coordinates to local metres with the tee at `[0, 0, 0]`.
   - Takes the line of play from the `golf=hole` way, else the fairway
     polygon's centreline, else a straight line.
   - Estimates fairway width from the polygon's cross-section.
   - Fits every green, bunker and water polygon with ellipses (below).
   - Estimates par from the length if OSM has no `par` tag.
6. Writes the course world, each hole start exactly on its hole's tee.
7. Verifies the result and draws its contact sheet (below).

### Zones are ellipses

A material zone is `{"type": "bunker", "center": [x, y, z], "radii": [rx, rz],
"rotation_degrees": d}`, turned about the vertical like a hole start
(`rotate_about_y`: the first radius lies along `(cos d, sin d)` in x/z).
`osm_ellipse.py` fits each OSM polygon from its area-weighted second moments,
scaled to the polygon's area. A shape one ellipse cannot follow is cut in half
across its long axis where the fit is worst, again and again, or laid out as a
chain of ellipses along its centre line (a creek), up to `zones.max_pieces`; a
stream mapped as a line becomes a chain `zones.stream_half_width` wide. How
well the ellipses cover each polygon (intersection over union) is recorded in
the hole's `source.zone_fit` next to the OSM way, and a poor fit is a
`ZONE_FIT` warning. A hole whose pin has no green in OSM gets a circle at the
pin (`source.made_green`, a `GREEN_MADE` warning).

### Verifying an import

Every import ends with a verification, and `verify_osm_import.py` runs the same
checks on what is already in `assets/` (after a re-import or hand edits):

```bash
py -3 verify_osm_import.py mollerup_golf_club
py -3 verify_osm_import.py --all --quiet       # exit status 1 on any error
```

It checks the generated files — holes, course world and the course's entry in
`testdata/scorecards.json` — and audits the course's OSM data again (from the
cache, else OSM; `--no-osm` skips that), and prints a per-hole table (par,
length, direction, and the scorecard's beside them) and coded findings
(`osm_checks.py`):

```
WARN  HOLE_LENGTH: hole 4 is 322 m, the scorecard says 395 m (-19%)
ERROR HOLE_BEARING: hole 6 was expected to face west (270°) but faces north (355°)
```

An **error** means the course is wrong; a correct import has none. A
**warning** is worth a look in the editor; an **info** note is true of real
courses too. Tolerances are the `checks` section of `osm_golf_config.json`.

| Code | Level | Needs a scorecard | Meaning |
|---|---|---|---|
| `HOLE_COUNT` | error | – | Holes imported against the scorecard's, or against the world's hole starts |
| `NUMBERING_GAP` | error | – | Hole files out of order or numbers missing |
| `PAR_MISMATCH`, `TOTAL_PAR` | error | yes | Par differs from the scorecard |
| `HOLE_LENGTH` | warn / error | yes | Length along the line of play off the scorecard by `length_warn` / `length_error` |
| `HOLE_BEARING` | warn / error | yes | Tee-to-pin direction off the scorecard's by `bearing_warn_deg` / `bearing_error_deg` |
| `TEE_GREEN_PAIRING` | error | yes | A hole far off its length whose tee matches another hole's green |
| `NAME_MISMATCH` | warn | yes | Hole name differs from the scorecard's |
| `HOLE_START_MISMATCH` | error | – | The world hole start is not the hole's tee, so the game draws the hole elsewhere |
| `PROJECTION` | error | – | Tee-to-pin distance differs from the geodesic (Vincenty) one |
| `GREEN_SHARED` | error | – | Two holes finish on the same pin |
| `HOLES_OVERLAP` | error | – | One hole runs along another for most of its length: imported twice |
| `GREEN_MISSING`, `PIN_OFF_GREEN` | error | – | A hole without a green, or whose pin is off it |
| `LOADER`, `ZONE_FORMAT` | error | – | Something `hole_loader.cpp` would refuse (e.g. an old `radius` zone) |
| `ROUTING` | warn / error | – | Long walk from a green to the next tee, naming a nearer tee if there is one |
| `PAR_LENGTH`, `LINE_DETOUR` | warn | – | A length implausible for its par; a line of play far longer than tee to pin |
| `ZONE_FIT`, `GREEN_MADE`, `TEE_SHARED` | warn | – | See above; two holes starting on one spot |
| `HOLES_CROSS` | info | – | Two holes cross (the Old Course's 7th and 11th really do) |
| `TREES_SPARSE` | info | – | A side of a hole with hardly a tree (`sparse_trees_per_100m` within `tree_side_m`), named left/right with its compass direction: if it is wooded in real life, map the trees in OSM |

**Contact sheet.** Each verification also writes a north-up SVG of the course
(`osm_contact_sheet.py`) to `.osm_cache/contact_sheets/<course>.svg` (the path
is printed; `--sheet-out` changes it): every hole's line from a numbered disc
at the tee to its number at the green, the greens, bunkers and water as the
game sees them, a dashed red arrow along the scorecard's direction for each
hole, grey dashes from each green to the next tee, and the findings. Hold it
beside the club's course map: a wrongly numbered or reversed hole is obvious
in a minute.

**Reference data.** `testdata/scorecards.json` holds, per course id, the
published par and length of each hole (`metres`, along the line of play, from
the tee set named in `tees`), optionally its direction (`bearing_deg`, or
`faces`: `N`, `NE`, … `NNW`), the `sources` and a `confidence` note saying how
the numbers were taken. Every course in `assets/courses` has one.

Current results (`--all`): no errors. Warnings: Marienlyst's 1st and 5th are
16-19% short of the club's rounded lengths; Mollerup's 4th is 19% short of the
back tee (OSM's tee is the 53 tee's) and its 13th 20% long (OSM's hole way
starts behind the back tee), and a snaking bunker on its 4th fits its
ellipses at 62%; Kalø's 11th has no green in OSM; the Old Course's 2nd is
62 m short of the championship tee, which OSM does not map; Mollerup Par 3's
card has no lengths but the 6th's.

### Typical workflow

```bash
# 1. Convert, and read the audit and the verification
py -3 osm_golf_convert.py "Skandinavisk Golf Center"

# 2. Fill the gaps in .osm_cache/contact_sheets/<course>_osm_todo.md in OSM,
#    add the course's scorecard to testdata/scorecards.json, and re-import
#    (--refresh to fetch your edits) until the audit has no warnings and the
#    verification no errors

# 3. Only then touch up holes in the editor
open ../hole_editor/hole-editor.html
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

Two lines with one ref that end on the same green are one hole mapped once per
tee set (Kalø's 5th and 7th), not two courses: the back tee's line is kept.

If you actually wanted the other layout, find its own polygon with `--list`
and import that by `--id`.

When another course has its own `leisure=golf_course` beside or inside the
boundary (Mollerup's "Pitch and Putt" is drawn inside Mollerup Golf Club's; St
Andrews' New Course is beside the Old), every golf feature inside a course
drawn within this one, or inside another boundary and outside this one, is
left out, and the run says how many; where two boundaries only overlap at an
edge, features stay. Import the other
course by its own `--id`, with `--course-name` for a better name than OSM's:

```bash
py -3 osm_golf_convert.py --id W143371710                                    # Mollerup Golf Club
py -3 osm_golf_convert.py --id W1010269501 --course-name "Mollerup Par 3"
```

### Courses without hole numbers

Some courses are mapped with no `ref` on anything, so nothing says which hole
is which. The fix is in OSM: tag each `golf=hole` line `ref=<hole number>`
(the audit's `LINE_NO_REF`). Until then, give such a course its holes in play
order under `holes` in its `courses` entry of `osm_golf_config.json`, worked
out from the published scorecard (pars and lengths) and from each green being
a short walk from the next tee:

```json
"mollerup_golf_club": {
  "holes": [
    {"line": "W1010266853", "par": 3},
    {"tee": "W1010269500", "green": "W1010266856", "par": 3},
    ...
  ]
}
```

`line` is a `golf=hole` way. A hole mapped without one takes a `tee` and a
`green`: the tee is a `golf=tee` element, or the hole's fairway, whose point
furthest from the green becomes the tee (a guess, `TEE_GUESSED`: better to
map the hole's line). `par` is used where OSM has none.
Hole lines not in the list are dropped (Mollerup has a 19th line that is on no
scorecard). The direction a line is drawn in does not matter: half of
Mollerup's run green to tee, and each hole is turned to end at its pin.

Mollerup Golf Club's list was matched line by line to the club's course map
(mollerupgolfclub.dk/baneguide), and its verification checks every hole's
length and direction against the club's card and map. Mollerup Par 3 needs no
list: its six hole lines are mapped and numbered in OSM.

### Missing OSM data: the audit

Wherever OSM is silent the converter guesses — a hole with no `golf=hole` line
gets a made-up tee, a line without `par` a par from its length — and a guess
can be wrong without any check noticing (the scorecard checks can't catch a
short hole on a course whose card has no lengths). So every import first
audits the raw OSM features (`osm_audit.py`) and reports each gap as a coded
finding saying what is missing, where, and what to draw or tag:

```
WARN  LINE_OUTSIDE_COURSE: hole 6's line way/1565386487's tee end at (56.211556, 10.190986) is 28 m
      outside the course boundary way/1010269501, so what is mapped there (the tee, water, bunkers) is
      left out of the import: extend the boundary (leisure=golf_course) to take it in
WARN  LINE_NO_PAR: hole 4's line way/1565386485 has no par tag, so par 3 was guessed from its 166 m:
      tag par= from the scorecard
```

The same findings are written as a checklist to
`.osm_cache/contact_sheets/<course>_osm_todo.md`, with a link to every feature
and to the OSM editor at every spot, and the warnings are on the contact
sheet. Fix them in OSM (the map gets better for everyone) and re-import with
`--refresh`; a well-mapped course audits with no warnings. A **warn** is a
guess the import made; an **info** note is only less complete than it could
be. Distances are the `audit` section of `osm_golf_config.json`.

| Code | Level | What is missing, and what to map |
|---|---|---|
| `HOLE_LINES_MISSING` | warn | Fewer `golf=hole` lines than the scorecard's holes (the holes are listed one by one below it) |
| `HOLE_LINE_MISSING` | warn | A green no hole line ends on: draw the line tee to green, with `ref` and `par`. Names unused tees nearby and the fairway leading to it |
| `TEE_GUESSED` | warn | The course config takes a hole's tee from its fairway's far end: map the tee and the line, then drop the hole from the config |
| `GREEN_MISSING` | warn | Nothing tagged `golf=green` at a line's end: the import makes a round green |
| `LINE_OUTSIDE_COURSE` | warn | A hole line's tee or green end lies outside the course boundary, so what is mapped there is left out: extend the boundary |
| `LINE_NO_PAR`, `LINE_NO_REF` | warn | Par guessed from the length, hole numbered from where it lies (info when the config supplies it) |
| `REF_GAP` | warn | No line has some hole number |
| `SCORECARD_MISSING`, `SCORECARD_NO_LENGTHS` | warn | Nothing in `testdata/scorecards.json` to check the import against; research the club's card |
| `SCORECARD_NO_DIRECTIONS`, `LINE_NO_DIST` | info | No directions in the card; no `dist:*` tags on the lines |
| `FAIRWAY_MISSING` | info | A par 4 or 5 whose line crosses no fairway |
| `HOLE_LINE_REVERSED`, `TEE_MISSING` | info | A line drawn green to tee (reverse the way); no `golf=tee` area at a line's start |
| `GREEN_UNUSED`, `TEE_UNUSED` | info | A green or tee no hole uses: a practice green (name it so) or a hole still to map |
| `SECOND_LAYOUT`, `LINE_UNUSED`, `REF_SHARED` | info | Lines left out: another layout in the boundary, or not in the config's hole list; one ref on several lines |
| `FENCE_NO_HEIGHT` | info | A fence without a `height` tag, and the height it was given: tag `height=` |
| `PIN_MISSING`, `COURSE_NAME_MISSING` | info | Pins go in the middle of their greens; an unnamed boundary |

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

- **Green and bunker shapes.** OSM polygons become ellipses (a few for a bent
  bunker), and a DEM cannot see green contours or bunker lips. Expect to touch
  these up; `ZONE_FIT` names the ones that fit worst.
- **Elevation detail.** A 10–30 m DEM gives the landform, not the mounding.
- **Trees.** Positions come from `natural=tree` nodes and planted woodland, so
  they are plausible rather than exact, and are capped per hole. Trees OSM
  doesn't have are better mapped there (`TREES_SPARSE` points at bare sides).
- **Water.** Lakes and creeks become chains of ellipses; check their banks.
- **Fairway width** falls back to 20 m wherever OSM has no fairway polygon, or
  where the polygon is a shared double fairway and therefore not one hole's.
  The fairway ribbon is one width from tee to green, so a wide fairway (OSM
  gives Augusta's 47-70 m) also reaches 25-35 m either side of the tee, where
  a real fairway starts well out from it.

What you should *not* have to redo by hand is the overall layout: hole
positions, lengths, doglegs, par, and the fall of the land.

### Finding an OSM relation ID

Go to [openstreetmap.org](https://openstreetmap.org), search for the course,
click the result, and copy the relation ID from the URL or the sidebar. Pass
it as `--id R<number>`. This is more reliable than name search for courses
with common or ambiguous names.
