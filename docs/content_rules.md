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
