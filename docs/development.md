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

An import must verify with 0 errors before holes are touched up: fix the
importer, not its output. Where OSM lacks information, `osm_audit.py` says what
the owner should map; overrides in `osm_golf_config.json` are a stop-gap.

The server module (from PowerShell, after `. <emsdk>\emsdk_env.ps1`):

```powershell
spacetime build --module-path server/golfpp_module
spacetime start                                           # a local server, in another terminal
spacetime publish golfpp --server local --bin-path server/golfpp_module/build/lib.wasm
tooling\net\check_determinism.ps1 -Emsdk <emsdk>         # golden shots, drives and walks, native vs WASM
spacetime generate --lang rust --bin-path server/golfpp_module/build/lib.wasm --out-dir net/client_bridge/src/module_bindings
```

`.\tooling\gb -m` runs local multiplayer (server, publish, two anonymous
clients); `.\tooling\gb -x` stops it. Hosting is `server/hosting.md`, shipped
to owners who run their own server. The module's plain C++ (all of
`server/golfpp_module/src` but `lib.cpp` and `module_cache`) is in the native
test build. The module compiles content in: republish it after content
changes. Online needs Rust (`net/client_bridge`; on Windows the target
`x86_64-pc-windows-gnu`, on macOS `aarch64-apple-darwin` and
`x86_64-apple-darwin`); the test preset builds with `-DGOLFPP_NET=OFF` and
links `src/net/` against `tests/fake_stdb_bridge.h`. `--server <uri> --db
<name> --anonymous` point the game at a server.

The native `"sync:"` tests run a client and observer against the module's
movement and cart rules with latency, reordering, loss and disconnects, including a seeded
three-minute soak with 5% loss and 200 ms jitter. `online_check` also enters
the wheel, drives and steps out, checking the parked row and sync counter,
and walks, checking the server's steps put it where its own did.

The version is `project(VERSION)` in `CMakeLists.txt`. Players' builds set
`GOLFPP_NO_CONSOLE` (errors only in SDL's log): debug from the presets.

**Flag any new dependency first.** Approved: the SpacetimeDB CLI, C++ bindings
and `spacetimedb-sdk` (pinned to the CLI's version), the Emscripten SDK, the
crates in `net/client_bridge/Cargo.toml`, SpacetimeAuth, Inno Setup 6 and
Python for releases, GitHub Actions for the macOS and Linux releases, and the
pinned SDL2, SDL2_mixer and GLM sources in `tooling/release/build_deps.sh`,
and numpy, scipy and Pillow for the importer's aerial reading (tooling only).
The Steamworks SDK is approved but **blocked** until the owner has a Steam
app id.
