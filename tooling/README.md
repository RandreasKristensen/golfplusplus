# golf++ — tooling

Design and import courses for golf++. The tooling folder contains two things:
a browser-based hole editor for building and tweaking holes visually, and a
command-line script that pulls real course data from OpenStreetMap and converts
it directly into hole JSON files.

---

## hole-editor.html

A single-file, no-install web tool for authoring hole JSON by hand. Open it
directly in any browser — no server required.

```
open hole-editor.html
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

## osm_golf_convert.py

Queries OpenStreetMap via the Overpass API and converts a real golf course into
hole JSON files and a course manifest, ready to load directly into the game or
open in the editor.

### Requirements

```bash
pip install requests  # optional; falls back to Python's standard library
```

### Usage

```bash
# Search by course name
py -3 osm_golf_convert.py "Marienlyst Golf Klub"

# Nearest course to a lat/lon — useful when you're standing on the course
py -3 osm_golf_convert.py --lat 56.180454 --lon 10.156731

# By OSM relation ID — most reliable; find it at openstreetmap.org
py -3 osm_golf_convert.py --id R3456789

# Custom output directory
py -3 osm_golf_convert.py "Aarhus Golf Klub" -o ../assets/holes --course-out ../assets/courses

# Skip writing the course manifest
py -3 osm_golf_convert.py "Aarhus Golf Klub" --no-course

# Use generator tuning, including tree dimensions and hub path filtering
py -3 osm_golf_convert.py "Marienlyst Golfklub" --config osm_golf_config.json
```

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

Use area select in either hole view or world view to drag a selection rectangle
around editable points/items. The editor highlights everything inside the box
and can bulk-delete selected route points, trees, zones, collectibles, and
interactables. Fixed anchors such as tees, pins, spawn, and hole starts are
selectable for inspection but are not bulk-deleted.

Save hole, save world, and save config are separate on purpose. Hole JSON uses
per-hole local coordinates, while world JSON uses shared course coordinates.

### Output

Running the converter from `tooling/` writes directly to the game's asset
folders by default:

```
../assets/holes/
  aarhus_golf_klub_h01.json
  aarhus_golf_klub_h02.json
  ...
../assets/courses/
  aarhus_golf_klub.json          ← course manifest
```

The course manifest follows the same format as the hand-authored course files:

```json
{
  "id": "aarhus_golf_klub",
  "name": "Aarhus Golf Klub",
  "hole_count": 18,
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

### Typical workflow

```bash
# 1. Convert
python osm_golf_convert.py "Skandinavisk Golf Center"

# 2. Open the editor and paste in a hole to review and fix up
open hole-editor.html
# → import → paste hole JSON → adjust spline / zones → export

# 3. Save the cleaned JSON back to ../assets/holes
```

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

### Finding an OSM relation ID

Go to [openstreetmap.org](https://openstreetmap.org), search for the course,
click the result, and copy the relation ID from the URL or the sidebar. Pass
it as `--id R<number>`. This is more reliable than name search for courses
with common or ambiguous names.
