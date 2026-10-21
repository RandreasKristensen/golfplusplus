# AGENTS.md — golf++

The code is the documentation. This file only holds what the code cannot tell
you: intent, hard rules, where to start reading, and how to check your work.
Every header's first comment says what the file is for; read the header of
anything you touch before changing it.

For ANY directory-specific command, write the full directory. The repo root is
`C:\Users\arand\Desktop\AU\sjov\golfplusplus`.

**Source control: work directly on `main`, in the working tree.** Never create,
switch or merge branches, and never `git add`, commit, push or stash. The owner
works alone, tests before pushing, and does all source control. Read-only git
commands (`status`, `diff`, `log`) are fine.

---

## What this is

A lo-fi 3D golf game in C++17 that looks like a 1989 camcorder tape played on a
small TV. Courses are real golf courses imported from OpenStreetMap. Each course
is a hub: walk or drive a cart between hole starts, pick up collectibles, level
RuneScape-style skills. A course is one continuous area on its real land
(the course world's `ground` grid): holes are played where they sit, never in
a separate scene, so other groups stay visible and a stray shot lands on the
next hole or the ground between. There is exactly one surface
(`build_ground` in `src/physics/ground_mesh.h`): the land decides the height
everywhere (its relief scaled, greens kept flatter, by
`src/physics/land_relief.h`), holes included, and a hole only lifts it by its own heights (0 for
an imported hole, so hole-editor touch-ups are lifts), finer on fairways and
greens so the land's detail is where the ball rolls; every height and normal
comes from that one grid, except on a tee box
(`src/game/tee_box.h`): a flat top over the highest ground it covers, which
`sample_area` returns there. Courses should be right out of the
importer; the hole editor is for touch-ups.

Golf simulation first, RPG progression second. Online play is the main mode:
rooms of up to 40 players per course on SpacetimeDB, everyone visible on the
course, groups of up to 4 sharing a scorecard, each player with their own ball
at their own pace. Offline stays a complete solo mode.
Everything is unlocked today: no money, shop, quests or unlock gating.

## Where to start reading

| To change… | Start at |
|---|---|
| The frame loop, saving, audio dispatch | `src/core/app.cpp` |
| Keys → game actions | `src/core/key_bindings.cpp` |
| Menus (new screens go here) | `src/core/startup_flow.h` (the online screens in `src/core/online_menus.h`), drawing in `src/renderer/menu_overlay.h` |
| What the renderer is told each frame, camera rigs | `src/core/render_frame.h`, `src/renderer/render_data.h` |
| Per-frame gameplay | `src/game/game_state.h` |
| A shot's flight (deterministic, replayable), its playback | `src/game/shot_simulation.h`, `play_shot` in `src/game/game_state.h` |
| Offline vs online: progress changes, net commands, motion updates | `src/game/mode_dispatch.h`, `src/game/net_types.h`, `src/game/motion_sync.h` |
| The online connection: sign-in, subscriptions, sending and receiving | `src/net/net_client.h` (C++), over `src/net/stdb_bridge.h`, the C API of the Rust bridge in `net/client_bridge/`; where to connect is `assets/online.json` |
| Between the menus and the connection; rejoining a room after a reconnect | `src/core/online_session.h` |
| My room's rows (players, balls, groups, shots), the server clock | `src/net/room_rows.h` |
| An online round: the server's answers to my shots and actions | `src/game/online_play.h` |
| Other players and their shots, as shown | `src/game/remote_players.h`; drawn by `src/renderer/remote_avatar_batch.h`, `src/renderer/online_overlay.h`, `src/core/render_online.h` |
| Starting courses and holes, the hub | `src/game/course_session.h`, `src/game/play_area.h` |
| The golf cart (parked beside hole 1, stepped into and out of, driven; online, anyone can ride in another's passenger seat) | `src/game/golf_cart.h`, its seats and parking spots `src/game/cart_seats.h` (shared with the server); the server's `golf_cart` table and `board_cart` / `leave_cart` in `server/golfpp_module/src/lib.cpp`; drawn by `src/renderer/cart_batch.h` |
| The sign at every tee (placed at runtime, posts stop shots) | `src/game/hole_sign.h`; its face `src/renderer/hole_sign_face.h`, drawn by `src/renderer/hole_sign_renderer.h` |
| The course map held up with M (fitted to the holes, numbered) | `src/renderer/course_map_overlay.h`; its ground `src/renderer/course_map_fill.h` (the one home of map inks) |
| The tee box at every tee (flat, placed at runtime, the surface on it) | `src/game/tee_box.h`; drawn by `src/renderer/world_marker_batch.h` and `assets/shaders/tee_box.frag` (the shadow map falls on it) |
| What players and carts bump into (trunks, poles, sign boards, nets, flagsticks, carts and people on foot; tee boxes for carts only), the bump and crash sounds | `src/game/area_obstacles.h` (built with the area), `src/game/body_contact.h` (walking and driving), over `src/physics/body_collision.h` |
| Fences in course worlds (shots stop at their nets) | `src/physics/fence_collision.h`; drawn by `src/renderer/fence_renderer.h` |
| The floodlight poles along every hole (placed at runtime, alternating down the fairway, at each tee and green; poles stop shots like trunks) | `src/game/floodlights.h`; their light after dark in `src/core/light_frame.h`; drawn (and into the shadow map) by `src/renderer/floodlight_renderer.h` from `src/renderer/floodlight_batch.h` and `assets/shaders/floodlight.vert` |
| Player settings (volumes, field of view, fullscreen): the screen, the file next to the save | `src/core/settings_menu.h`, `src/game/settings.h`; entries in `assets/ui/settings.json` |
| XP, collectibles, completion rules | `src/game/progress_rules.h` (pure functions over `save_data`) |
| Feel numbers | `assets/tuning/game_tuning.json` (struct: `src/game/game_tuning.h`) |
| Rewards and skills | `assets/progression/*.json` (`src/game/reward_rules.h`) |
| Ball flight, contact, terrain | `src/physics/` |
| HUD | `src/renderer/hud_overlay.cpp` (scorecards in `scorecard_overlay.cpp`); the GL side is `src/renderer/renderer.cpp` |
| The picture's look: line count, camcorder, tape and TV | `assets/visuals/picture.json` (`src/game/picture_loader.h`), sized by `src/renderer/picture_size.h`; the camcorder is `src/renderer/camcorder_pass.h` and `assets/shaders/camcorder*.frag` (exposure, white, gain and saturation by time of day are keys of `assets/visuals/lighting.json`); the tape is `src/renderer/tape_pass.h` and `assets/shaders/tape.frag` (tracking noise from `src/core/tape_tracking.h`), the TV `src/renderer/tv_pass.h` and `assets/shaders/crt.frag` |
| The camcorder's datestamp and the VCR's PLAY / PAUSE tag and still frame | `src/core/vcr_frame.h` (the `datestamp` setting), drawn by `src/renderer/vcr_overlay.h` |
| Text size and layout | `src/renderer/pixel_font.h`, styles in `assets/ui/text_styles.json` |
| Content format | the loader next to it (`src/game/*_loader.cpp`), each with `parse_*_from_text` |
| The online server (SpacetimeDB module): tables, views, reducers | `server/golfpp_module/src/lib.cpp`; its rules next to it in `account_rules.h`, `play_rules.h`, `server_content.h` |
| Shots natively vs in the server's WASM | `tooling/net/check_determinism.ps1` |
| Tooling | `tooling/README.md` (OSM importer, hole editor, art generator) |
| The weather: wind, cloud, rain, thunder, fog (never in a shot) and the ground's wetness as numbers that push each other (days-long settled and unsettled regimes; clouds run ahead of the storm, rain trails it, so a clearing sky makes a sun shower; `coupled_weather` keeps any set of them possible), the kinds read off them, and what they do to a shot | `src/game/weather.h` (the `wind` and `weather` sections of the tuning), the wind by height `src/physics/wind.h`, in a shot `step_shot` in `src/game/shot_simulation.h`; offline or online `course_weather_at` in `src/game/mode_dispatch.h` |
| The weather as drawn: overcast, rain and fog light, haze and wet ground, the sky's cloud deck, lightning and its thunder (`src/core/lightning_frame.h`, the sounds from `tooling/audio/make_thunder.py`), the rainbow, rain streaks, the wind's specks and every flag flying, faded in and out | `src/core/weather_frame.h` (presentation only, kept by `app`), the light under it `src/game/weather_lighting.h`; drawn by `src/renderer/weather_renderer.h` and `assets/shaders/weather_streak.vert`, the sky's part in `assets/shaders/backdrop.frag`, the ground's in `terrain.frag`, flags in `src/renderer/world_marker_batch.h`; its numbers in the `weather`, `rain` and `wind` sections (the rainbow in `sky`) of `assets/visuals/lighting.json` |
| The rangefinder (Left Shift: the eyepiece, its zoom, distance and height to whatever it points at or any flag it locks onto, spoiled by rain and fog, the wind) | `src/game/rangefinder.h` (the `rangefinder` section of the tuning), what its laser meets `src/game/sight_line.h`; drawn by `src/renderer/rangefinder_overlay.h` and `src/renderer/eyepiece_pass.h` (`assets/shaders/eyepiece.frag`) |
| The god menu (F1 with `GOLFPP_GOD` or `--god`: time of day, a weather kind, then its cloud, rain, fog and thunder, wind, wetness, offline) | `src/core/god_menu.h`, drawn by `src/renderer/god_menu_overlay.h` |
| The time of day: the sky's clock, the sun and moon over a course | `src/game/sky_clock.h` (its timeline is the `sky_clock` section of the tuning; each course's fixed date, clock and lowest sun are the `sky` of its course file); each frame's sky in `src/core/sky_frame.h` |
| Scene colours and light: what each thing is coloured, the light, sky and haze by sun elevation, glow floors | `assets/visuals/lighting.json` (`src/game/lighting_loader.h`, its fields and ranges in `src/game/lighting_fields.h`), evaluated at a sun elevation by `src/game/sun_lighting.h`; the shaders' light and haze in `assets/shaders/lighting.glsl`, uploaded by `src/renderer/lighting_uniforms.h` (the haze from `src/renderer/scene_haze.h`); the ground's baked sky visibility and horizons in `src/renderer/sky_visibility.h` |
| Shadows and night lights: the blob under the ball (cast along the sun) and at the feet of players, carts and trees, the shadow map of trees, floodlights, players and carts, the land's shadow at low sun, the lights after dark (ball, headlights, floodlights) and the glow floors and lamp faces they raise | `src/core/light_frame.h` (what casts and what lights, from the frame and the game's ground); the shadow map is `src/renderer/shadow_map_pass.h` (players and carts in it from `src/renderer/shadow_casters.h`, fitted by `src/renderer/shadow_map_fit.h`, sampled in `assets/shaders/terrain.frag`); the land's shadow is `sun_shadow` in `assets/shaders/lighting.glsl` over the horizons `src/renderer/sky_visibility.h` bakes; the nearest lights picked by `src/renderer/scene_lights.h`; blobs drawn by `src/renderer/blob_shadow_renderer.h` and `assets/shaders/blob_shadow.vert`; their numbers in the `shadows` and `night_lights` sections of `assets/visuals/lighting.json` |
| The ground's static meshes on the GPU (terrain, drawn zone shapes) | `src/renderer/terrain_renderer.h` |
| The sky: gradient, horizon glow, clouds lit by the low sun, sun, moon, stars (one home, with the course's panoramas) | `src/renderer/backdrop_pass.h` and `assets/shaders/backdrop.frag`; its numbers in the `sky` section of `assets/visuals/lighting.json` |
| Course backdrops, ground textures | `tooling/art/make_art.py`, drawn by `src/renderer/backdrop_pass.h` and `assets/shaders/terrain.frag` |
| What players download: the installer, the zip, the server package, `THIRD_PARTY.txt` | `tooling/release/build_release.ps1` (`tooling/README.md`); what it stages is the `install` rules in `CMakeLists.txt` |

Reuse these instead of writing your own: `src/physics/vector_math.h` (yaw,
horizontal distance, safe normalize, `clamp01`; use `glm::radians` and
`glm::pi`, never a local `pi`), `src/game/json_util.h` (every JSON read),
`src/game/content_files.h` (every file read and directory scan: loaders parse
text, so the server can parse the same content without a filesystem),
`tests/test_support.h` (fixtures, `started_hole()`, `started_game()`,
`near()`).

## Build and test

```bash
cmake --preset test && cmake --build build/test && ./build/test/golf++-tests
./build/test/golf++-tests "cart"        # only tests whose name contains "cart"
cmake --preset release && cmake --build build/release   # ./build/release/golf++
tooling\release\build_release.ps1                       # the installer and zip for players, in build/dist
python -m unittest test_osm_golf_convert                # in tooling/osm_import/
py -3 verify_osm_import.py --all                        # in tooling/osm_import/: every course against testdata/scorecards.json
```

The `test` preset is optimized (`-O2 -g`, asserts on): the whole suite runs
in well under a minute, and optimized code is what ships. Step through code
in the `debug` preset. A test that takes over a second is usually a slow
game function (building a course, say), not a slow test: fix the function.

An import must verify with 0 errors before holes are touched up in the
editor: fix the importer, not its output. Where OSM lacks information, the
import's audit (`osm_audit.py`) says what to map; the owner maps it in OSM and
re-imports. Per-course overrides in `osm_golf_config.json` are a stop-gap for
what can't be mapped yet.

The server module builds with the Emscripten SDK and the `spacetime` CLI (from
PowerShell, after `. <emsdk>\emsdk_env.ps1`):

```powershell
spacetime build --module-path server/golfpp_module
spacetime start                                           # a local server, in another terminal
spacetime publish golfpp --server local --bin-path server/golfpp_module/build/lib.wasm
tooling\net\check_determinism.ps1 -Emsdk <emsdk>         # golden shots, native vs WASM
```

Running a server, configuring who may sign in and pointing the game at it is
`server/hosting.md`, the README of the server package every release ships
(written for owners who run their own server; they get the built module,
never its source). For local multiplayer,
`.\tooling\gb -m` does all of that (starting the server
only for the session, publishing only a changed module, anonymous logins on)
and launches two anonymous clients; `.\tooling\gb -x` stops them and the
server (`tooling/README.md`).

Its plain C++ (everything in `server/golfpp_module/src` but `lib.cpp` and
`module_cache`) is also in the native test build, so the server's rules are
tested with the game's. The module compiles content in, so republish it after
content changes.

Online play needs Rust: the default build runs cargo on `net/client_bridge`
(with MinGW, the `x86_64-pc-windows-gnu` target: `rustup target add
x86_64-pc-windows-gnu`) and links it into `golf++`. `-DGOLFPP_NET=OFF` (the
test preset) builds without it: offline only. Tests link `src/net/` against
a fake bridge (`tests/fake_stdb_bridge.h`); `cargo test` in
`net/client_bridge` tests the Rust side. `--server <uri> --db <name>
--anonymous` (or `GOLFPP_SERVER`, `GOLFPP_DB`) point the game at a local or
self-hosted server. After changing the server's tables or reducers, bump
`protocol_version` and regenerate the bindings: `spacetime generate --lang rust --bin-path
server/golfpp_module/build/lib.wasm --out-dir net/client_bridge/src/module_bindings`.

The build uses `-Wall -Wextra -Wpedantic -Wshadow` and must stay warning-free.
Tests use `vendor/doctest.h`, a small runner with `TEST_CASE`, `CHECK` and
`REQUIRE` only. Gameplay tests run on `tests/fixtures/` (not `assets/`), so
content edits don't break them; tests of shipped content live in
`tests/content_tests.cpp`. Golden shots (`tests/fixtures/golden_shots/`) run
on frozen copies of the tuning, clubs and rewards, so feel edits don't move
them; a new tuning field must be added to that copy too. `GOLFPP_COURSE`,
`GOLFPP_VSYNC`, `GOLFPP_WINDOW`, `GOLFPP_SCREENSHOT` (a BMP of the picture
at any window size, for reviewing visuals), `GOLFPP_GAME_TIME` (the time
of day pinned), `GOLFPP_WEATHER` (the weather held offline) and `GOLFPP_GOD`
(the god menu on F1, as `--god`) are in `docs/performance.md`.

The version is `project(VERSION)` in `CMakeLists.txt`; the main menu and the
installer show it. Players' builds (`build_release.ps1`) set
`GOLFPP_NO_CONSOLE`, so they open no console window and startup errors are
only in SDL's log: debug from the presets' builds, which keep the console.

**Do not introduce new dependencies without flagging it first.** The server
uses the SpacetimeDB CLI, the Emscripten SDK and the SpacetimeDB C++ bindings
(fetched by `server/golfpp_module/CMakeLists.txt`, pinned to the CLI's
version). The client bridge uses Rust with the crates in
`net/client_bridge/Cargo.toml` (`spacetimedb-sdk` pinned to the server's
version). SpacetimeAuth is approved, configured in its dashboard. Releases
are built with Inno Setup 6 and Python (`tooling/release`). The
Steamworks SDK is approved but **blocked** until the owner has a Steam app
id.

---

## Hard rules

**Physics (`src/physics/`) is pure.** Every function takes inputs by value or
const reference and returns a new value: no mutated parameters, no globals, no
`static` locals, no I/O, no profiling. Same inputs, same result, so the server
can replay shots. `simulate_shot` (`src/game/shot_simulation.h`) keeps the
same rules: it runs a whole shot in fixed steps, and `game_state` only plays the
result back. Gameplay simulation that affects results must not depend on frame
`dt`. Every library and the server module build with `-ffp-contract=off`, so
floating point is never fused differently natively and in WASM.

**No globals or singletons.** `app` owns everything and passes it by
reference. The exceptions are the GL function table in
`src/renderer/gl_loader.h` (OpenGL is global state) and, in the server
module, `server/golfpp_module/src/module_cache.h`: one memo of the parsed
embedded content and built courses, never game state.

**The server plays by the game's rules.** Online, the server decides shots,
strokes, completions and progress with the game's own code (`simulate_shot`,
`progress_rules`, `round_state`, the `play_area` helpers), so a gameplay rule
changed in `src/game` changes online play too. What only the server checks
(names, logins, link codes, speed, reach, shot validity) lives in the plain
`*_rules` files next to `lib.cpp`, which only reads and writes rows. Its
limits are the `server` section of the tuning. Reducers fail with the ids in
`src/game/server_errors.h`, which the client shows from its string table. The caller's
token is read with `connection_jwt_payload` in `lib.cpp`, never
`AuthCtx::get_jwt`, which never returns one in these bindings. A mid-hole
disconnect abandons the hole: scores exist only for completed holes.

**Game and server stay in step.** A game plays online only on a server with
its own `protocol_version` (`src/game/net_types.h`), which the module reports
in the one-row `server_protocol` view; any other server is told apart before
anything else is read, and the player is asked to update the game. Every
change to the module's tables, views or reducers bumps it (and regenerates
the bindings); the `server_protocol` view itself never changes. Release the
module first, then the installer built from the same commit
(`server/README.md`).

**Content is data.** No gameplay numbers, rewards, item stats, sound choices or
text in C++. Feel numbers go in `game_tuning.json`, rewards in `rewards.json`,
skills in `skills.json`, club sounds in the club files, scene colours and
their light in `assets/visuals/lighting.json`. Loaders don't invent fallback
content: missing content fails loudly at startup.

**The weather is shared, replayable and in play.** A course's weather is a
pure function of the course and UTC time on the sky's clock (`weather_at`
in `src/game/weather.h`), the same for everyone on it: no row, no save
field. A shot records when it was hit (`shot_input::shot_at`) and flies in
the course's weather at that moment for its whole flight
(`shot_course::weather`); the server checks `shot_at` against its own clock
(`server.shot_time_slack_seconds`) and works the weather out itself, as every
client replaying the shot does. The weather code is plain arithmetic (no
sine, cosine or powers) so the server's WASM gets the very same weather. The
god menu's weather (`game_state::weather_override`) is offline only.

**Time of day is visual only.** It never changes physics, scoring or rewards
(until a `progress_rules` change explicitly says otherwise). The time of day
is the tuning's `sky_clock` timeline at UTC now, the same for every player:
online "now" is the server clock estimate (`shared_unix_ms` in
`src/game/mode_dispatch.h`). Only the time runs: each course is always on
the date of its course file's `sky` (shown on its own clock, its sun
optionally never sinking below a floor), so the seasons never change. No
save field, no server row; changing the cycle needs a game update. A gameplay rule that ever depends on the time of day
uses `game_day_at` (plain arithmetic the server can replay), never the sun's
trigonometry. All scene light and haze come from `assets/shaders/lighting.glsl`
and its `frame_lighting` block, uploaded once per frame; no shader has a light
or haze uniform of its own (the shadow map is a texture the terrain and tee
boxes sample, drawn while the sun is up), and the clock moving changes uniforms
only, never a mesh. Everything drawn in the scene goes through it, lit or at least as
bright as the light there is, so nothing glows at night unless the palette
gives it a glow floor, it is a lit lamp's face (`lamp_glow`) or a night light
reaches it. Light after dark comes from something the player can see holding
it: the ball (it has a light in it), a cart's headlights, a floodlight's lamp;
never from a point in the air. Lighting that depends on
the terrain but not the time (the ground's sky visibility and horizons) is
baked when the area's render meshes are built, never per frame; the sun's
place against it is a uniform. The lights after dark are picked on the CPU
each frame (at most `max_scene_lights`, nearest the camera) and evaluated per
pixel in `lighting.glsl`; a new light source is a candidate in
`src/core/light_frame.h` with its numbers in `lighting.json`, never a shader
of its own.

**The ball, cup and flag stay readable at every time of day.** They keep a
glow floor from `lighting.json`; after dark the ball glows past white and the
green's floodlights lift the cup off its green. Tests in `tests/content_tests.cpp`
check ball-against-fairway and flag-against-sky contrast at every palette
key, and the ball's glow and the cup against its floodlit green at night:
when one fails, fix the palette, not the check.

**Text is data.** On-screen strings live in `assets/text/en.json`, styles in
`assets/ui/text_styles.json`, both referenced through `src/game/text_ids.h`;
sounds code plays are in `src/audio/sound_ids.h`. Tests check every id exists.
A missing string draws as `#key#`, a missing glyph as a box, a missing style in
magenta. HUD text is the bitmap font only, never a TTF. The only text outside
the table is the `Ctrl` profiler overlay (developer diagnostics). Strings are
UTF-8 and the font is keyed by code point (`src/game/utf8.h`).

**Text always goes in a box.** Overlay text is drawn with `draw_label` (or
`layout_text`) from `src/renderer/pixel_font.h` into a `ui_rect` cut from its
panel (`src/renderer/ui_rect.h`), never at a free-floating point or a
hand-picked size. The style says how much of the box it fills; the layout
picks a whole-number scale on the low-res target so font pixels stay square
and sharp, wraps or shrinks, and only then cuts off with an ellipsis. A test
in `tests/content_tests.cpp` draws every shipped screen at 4:3, 16:9 and
21:9 and fails if any label is cut off: when it fails, make the box bigger or
the string shorter, don't lower the check.

**The camcorder, tape and TV passes are never optional.** Scene (low-res HDR
FBO, where the sun and anything glowing may go past white) → camcorder pass
(`camcorder.frag`, scene resolution: iris, white balance, tone curve, bloom,
CCD smear, gain noise, into an 8-bit target; while the rangefinder is up,
the eyepiece pass (`src/renderer/eyepiece_pass.h`) first turns the scene
into what is seen through it, as part of the scene: its round view is the
scene drawn a second time at its zoom, scissored to the view, at scene
resolution) → tape pass (`tape.frag`, scene
resolution, its own target) → overlay (HUD, menus, datestamp) drawn onto the
tape target, so text is never bloomed, smeared or noisy → TV pass (`crt.frag`,
window resolution, last). The camcorder doesn't repeat what the tape owns
(colour bleed, the tape's colour loss and grain). While paused the scene and
camcorder hold their last frame and the tape plays on over it. The TV upscale is nearest vertically
and soft horizontally. The line count is art direction (`target_lines` in
`assets/visuals/picture.json`), never a setting: each scene line is a whole
number of window rows (`src/renderer/picture_size.h`), so wider windows get
more width. Tape effects are sized as fractions of the picture height, never
in pixels, so the look doesn't change with the window size; the tape wear
setting blends the whole tape look between picture.json's fresh (next to
no tape) and worn. Tracking noise, and anything else that flashes
the whole screen, stays out of play (never while aiming or during a shot;
lightning strikes only while walking);
no full-frame brightness pulses. The renderer's clock (`clock_seconds`) is for
looks only and never reaches gameplay. Never make a pass toggleable or skip
it. For every rendering decision, ask whether it looks more like a consumer
camcorder tape from 1989 or less.

**Drawing stays cheap.** Every effect runs at scene resolution or smaller;
only the TV pass runs at window resolution. Static geometry (terrain, trees)
is uploaded when its revision changes, never rebuilt per frame. Every pass
has its own `gpu_profile_stage`, shown by the `Ctrl` overlay
(`docs/performance.md`). Pick a shader path with separate programs or
`#define` variants, not a branch on a uniform (terrain.frag's flat draws are
the one left). The lighting, sky, camcorder, shadows and night lights
together cost at most 1.0 ms of GPU at 1080p on the owner's machine (release
build, `GOLFPP_VSYNC=0`, `marienlyst_golfklub`).

**Layering.** `physics` depends on nothing; `game` on physics; the GL-free
renderer pieces and `core` on game. Game code reads `game_input` (intents),
never keys. No game logic in the renderer, no rendering in game code. The
libraries in `CMakeLists.txt` are SDL- and GL-free so tests link them; only the
`golfpp` executable touches SDL and GL.

**Offline always works.** Offline must never need a server, an account or
Steam (the "Stop Killing Games" idea: the game keeps working if the official
server shuts down, and every release ships its server as a package owners can
run themselves, so no Maincloud-only features). No Steam DRM, no forced relaunch through Steam. Offline and online
progress never mix: the local save is offline only, and nothing imports,
exports or merges between the two. Accounts are not logins: progress is keyed
by an account id, and one account can have several logins. An anonymous login
is a guest (`is_guest_login` in `account_rules.h`): its account is deleted
when it disconnects, and it cannot link logins.

**Offline and online part in one place.** Every progress change goes through
`src/game/mode_dispatch.h`: offline applies `progress_rules` to the local save,
online pushes a `net_command` and the result comes back from the server into
`game_state::online`. Both modes run the same `progress_rules`, so rule changes
go there. Read progress through `active_progress`, never `state.save` directly.
Game code never talks to the network or includes `src/net/`: it pushes
`net_commands` and reads `online` (`src/game/net_types.h`), the way it pushes
`audio_events`; `app` owns the `net_client` (in its `online_session`) that
sends and fills them. The menus don't include `src/net/` either: they read an
`online_menu_status` and return `online_request`s, which `online_session`
carries out. Online
motion is sparse and rate-limited by `src/game/motion_sync.h` (the `net`
section of the tuning). Online the client plays ahead (a shot plays as soon as
it is hit) and the server's answer wins (`src/game/online_play.h`): a refusal
puts the player and ball back, the server's rest position is blended to, and a
hole is over when the server says so, never by local cup detection. Others in
the room are presentation only (`src/game/remote_players.h`). Subscriptions are only ever the player's own rows or
their room's (`net_client`), never a whole table: a full room fans every
update out to everyone in it, and SpacetimeDB bills that traffic.
`src/net/stdb_bridge.h` and `net/client_bridge/src/ffi.rs` declare the same
C structs; change both together (`net_client` refuses a bridge whose layout
differs). The bridge's sign-in failures are ids the game turns into text,
and the text it shows in the browser comes from the string table.

**Saves.** `src/game/save_data.h` is the offline save. Any change to its shape
bumps `current_save_version` and adds a migration step; parsing ignores
unknown fields and refuses newer versions. Save on hole completion, course
completion, leaving a round and clean exit; never mid-hole, never transient
state. An unreadable save is backed up, never overwritten silently. Player settings are not progress and apply online too: they live in
`settings.json` next to the save (`src/game/settings.h`), never in it. Online
progress is never written to disk: only the browser login's refresh token is
kept, in Windows Credential Manager (`net/client_bridge/src/login.rs`).

## Writing code that documents itself

Bad patterns replicate: assume whatever you write will be copied.

- Each header's first comment says what the file is for. Comments explain
  what and why, briefly, and only what the code can't say: nobody reads
  them but agents, so every line costs context. Never history ("used to",
  "previously", "the old …", "no longer", "after the refactor"), in code,
  tests or tooling; when you find one, delete it rather than rewording it.
- One home per job. Before writing a helper, search for it (see the shared
  helpers above). Never copy a function into a second file.
- Delete dead code, unused fields and unused JSON keys instead of leaving them
  for later. Planned work lives in `docs/`, not in unreachable code.
- Name things for what they are now. No `_ptr`, `legacy_`, `old_` or `new_`
  names; rename instead.
- A struct default is never a second copy of a tuning value: data defaults
  live in the loader (named constants) or in the JSON.
- No exceptions: return `std::optional` or a result struct. Use the
  `std::error_code` overloads of `std::filesystem`.
- `snake_case` everything. Structs for plain data; `class` when a type has
  private members. `const` aggressively. No raw owning pointers. No
  `using namespace std` in headers. Include what you use.
- A `.cpp` past ~300 lines probably has two jobs; split it before adding a
  third. `renderer.cpp`, `game_state.cpp` and `app.cpp` are already at their
  limit: new screens go in `startup_flow` / `menu_overlay`, new HUD in
  `hud_overlay`, new rules in `progress_rules`.

## Content and licensing

`assets/` holds every hole, course, course world, club, tuning, reward, string,
style, font, shader and sound. Holes and course worlds come from
`tooling/osm_import/` and are cleaned up in `tooling/hole_editor/`. Course
worlds also carry tooling-only data (`walking_shortcuts`, `spawn_zones`,
`interactables`, OSM refs) that the game does not read yet: placeholders for
NPC and interaction work, which should become one data-driven interaction
system.

- No trademarked course names (e.g. Augusta National, St Andrews / Old Course)
  in player-facing text, store material or file names.
- Course data is OpenStreetMap (ODbL): never remove or hide OSM attribution
  (the main menu's credits, `menu.main.credits`, and `THIRD_PARTY.txt`).
- Heights in Denmark are the Danish Elevation Model (Klimadatastyrelsen, CC BY
  4.0), elsewhere the AWS Terrain Tiles (Terrarium) and their sources (SRTM,
  USGS NED, national DEMs): credit them alongside OSM. The Dataforsyningen
  token that fetches the Danish model is personal: it lives only in the
  gitignored `tooling/osm_import/dataforsyning_token.txt`.
- All art is drawn by code (shaders, `tooling/art/make_art.py`, the pixel
  font's data): no image files from elsewhere, no AI image tools. The icon in
  `assets/icons/` was drawn by code that wasn't kept; it is redrawn by a kept
  script when it is redesigned. Every music and sound
  file in `assets/audio/` is a placeholder until original music and sound
  design replace it (the thunder is drawn by `tooling/audio/make_thunder.py`).
  Record the source and licence of any asset not made by code; only use
  assets that allow commercial use.
- Every library the game ships or compiles in has its licence in
  `THIRD_PARTY.txt` (`tooling/release/third_party.py`): the Rust crates are
  found by themselves; a new native library goes in its `NATIVE_PACKAGES`
  and in `third_party_header.txt`.
- Flag any AI-generated art, audio, text or trailer content to the owner (Steam
  requires disclosure). AI-written code needs no disclosure, and neither does
  what that code draws.

## Planned work

`docs/launch_todo.md` is the commercial launch plan and checklist: the free
trial, membership for the official servers, our own site and Steam. **Steam
login (its section 7) is blocked until the owner confirms a Steam app id: don't
start it or add any Steam code.**

When anything from the launch todo is implemented, update this file in the same
change, delete it from the todo (renumbering the remaining sections, and every
cross-reference to them), and add any rules it brings.

## End of every session

- Search `AGENTS.md`, `README.md` and `docs/launch_todo.md` for todos, open
  decisions ("TBD", "decide whether", "owner decides"), "planned" wording and
  unchecked items that this session implemented or decided; remove them and
  make sure the result is described where it belongs (code comments first,
  this file only for rules).
- Check this file still matches the code: every path in it exists.
- `docs/ideas.md` is the owner's scratchpad: never clean it up or remove entries.
