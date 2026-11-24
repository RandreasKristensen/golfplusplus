# Courses as server data, not module code

The module compiles every course in (`server/golfpp_module/embed_content.cmake`,
gzipped and inflated by `module_cache.cpp`). That stops scaling: content grows
with every course, and Maincloud refuses large module uploads.

## What we found (2026-11-24, releasing 0.3.0)

- Maincloud's publish failed with `413 Payload Too Large` on a 63 MB module;
  0.2.1's ~35 MB module published. The exact limit is not documented: ask
  support@spacetimedb.com. A local `spacetime start` server took 63 MB.
- 58 MB of it was course JSON, mostly `world.json` (Hedensted alone 8.4 MB).
  Minifying saves under 10%; gzip -9 takes all of `assets/world/` from 60 MB
  to 13 MB, which is today's stopgap (module ~18 MB, ~4.5x headroom).
- The limit is on the module upload, not the database: tables hold gigabytes
  and a module may use up to 8 GB of memory (spacetimedb.com/limits).
- At 100x the courses (~6 GB of JSON) even an unlimited upload fails: the
  module inflates and keeps every course in memory at first use. Courses must
  load on demand.
- `server_content_hash` (`src/game/content_hash.h`) fingerprints every course
  file, so a server cannot simply carry fewer courses than the game.

## Plan

- [ ] Course tables: one row per course file (or chunk), keyed by path, with
  its content hash. An admin reducer uploads them in chunks under the request
  size limit; a tooling script uploads `assets/world/courses/` after publish.
- [ ] The module keeps only play content (tuning, items, progression, props,
  hub) embedded; `module_cache` builds a course from its rows when a room first
  needs it and may drop idle ones.
- [ ] Compatibility: hash play content as today, and each course on its own, so
  a game only refuses courses whose files differ, not the whole server.
- [ ] A compact binary course format for terrain and geometry instead of text
  floats in `world.json` (shrinks downloads, uploads and parse time); the
  importer writes it and the game and server read the same file.
- [ ] Hosting: document the upload step in `server/hosting.md` and
  `server/README.md`; the release script runs it for the local server.

## Done when

The module's size no longer depends on the number of courses, a new course
reaches a running server without republishing the module, and the online check
(`tooling/net/online_check/`) plays a round on an uploaded course.
