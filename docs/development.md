# Build, test and release procedures

Commands below run from the repository root unless a different directory is stated.
Use an explicit absolute working directory for every directory-specific command.
On Windows, use PowerShell-compatible syntax: run commands separately and stop on
a nonzero exit code; append `.exe` to native executable paths as needed.
Publishing changes a running server: use the publish commands only when that
operation is part of the owner's requested task.

```bash
cmake --preset test && cmake --build build/test && ./build/test/golf++-tests
./build/test/golf++-tests "cart"        # only tests whose name contains "cart"
cmake --preset release && cmake --build build/release   # ./build/release/golf++
tooling\release\build_release.ps1                       # the installer and zip, in build/dist
python -m unittest test_osm_golf_convert                # in tooling/osm_import/
py -3 verify_osm_import.py --all                        # in tooling/osm_import/
```

The `test` preset is optimized (`-O2 -g`, asserts on) and runs in well under a
minute; step through code in the `debug` preset. A test over a second is
usually a slow game function: fix the function. The build uses `-Wall -Wextra
-Wpedantic -Wshadow` and stays warning-free: the `test` preset (which every
release builds first) makes warnings errors. Tests use `vendor/doctest.h`
(`TEST_CASE`, `CHECK`, `REQUIRE`). Gameplay tests run on `tests/fixtures/`, not
`assets/`; shipped content is tested in `tests/content_tests.cpp`. Golden shots
(`tests/fixtures/golden_shots/`) run on frozen copies of the tuning, items (the
clubs among them) and rewards: a new tuning field goes in that copy too. Environment variables
(`GOLFPP_COURSE`, `GOLFPP_SCREENSHOT`, `GOLFPP_GAME_TIME`, `GOLFPP_WEATHER`,
`GOLFPP_GOD`, …) are in `docs/performance.md`.

**God menu strike.** F1's STRIKE ME row (Left/Right: dazed or knocked out, Enter: strike) calls lightning down on me, offline or online; online only on a server with `allow_god`, which `tooling/net/dev.ps1` sets on the local one.

**Tuning the look live.** The game reloads `assets/picture/lighting.json`
and `picture.json` when they are saved (their write times polled every half
second, `src/game/look_reload.h`): edit them next to the visual tour. A file
that fails to load keeps the last good look, and the notice says which file
and why until a good one is saved. With the god menu enabled
(`GOLFPP_GOD=1`), F2 in the visual tour opens the look panel
(`src/core/look_tuning.h`): Up and Down pick a number, Left and Right step it
(Shift: a tenth of a step), within the loader's range. Each step is written
straight into `lighting.json`, changing only that number's text, and shown at
once. The rows (a label, a path into `lighting.json`, a step) are
`assets/picture/tuning_panel.json`; `@sun` in a path is the palette key
nearest the sun shown. Development builds read `assets/` in the source tree,
so the panel's edits are the repository's: review them before committing.

A test of something placed, posed or animated in the picture says what a
player would see: project through the camera and check what is in view, which
way it faces and where it sits on screen. A wrong sign must fail a test.

What only a GPU can show is checked by drawing it with the game's own
renderer: after changing the grass, the ground's materials, the trees, the water or the
backdrop, run `cmake --build build/test --target golfpp-picture-check` and
`./build/test/golfpp-picture-check` (all courses, about 25 minutes; name course ids
to narrow it, `--grass`, `--tiles`, `--eyepiece`, `--shadow-receivers`, `--presets` (both scene resolution presets at 720, 1080 and 1440 rows), `--trees` (the trees alone: casters, bare crowns walked past, falling leaves against `falling_leaf.h`, the image gate, and summer and winter pictures of each species in `build/test/picture_check/trees`) or `--water` (which also compares the ground's coarser mirror levels with the full ground) for one check; `--crowd=<label>` and `--against=<label>` compare how a crowd is drawn before and after a change). It must end `all passed`;
its pictures are in `build/test/picture_check/<course>` (`tooling/picture_check/README.md`).

An import must verify with 0 errors before holes are touched up: fix the
importer, not its output. Where OSM lacks information, `osm_audit.py` says what
the owner should map; overrides in `osm_golf_config.json` are a stop-gap.

`WATER_SHORE_RELIEF` warns when a pond's sampled shoreline elevation varies
by more than `checks.water_shore_relief_warn_m` (default 2 m): lowering land to
the lowest shore can carve a crater. Review its outline and elevation before
changing content. A confirmed real feature can be recorded in that course's
`checks.reviewed_water_shores` as `{ "review id": "reason and evidence" }`;
the printed review id binds to the shoreline samples and heights, so changed
geometry or elevation requires review again. Accepted findings remain notes.
These are heuristic warnings, not a simulation of the final ground mesh;
connected ponds and smoothing can cause additional cuts. Run regression checks
with `py -3 -m unittest test_osm_water_checks` in `tooling/osm_import/`.

The ground importer detects enclosed, repeated-height DEM pits: at least twelve
equal samples, with a rim whose tenth percentile is over 4 m higher and median
over 6 m higher. It interpolates these only at course-specific
`ground.reviewed_dropouts` locations (`position` in course XZ, `reach_m` from the
detected centre, and a required `reason`). Other detected pits fail verification
as `TERRAIN_DROPOUT`; real quarries and steep basins must not be repaired merely
for looking unusual. The reviewed inland pond errors affect both Hedensted and
both Himmerland layouts. `--ground-only COURSE_ID` refits only the ground grid to the map's paper
round the course's placed holes (offline from the cache, leaving the rest of the course as it is).
Outline generation uncrosses folded edges while retaining their vertices;
verification rejects remaining crossings as `ZONE_CROSSING`. Terrain and outline
regressions: `py -3 -m unittest test_osm_terrain_quality` in `tooling/osm_import/`.

The server module (from PowerShell, after `. <emsdk>\emsdk_env.ps1`):

```powershell
spacetime build --module-path server/golfpp_module
spacetime start                                           # a local server, in another terminal
spacetime publish golfpp --server local --bin-path server/golfpp_module/build/lib.wasm
tooling\net\check_determinism.ps1 -Emsdk <emsdk>         # golden shots, drives, walks and flights with contacts, native vs WASM
tooling\net\check_determinism.ps1 -SelfTest               # that comparison fails on perturbed rows (native only)
spacetime generate --lang rust --bin-path server/golfpp_module/build/lib.wasm --out-dir net/client_bridge/src/module_bindings
```

`.\tooling\gb -r` (`-rr` stops a running copy first) brings up the local server
(`tooling\net\dev.ps1`) and launches one client on it, so PLAY OFFLINE and PLAY
ONLINE both work; if the server fails it still launches for offline play.
`.\tooling\gb -m` runs local multiplayer (server, publish, two anonymous
clients). Both wait for the game to close and then stop the server if they
started it (one already running is left alone); `.\tooling\gb -x` stops the
clients and the server. A release build
(`tooling\release\build_release.ps1`) also publishes its module locally through
`dev.ps1`. Hosting is `server/hosting.md`, shipped
to owners who run their own server. The module's plain C++ rule `.cpp` files in
`server/golfpp_module/src` (excluding `lib.cpp` and `module_cache`) are in the
native test build. `tee_schema.h`, `tee_support.h` and `tee_reducers.h` are
SpacetimeDB adapters included only in `lib.cpp`; native `tee_rules_tests` test
the shared UTC guards and lane placement, and `net_client_tests` exercise
booking snapshots, commands and reconnect through the paired C bridge. The module compiles content in: republish it after content
changes. Online needs Rust (`net/client_bridge`; on Windows the target
`x86_64-pc-windows-gnu`, on macOS `aarch64-apple-darwin` and
`x86_64-apple-darwin`); the test preset builds with `-DGOLFPP_NET=OFF` and
links `src/net/` against `tests/fake_stdb_bridge.h`. `--server <uri> --db
<name> --anonymous` point the game at a server.

The native `"sync:"` tests run a client and observer against the module's
movement and cart rules with latency, reordering, loss and disconnects, including a seeded
three-minute soak with 5% loss and 200 ms jitter. The real SDK/database lifecycle
check is `tooling/net/online_check/README.md`: six anonymous clients enter the
hub, book and travel by accepted walking inputs, dap and play at a natural UTC
quarter hour. Use a new disposable local database, never the existing `golfpp`
database. Its fail-fast diagnostics and nonzero exit status are release evidence;
the fake bridge and native rules alone do not execute database transactions.

The version is `project(VERSION)` in `CMakeLists.txt`. Players' builds set
`GOLFPP_NO_CONSOLE` (errors only in SDL's log): debug from the presets.

**Flag any new dependency first.** Approved: the SpacetimeDB CLI, C++ bindings
and `spacetimedb-sdk` (pinned to the CLI's version), the Emscripten SDK (and its zlib port, which inflates the module's embedded content), the
crates in `net/client_bridge/Cargo.toml`, SpacetimeAuth, Inno Setup 6 and
Python for releases, GitHub Actions for the macOS and Linux releases, and the
pinned SDL2, SDL2_mixer and GLM sources in `tooling/release/build_deps.sh`,
and numpy, scipy and Pillow for the importer's aerial reading (tooling only).
The Steamworks SDK is approved but **blocked** until the owner has a Steam
app id.

Hub flow regression coverage lives in `tests/hub_flow_tests.cpp` (physical
approach/model projection, book controls and pending acknowledgements, warmup and
first-ball adoption, leave-menu preservation and aspect-ratio layout). Travel and
reconnect snapshot boundaries also run through `online_session_tests` and the fake
bridge (`tests/hub_built_tests.cpp`: the built plaza and range, the departure board's text, the boarding cues and the bus's open door). Regenerate the code-drawn hub models with
`py -3 tooling/practice_ground/make_hub_props.py` (`--check` verifies the shipped
JSON; `MODEL --stage NAME` writes one build stage), then photograph them:
`cmake --build build/test --target golfpp-preview-prop` and
`py -3 tooling/art/preview_prop/preview_prop.py hub_bus --out build/prop_preview/bus`
(`views.png` holds the four inspection views; `rear.png`, `board.png` extra; the bus's doors: set `GOLFPP_PREVIEW_DOORS=0..1` for how far through their swing). Open
every view after each change. These models' bounds are compiled into server
content, so rebuild the module after changing them, and copy them to the frozen
golden props in `tests/fixtures/golden_shots/world/props/`.

Social regressions are `tests/social_dap_tests.cpp`: shared validation boundaries,
clock/ack/refusal/session behavior, both recipients' effective XP, paired snapshots,
and projected fist contact/orientation for both hands at 4:3, 16:9 and 21:9.
`net_client_tests` also checks the actual C transport and scoped subscription.
The SDK dap adapters run in `online_check` with two participants and an observer;
both recipients must receive exactly one reward per accepted row. Native tests
cover individual XP effects and exact duration/expiry boundaries. Named-account
reconnect and full-round completion need their own live evidence when changed;
anonymous lifecycle checks cannot prove named persistence.

**Disposable local server (owner decision).** `tooling/net/dev.ps1` republishes
`golfpp` whenever the built module changes. It first tries migration; if refused,
it republishes with `--delete-data --yes`, then restores anonymous login support
and regenerates bindings. Local accounts, bookings and scores may be deleted.

**Cross-platform release checks.** `.github/workflows/release.yml` builds and tests
on macOS 15 (Apple Clang) and Ubuntu 22.04 (GCC) for release tags and manual
runs only. Releases follow the friends' playtest schedule; ordinary pushes and
pull requests do not trigger packaging. A manual run with a blank release_tag
can check both platforms ahead of a playtest. Windows alone cannot validate
another compiler, standard library or OS. Each successful tag job attaches its
own package, independently of the other platform.
To repair one platform on an existing alpha release, run the workflow from the
fixed main revision with `platform: macos` (or `linux`) and `release_tag` set to
the existing tag. It tests and packages the selected revision, then replaces
only that platform's asset; it does not move the tag. Keep project(VERSION)
matching the release being repaired. The Actions run records the actual source
revision. A blank release_tag uploads run artifacts only.
