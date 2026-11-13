# Content and licensing rules

Read before changing the corresponding systems, including callers and content.
Paths are relative to the repository root.

**Content is data.** No gameplay numbers, rewards, item stats, sound choices or
text in C++: tuning in `assets/play/tuning/`, rewards and skills in
`assets/play/progression/`, items (what they do, how they are used, their
sounds) in `assets/play/items/`, a model's colours, shape, points and mounts in
its prop file, scene colours and light in `assets/picture/lighting.json`. A
new cigarette, beer, cart or trinket is files, never C++. Loaders never invent
fallbacks: missing content fails loudly at startup.

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
interaction system.

- No trademarked course names in player-facing text, store material or file
  names.
- Never remove or hide attribution: OpenStreetMap (ODbL), the Danish Elevation
  Model (Klimadatastyrelsen, CC BY 4.0) and the AWS Terrain Tiles and their
  sources, in the main menu's credits and `THIRD_PARTY.txt`. The
  Dataforsyningen token lives only in the gitignored
  `tooling/osm_import/dataforsyning_token.txt`.
- All art is drawn by code: no image files from elsewhere, no AI image tools.
  Sounds in `assets/sound/` are placeholders. Record the source and licence of
  any asset not made by code; commercial use only.
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
`body_max`, the bus's `wheel_contact_*`, `board_center`/`board_size`/`board_normal`)
define placement, collision and where the renderer writes board text (the bus's
destination, TEE BOOK) with the sign text path; no letters are geometry. These
models are simulation content because their bounds enter `play_area::solids`.

Dap reach, duration, timeline and hand geometry are the required `dap` section
of `play/tuning/player.json`; `dap_xp` in progression rewards pays `social`.
It uses the standard level curve and existing generic icon. Both files are
already simulation fingerprint inputs and embedded in the server.

The hub generator authors a flat, tiered and breaking short-game green, with
separate green speeds (`green_stimp_feet` on a green zone), six flat range bays
and three hill bays. Ranges fire away from the short game and arrival. Tee mats
share tee-pad flattening; the hub's authored land is used at its authored height.
The first cart-road polyline is the walking approach behind the hill's tee line.
Arrival, book and bus sit together behind the flat firing line beside the
short-game approach; their access paths bypass the lectern and parked bus.
The elevated tee targets the owner's 15-25 m drop; lower targets play shorter
because the unchanged ball simulation carries a given launch farther downhill.
