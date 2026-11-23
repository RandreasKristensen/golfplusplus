# Content and licensing rules

Read before changing the corresponding systems, including callers and content.
Paths are relative to the repository root.

**Content is data.** No gameplay numbers, rewards, item stats, sound choices or
text in C++: tuning in `assets/play/tuning/`, rewards and skills in
`assets/play/progression/`, items (what they do, how they are used, their
sounds) in `assets/play/items/`, a model's colours, shape, points and mounts in
its prop file, scene colours and light in `assets/picture/lighting.json` (the
printed maps' inks, shared by the course map and the hole signs, in its `map`
section). A
new cigarette, beer, cart or trinket is files, never C++. Loaders never invent
fallbacks: missing content fails loudly at startup. Only an omitted optional
field takes its default; a present but malformed one (a tree, road, zone or
number) refuses its whole file, naming the field (`parsed<T>` in
`src/game/json_util.h`). One invalid course manifest or a duplicate course id
refuses every course, in the game and the server alike (`parse_courses`).

**Text is data.** Strings in `assets/interface/text/en.json`, styles in
`styles.json`, both through `src/game/text_ids.h`; sounds through
`src/audio/sound_ids.h`. Tests check every id exists. HUD text is the bitmap
font only (UTF-8, keyed by code point). The only text outside the table is the
god menu's profiler overlay.

## Content and licensing

`assets/` holds all content by what it is for: `world/` (courses, a folder
each with `course.json`, `world.json`, `holes/` and backdrops; `props/`;
`ground/`), `play/` (tuning, items and clubs, progression, `online.json`), `picture/`
(lighting, picture, shaders), `sound/`, `interface/` (text, font, settings,
icons). New kinds of content go in the folder of what they are for, never a
new top-level one. Holes and course worlds come from `tooling/osm_import/`
and are touched up in `tooling/hole_editor/`; course worlds' tooling-only data
(`walking_shortcuts`, `spawn_zones`, `interactables`) awaits one data-driven
interaction system. Every place manifest (`course.json`, `hub.json`) carries
`rules` (see the gameplay rules); the importer writes them for courses.

- No trademarked course names in player-facing text, store material or file
  names.
- Never remove or hide attribution: OpenStreetMap (ODbL), the Danish Elevation
  Model (Klimadatastyrelsen, CC BY 4.0) and the AWS Terrain Tiles and their
  sources, in the main menu's credits and `THIRD_PARTY.txt`. The
  Dataforsyningen token lives only in the gitignored
  `tooling/osm_import/dataforsyning_token.txt`.
- All art is drawn by code: no image files from elsewhere, no AI image tools.
  Sounds in `assets/sound/` are placeholders (silent `.wav` from
  `tooling/audio/make_placeholders.py`, described as `Placeholder ...`). Record the
  source and licence of any asset not made by code; commercial use only.
- Every shipped or compiled-in library has its licence in `THIRD_PARTY.txt`
  (`tooling/release/third_party.py`; native libraries in its
  `NATIVE_PACKAGES` and `third_party_header.txt`).
- Flag AI-generated art, audio, text or trailer content to the owner (Steam
  requires disclosure). AI-written code, and what it draws, needs none.

Every place has a `world.bus_stop` {position, heading}: the importer chooses each
course's (`tooling/osm_import/osm_bus_stop.py`: a car park, else a service road,
nearest the first tee and a bus length clear of play, solids and cart paths, with
a path to the first tee); no clear spot is an import error. A course whose own
boundary has no clear car park or service road uses its club's, through a per-course
`bus_stop.reach_m` in `osm_golf_config.json` (Jelling Par 3, Marienlyst). The practice ground
generator authors the hub's, and the tee book's `world.hub.book` anchor. The bus
stands on a pad flattened like a tee; its box, turned with its heading, is in the
one solids list.

`tooling/practice_ground/make_hub_props.py` draws the tee book and course bus as
JSON boxes, built and checked stage by stage with the prop preview
(`tooling/art/preview_prop/`). Named points (`approach`, `door`, `body_min`,
`body_max`, the bus's `wheel_contact_*`, `engine`, `horn` and `door_hinge`, the bus's `numbers` `door_swing_seconds`/`door_open_degrees`, `board_center`/`board_size`/`board_normal`)
define placement, collision and where the renderer writes board text (the bus's
destination, TEE BOOK) with the sign text path; no letters are geometry. These
models are simulation content because their bounds enter `play_area::solids`.

Dap reach, duration, timeline and hand geometry are the required `dap` section
of `play/tuning/player.json`; `dap_xp` in progression rewards pays `social`.
It uses the standard level curve and existing generic icon. Both files are
already simulation fingerprint inputs and embedded in the server.

The hub generator lays out one compact place (north is -Z): a road along the
south and one leaving the map, the parked bus facing north with its door as every
arrival (`arrival` {position, heading}), the starter hut and tee book ten metres
from it, one 40 x 25 m putting green (nine cups, a tier and a break), then the
plaza and clubhouse pad, one chipping green (a deep and a shallow bunker, an
uphill and a downhill bank made in `land_height`), and one range tee line of 20
bays 3.5 m apart facing north into one shared field of target greens at 50-250 m.
Players may walk onto the field. Each target has a `board` position for its
distance board. Green speeds are `green_stimp_feet` on a green zone. The first
nine `practice_cups` are the putting green's, the last the chipping green's. Tee
mats share tee-pad flattening; the hub's authored land is used at its authored
height. Walking paths are cart roads (width 2) from the bus door to the book,
putting green, short game, tee line and the hill ramp; the generator's
`check_layout` and a shipped-content test hold the walking budgets along them
(book ~10 m, putting green 15-25, short game 40-50, tee line 60-70, hill tee
within 120) and keep every firing line off paths and pads. The hub manifest's
`hub.pads` are named, loaded footprints (`position`, `reach` radius): the built
things' (starter hut, clubhouse, clocks, departure board) and the reserved ones
(plaza, the clubhouse's rooms, caddie shack, cart barn) and the car park, which is built. `hub.props`
places every built thing (model, ground `position`, `heading`, raised `height`) from
the code-drawn models of `make_hub_props.py` (kinds `hub_fixture`, `hub_sign`,
`hub_label` in `model_kinds.cpp`; a fixture's `body_min`/`body_max` is one box in the one
solids list, a label is a board with none): the starter hut with the book on its counter,
PA pole, bin and bench; the clubhouse shell (lit windows, terrace tables, flagpole); two
clocks that write the in-game time the camcorder datestamp shows; the departure board
(a title and four `hub_departure_row` labels: online the next four booked groups, each its
hub-clock time and course over the names in it, cut to the first name and how many more
when the row is full; offline my booking); the PA pole (its `speaker` point is where the
boarding call comes from) and the hub's looping ambience (`hub.json` `ambience`: birds in
the shelterbelts, chatter on the terrace); a paved bus bay (a 4.6 m cart road) from the
bus to the road; and on the
range a divider between bays (0.9 m, a shank stops on it), a bucket per bay, benches, bag
stands, a ball dispenser at each end of the line, six parked cars on the car park's surface (a
14 m cart road off the road; four code-drawn 1989-style models, solid boxes) and a distance board by every target
(`distance_meters`, from the nearest bay of its tee line). The tee line is one levelled strip
per line (`tee_strips`), not a box per bay; `floodlights` are authored poles behind it. The
practice cups' flags are short (`practice_pin_visual_height_meters`). Bunker depth is global tuning; the generator
makes one deep and one shallow by sinking or raising the land under it.
The hub's edges are planted by `tooling/practice_ground/hub_planting.py` (fixed seed, existing species only)
into the world's `trees` and `bushes` (the same arrays as a course's; `bushes` are a position, radius and
height, scrub unless a `species` is named): shelterbelts of mixed broadleaf with some spruce and scrub west
and north beyond the nets, a wooded crest on the east ridge, and clipped beech hedges (`hedge`) along the
south roads. `check_layout` and a shipped-content test keep every
plant off the range and its nets, the hill terrace and ramp, the plaza and facilities, paths, pads and
each firing line's flight width (10 m plus 12% of its length). The roads run off the grid's west and east edges.
The hill tee sits at the east end of the same tee line, beside the bays, on a ridge
that closes the east edge, reached by the ramp path; its shots fire into the same
field and no ball passes over another golfer.
The elevated tee targets the owner's 15-25 m drop; lower targets play shorter
because the unchanged ball simulation carries a given launch farther downhill.

Hub path lamps are generated from walking routes at 20 m target spacing, offset off paved ways and firing lines; every route joint is within 15 m of a post. `hub_path_lamp` supplies its warm light, cone and reach as model data to the existing night-light candidates. Its body bounds enter the shared solids list. The tee book is an open spread on the starter counter; its title is mounted above the window, with the original interaction anchor preserved.
