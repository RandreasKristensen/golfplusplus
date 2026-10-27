# AGENTS.md — golf++

The code is the documentation. This file only holds what the code cannot tell
you: intent, hard rules, where to start reading, and how to check your work.
Every header's first comment says what the file is for; read the header of
anything you touch before changing it.

For ANY directory-specific command, write the full directory. The repo root is
`C:\Users\arand\Desktop\AU\sjov\golfplusplus`.

**Source control: work directly on `main`, in the working tree.** Never create,
switch or merge branches, and never `git add`, commit, push or stash. The owner
does all source control. Read-only git commands (`status`, `diff`, `log`) are
fine.

---

## What this is

A lo-fi 3D golf game in C++17 that looks like a 1989 camcorder tape played on a
small TV. Courses are real golf courses imported from OpenStreetMap, each one
continuous area on its real land: holes are played where they sit, so other
groups stay visible and a stray shot lands on the next hole. Walk or drive a
cart between holes, pick up collectibles, level RuneScape-style skills. There
is one surface, `build_ground` (`src/physics/ground_mesh.h`): every height and
normal comes from it, except on a tee box (`src/game/tee_box.h`). Courses
should be right out of the importer; the hole editor is for touch-ups.

Golf simulation first, RPG progression second. Online is the main mode: rooms
of up to 40 players per course on SpacetimeDB, groups of up to 4 sharing a
scorecard, each player with their own ball at their own pace. Offline stays a
complete solo mode. Everything is unlocked today: no money, shop, quests or
unlock gating.

**Real golf decides.** Whenever something is unspecified (a rule, a number, a
behaviour, a penalty, what a player can do), assume what real golf does: the
Rules of Golf, real courses, real equipment, real physics from published
measurements. A departure from real is a deliberate design choice: named in
data or code (like `cup_leniency`), documented where it lives, and tested as
such. When unsure what real golf does, look it up rather than guess.

**Stage: early alpha.** Nothing depends on it. Breaking changes need no
caution: change the save, the server's tables and reducers, the protocol and
the content's formats freely, in one go, with no shims, migrations or staged
rollouts. Still bump `protocol_version` when the server changes, and still
follow the rules below. This lasts until the owner says otherwise.

## Where to start reading

| To change… | Start at |
|---|---|
| Frame loop, saving, audio dispatch | `src/core/app.cpp` |
| Keys → game actions | `src/core/key_bindings.cpp` |
| Menus (new screens go here) | `src/core/startup_flow.h`, `src/core/online_menus.h`; drawn by `src/renderer/menu_overlay.h` |
| What the renderer gets each frame, camera rigs | `src/core/render_frame.h`, `src/renderer/render_data.h` |
| Per-frame gameplay | `src/game/game_state.h` |
| Courses, holes, the area played on | `src/game/course_session.h`, `src/game/play_area.h`, `src/game/course_loader.h`; content in `assets/world/courses/` |
| A shot's flight and playback | `src/game/shot_simulation.h`, `play_shot` in `src/game/game_state.h`; `src/physics/` |
| Offline vs online: progress, net commands, motion | `src/game/mode_dispatch.h`, `src/game/net_types.h`, `src/game/motion_sync.h` |
| The connection, my room's rows, rejoining | `src/net/net_client.h` over `src/net/stdb_bridge.h` (Rust in `net/client_bridge/`), `src/net/room_rows.h`, `src/core/online_session.h`; `assets/play/online.json` |
| An online round; other players as shown | `src/game/online_play.h`; `src/game/remote_players.h`, `src/core/render_online.h` |
| The server: tables, views, reducers, its rules | `server/golfpp_module/src/lib.cpp`, `account_rules.h`, `play_rules.h`, `server_content.h` |
| Props drawn in the world | their files in `assets/world/props/` (`src/game/prop_loader.h`), `src/renderer/prop_pieces.h` |
| The golf cart | `src/game/golf_cart.h`, `src/game/cart_seats.h` (shared with the server), `src/renderer/cart_batch.h` |
| Hole signs, tee boxes, floodlights, fences | `src/game/hole_sign.h`, `src/game/tee_box.h`, `src/game/floodlights.h`, `src/physics/fence_collision.h` |
| What players and carts bump into | `src/game/area_obstacles.h`, `src/game/body_contact.h`, `src/physics/body_collision.h` |
| Course map (M), rangefinder (Left Shift), god menu (F1) | `src/renderer/course_map_overlay.h`; `src/game/rangefinder.h`, `src/game/sight_line.h`; `src/core/god_menu.h` |
| Settings | `src/core/settings_menu.h`, `src/game/settings.h`, `assets/interface/settings.json` |
| XP, collectibles, completion; rewards and skills | `src/game/progress_rules.h`; `assets/play/progression/` (`src/game/reward_rules.h`) |
| Feel numbers | `assets/play/tuning/*.json` (`src/game/game_tuning.h`) |
| HUD, scorecards | `src/renderer/hud_overlay.cpp`, `scorecard_overlay.cpp`; GL in `src/renderer/renderer.cpp` |
| Text size and layout | `src/renderer/pixel_font.h`, `assets/interface/text/styles.json` |
| Content formats | the loader next to it (`src/game/*_loader.cpp`, each with `parse_*_from_text`) |
| The weather and what it does to a shot | `src/game/weather.h`, `src/physics/wind.h`, `course_weather_at` in `src/game/mode_dispatch.h` |
| The weather as drawn | `src/core/weather_frame.h`, `src/core/lightning_frame.h`, `src/game/weather_lighting.h`, `src/renderer/weather_renderer.h` |
| Time of day, sun and moon | `src/game/sky_clock.h`, `src/core/sky_frame.h` |
| Scene colours, light, haze | `assets/picture/lighting.json` (`src/game/lighting_loader.h`, `src/game/sun_lighting.h`), `assets/picture/shaders/lighting.glsl`, `src/renderer/lighting_uniforms.h`, `src/renderer/sky_visibility.h` |
| Shadows and night lights | `src/core/light_frame.h`, `src/renderer/shadow_map_pass.h`, `src/renderer/scene_lights.h`, `src/renderer/blob_shadow_renderer.h` |
| The sky | `src/renderer/backdrop_pass.h`, `assets/picture/shaders/backdrop.frag` |
| The picture: line count, camcorder, tape, TV, datestamp | `assets/picture/picture.json`, `src/renderer/picture_size.h`, `camcorder_pass.h`, `tape_pass.h`, `tv_pass.h` (in `src/renderer/`), `src/core/vcr_frame.h` |
| Static meshes on the GPU | `src/renderer/terrain_renderer.h` |
| Backdrops, ground textures | `tooling/art/make_art.py` |
| Tooling (OSM importer, hole editor, art) | `tooling/README.md` |
| Shots natively vs in WASM | `tooling/net/check_determinism.ps1` |
| What players download | `tooling/release/build_release.ps1` (Windows, the server package), `build_release.sh` and `build_deps.sh` (macOS, Linux; run by `.github/workflows/release.yml`), the `install` rules in `CMakeLists.txt` |

Reuse instead of writing your own: `src/physics/vector_math.h` (yaw,
horizontal distance, safe normalize, `clamp01`; `glm::radians` and
`glm::pi`, never a local `pi`), `src/game/json_util.h` (every JSON read),
`src/game/content_files.h` (every file read and directory scan: loaders parse
text, so the server parses the same content without a filesystem),
`tests/test_support.h` (fixtures, `started_hole()`, `started_game()`,
`near()`), `src/renderer/camera_local.h` (placing things in a thing's frame).

Every local frame (a prop's, a cart's, a figure's, the camera's) is **x left,
y up, z forward**: a plain rotation, +x on the screen's left seen from behind.
Pose a thing by where its sides point (`posed_model`), never Euler angles.

## Build and test

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
(`tests/fixtures/golden_shots/`) run on frozen copies of the tuning, clubs and
rewards: a new tuning field goes in that copy too. Environment variables
(`GOLFPP_COURSE`, `GOLFPP_SCREENSHOT`, `GOLFPP_GAME_TIME`, `GOLFPP_WEATHER`,
`GOLFPP_GOD`, …) are in `docs/performance.md`.

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
tooling\net\check_determinism.ps1 -Emsdk <emsdk>         # golden shots, native vs WASM
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

The version is `project(VERSION)` in `CMakeLists.txt`. Players' builds set
`GOLFPP_NO_CONSOLE` (errors only in SDL's log): debug from the presets.

**Flag any new dependency first.** Approved: the SpacetimeDB CLI, C++ bindings
and `spacetimedb-sdk` (pinned to the CLI's version), the Emscripten SDK, the
crates in `net/client_bridge/Cargo.toml`, SpacetimeAuth, Inno Setup 6 and
Python for releases, GitHub Actions for the macOS and Linux releases, and the
pinned SDL2, SDL2_mixer and GLM sources in `tooling/release/build_deps.sh`.
The Steamworks SDK is approved but **blocked** until the owner has a Steam
app id.

---

## Hard rules

**Physics (`src/physics/`) is pure.** Inputs by value or const reference, a new
value out: no mutated parameters, globals, `static` locals, I/O or profiling.
`simulate_shot` keeps the same rules and runs a whole shot in fixed steps;
`game_state` only plays the result back. Nothing that affects results depends
on frame `dt`. Everything builds with `-ffp-contract=off`, so native and WASM
round the same.

**No globals or singletons.** `app` owns everything and passes it by
reference. Exceptions: the GL function table (`src/renderer/gl_loader.h`) and
the server's `module_cache.h` (parsed content and built courses, never game
state).

**The server plays by the game's rules.** Online, the server decides shots,
strokes, completions and progress with the game's own code (`simulate_shot`,
`progress_rules`, `round_state`, the `play_area` helpers). What only the
server checks (names, logins, speed, reach, shot validity) lives in the
`*_rules` files next to `lib.cpp`, limited by the tuning's `server` section.
Reducers fail with ids from `src/game/server_errors.h`, shown from the string
table. Read the caller's token with `connection_jwt_payload`, never
`AuthCtx::get_jwt`. A mid-hole disconnect abandons the hole: scores exist
only for completed holes.

**Game and server stay in step.** A game plays online only on a server with
its own `protocol_version` (`src/game/net_types.h`), reported in the one-row
`server_protocol` view, which never changes. Every change to the module's
tables, views or reducers bumps it and regenerates the bindings. Release the
module first, then the installer from the same commit (`server/README.md`).

**Content is data.** No gameplay numbers, rewards, item stats, sound choices or
text in C++: tuning in `assets/play/tuning/`, rewards and skills in
`assets/play/progression/`, a prop's colours and shape in its prop file, scene
colours and light in `assets/picture/lighting.json`. Loaders never invent
fallbacks: missing content fails loudly at startup.

**The weather is shared, replayable and in play.** A course's weather is a pure
function of the course and UTC time (`weather_at`), the same for everyone: no
row, no save field. A shot flies in the weather at `shot_input::shot_at`; the
server checks that time against its clock and works the weather out itself.
The weather code is plain arithmetic (no sine, cosine or powers) so WASM
matches. The god menu's weather is offline only.

**Time of day is visual only.** It never changes physics, scoring or rewards.
It is the `sky_clock` timeline at UTC now (online, the server clock estimate,
`shared_unix_ms`); each course stays on its course file's date. No save field,
no row. A gameplay rule that ever depends on it uses `game_day_at`, never the
sun's trigonometry.

**One lighting path.** All scene light and haze come from
`assets/picture/shaders/lighting.glsl` and its `frame_lighting` block, uploaded
once per frame; no shader has a light or haze uniform of its own, and the
clock moving changes uniforms only, never a mesh. Nothing glows at night
unless the palette gives it a glow floor, it is a lit lamp's face or a night
light reaches it. Light after dark comes from something visible holding it
(the ball, headlights, a floodlight), never a point in the air. A new light is
a candidate in `src/core/light_frame.h` with its numbers in `lighting.json`.
Terrain-dependent, time-independent light (sky visibility, horizons) is baked
with the area's render meshes, never per frame.

**The ball, cup and flag stay readable at every time of day.** Contrast tests
in `tests/content_tests.cpp` check them at every palette key and at night:
when one fails, fix the palette, not the check.

**Text is data.** Strings in `assets/interface/text/en.json`, styles in
`styles.json`, both through `src/game/text_ids.h`; sounds through
`src/audio/sound_ids.h`. Tests check every id exists. HUD text is the bitmap
font only (UTF-8, keyed by code point). The only text outside the table is the
`Ctrl` profiler overlay.

**Text always goes in a box.** Draw with `draw_label` / `layout_text`
(`src/renderer/pixel_font.h`) into a `ui_rect` cut from its panel, never at a
free point or a hand-picked size; the layout picks a whole-number scale, wraps
or shrinks, then ellipsizes. A test draws every screen at 4:3, 16:9 and 21:9
and fails on any cut-off label: make the box bigger or the string shorter.

**The camcorder, tape and TV passes are never optional.** Scene (low-res HDR)
→ camcorder (`camcorder.frag`: iris, white balance, tone curve, bloom, smear,
gain noise; the rangefinder's eyepiece is part of the scene) → tape
(`tape.frag`) → overlay (HUD, menus, datestamp, never bloomed or noisy) → TV
(`crt.frag`, window resolution, last; nearest vertically, soft horizontally).
The camcorder doesn't repeat what the tape owns. Paused, the scene and
camcorder hold their last frame while the tape plays on. The tape wear setting
blends the whole tape look between `picture.json`'s fresh and worn. The line count is art direction (`target_lines`), never a setting;
each scene line is a whole number of window rows. Tape effects are sized as
fractions of the picture height. Nothing flashes the whole screen during play
(never while aiming or during a shot; lightning only while walking). The
renderer's clock never reaches gameplay. For every rendering decision, ask
whether it looks more like a consumer camcorder tape from 1989 or less.

**Drawing stays cheap.** Every effect runs at scene resolution or smaller;
only the TV pass runs at window resolution. Static geometry is uploaded when
`terrain_render_revision` changes, which only a course load does: a hole
starting or ending never rebuilds the area or its meshes. Every pass has its
own `gpu_profile_stage` (`docs/performance.md`). Pick shader paths with
separate programs or `#define`s, not a branch on a uniform. Lighting, sky,
camcorder, shadows and night lights together cost at most 1.0 ms of GPU at
1080p on the owner's machine (release, `GOLFPP_VSYNC=0`,
`marienlyst_golfklub`).

**Layering.** `physics` depends on nothing; `game` on physics; `core` and the
GL-free renderer pieces on game. Game code reads `game_input` (intents), never
keys. No game logic in the renderer, no rendering in game code. Only the
`golfpp` executable touches SDL and GL.

**Offline always works.** Offline never needs a server, an account or Steam,
and every release ships its server for owners to run ("Stop Killing Games": no
Maincloud-only features, no Steam DRM, no forced relaunch through Steam).
Offline and online progress never mix: nothing imports, exports or merges
between them. Progress is keyed by an account, which can have several logins;
an anonymous login is a guest (`is_guest_login`): deleted on disconnect, and
it cannot link logins.

**Offline and online part in one place.** Every progress change goes through
`src/game/mode_dispatch.h`; both modes run the same `progress_rules`. Read
progress through `active_progress`, never `state.save`. Game code and menus
never include `src/net/`: game code pushes `net_commands` and reads `online`,
menus return `online_request`s; `app`'s `online_session` carries them out.
Motion is sparse and rate-limited (`src/game/motion_sync.h`). The client plays
ahead and the server's answer wins (`src/game/online_play.h`); a hole is over
when the server says so. Others are presentation only. Subscriptions are only
the player's own rows or their room's, never a whole table (SpacetimeDB bills
the traffic). `src/net/stdb_bridge.h` and `net/client_bridge/src/ffi.rs`
declare the same C structs: change both together.

**Saves.** `src/game/save_data.h` is the offline save; any change to its shape
bumps `current_save_version` (in the alpha an older save may be refused).
Parsing ignores unknown fields and refuses newer versions. Save on hole and
course completion, leaving a round and clean exit; never mid-hole or transient
state. An unreadable save is backed up, never overwritten. Settings live in
`settings.json` next to the save, never in it. Online progress is never
written to disk; only the login's refresh token is kept, in the system's
credential store (Windows Credential Manager, the macOS Keychain, the Secret
Service on Linux).

## Writing code that documents itself

Bad patterns replicate: assume whatever you write will be copied.

- Each header's first comment says what the file is for. Comments say what and
  why, briefly, only what the code can't. Never history ("used to", "no
  longer", "the old …"), in code, tests or tooling: delete it when found.
- One home per job: search before writing a helper; never copy a function.
- Delete dead code, unused fields and unused JSON keys. Planned work lives in
  `docs/`, not in unreachable code.
- Name things for what they are now: no `_ptr`, `legacy_`, `old_`, `new_`.
- A struct default never copies a tuning value: defaults live in the loader or
  the JSON.
- No exceptions: return `std::optional` or a result struct; `std::error_code`
  overloads of `std::filesystem`.
- `snake_case`. Structs for plain data, `class` with private members. `const`
  aggressively. No raw owning pointers, no `using namespace std` in headers.
  Include what you use.
- A `.cpp` past ~300 lines probably has two jobs: split it.
  `renderer.cpp`, `game_state.cpp` and `app.cpp` are at their limit.

## Content and licensing

`assets/` holds all content by what it is for: `world/` (courses, a folder
each with `course.json`, `world.json`, `holes/` and backdrops; `props/`;
`ground/`), `play/` (tuning, clubs, progression, `online.json`), `picture/`
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

## Planned work

`docs/plan.md` is the plan being built now, phase by phase; each phase says
what to update here. `docs/launch_todo.md` is the commercial launch checklist;
**Steam login (its section 7) is blocked until the owner confirms a Steam app
id: add no Steam code.** When a launch todo item is implemented, update this
file, delete it from the todo (renumbering sections and references) and add
any rules it brings.

## End of every session

- Search `AGENTS.md`, `README.md` and `docs/launch_todo.md` for todos, open
  decisions and "planned" wording this session settled; remove them and
  describe the result where it belongs (code comments first, this file only
  for rules).
- Check every path in this file exists.
- `docs/ideas.md` is the owner's scratchpad: never clean it up.
