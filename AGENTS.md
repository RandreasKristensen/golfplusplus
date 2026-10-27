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
is one ground, `build_ground` (`src/physics/ground_mesh.h`), and one surface
over it (`sample_area`): the ground, or a walkable solid's top such as a tee
box's (`src/game/tee_box.h`). Courses should be right out of the importer;
the hole editor is for touch-ups.

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
| Sound in the world: where each is heard from, how far it carries | `src/audio/sound_placement.h`, `src/game/world_sounds.h`; each sound's `reference_distance` and `max_distance` (or `placed: false`) in `assets/sound/sounds.json` |
| Keys → game actions | `src/core/key_bindings.cpp` |
| Menus (new screens go here) | `src/core/startup_flow.h`, `src/core/online_menus.h`; drawn by `src/renderer/menu_overlay.h` |
| What the renderer gets each frame, camera rigs | `src/core/render_frame.h`, `src/renderer/render_data.h` |
| Per-frame gameplay | `src/game/game_state.h` |
| Standing over my ball: the setup's dials and limits, my hand, the target and down views and V, what the wheel turns, the swing | `src/game/shot_setup.h`, `src/game/address_play.h`, `src/game/address_view.h`, `src/game/setup_pointer.h`, `src/game/swing.h`; the tuning's `setup`, `swing` and the camera's `address_*` |
| The OSD at address: the shot preview (flight, landing bracket, rest tick, a putt's start line), the setup word, the swing clock, where my bag is | `preview_shot` in `src/game/address_play.h`, `src/renderer/osd_overlay.h`; the tuning's `shot_preview` |
| A golfer's ability and a shot's dice: shape weights, the setup word, spreads, the natural shot, seeds | `src/game/golfer.h`, `src/game/shot_ability.h`, `src/game/shot_dice.h`; `assets/play/tuning/golfer.json`; the natural shot is a golfer setting (`assets/interface/settings.json`) and the picker in `src/core/startup_flow.h` |
| My bag and the club in hand: B, F at the bag, the picker | `src/game/golf_bag.h`, `src/game/bag_place.h`; the bag item in `assets/play/items/bag/`, its model; drawn by `src/renderer/bag_batch.h` |
| What moves that a shot meets (people, carts, balls at rest), the shooter's grace, the 2-stroke ball penalty | `src/game/shot_snapshot.h`, `simulate_struck_shot` in `src/game/shot_simulation.h`; the tuning's `solids` |
| Courses, holes, the area played on; my ball's life on them (teed, played, holed, given up) | `src/game/course_session.h`, `src/game/play_area.h`, `src/game/course_loader.h`; content in `assets/world/courses/` |
| Greens, bunkers, water and fairways: a zone's outline (inside, metres from its edge, its triangles), bunkers and ponds carved into the ground, a pond's level | `src/physics/material_zone.h`, `carved_height` in `src/physics/terrain.h`, `build_ground` in `src/physics/ground_mesh.h`; the tuning's `terrain` (`bunker_lip_meters`, `water_bank_meters`, `water_shore_meters`) |
| What F acts on in reach (my ball, a tee, a collectible, my bag, a seat) | `src/game/interactables.h` |
| A shot's flight and playback | `src/game/shot_simulation.h`, `play_shot` in `src/game/game_state.h`; `src/physics/` |
| How a ball leaves the club and flies: launch, lift and drag, the air | `src/physics/launch.h`, `src/physics/flight_model.h` (`air_at`), `src/physics/ball_physics.h`; the ball's fit in `assets/play/tuning/ball.json` (`flight`, `air`), each course's `air` in its `course.json`; the reference in `tests/fixtures/reference/` |
| The clubs and what a swing does to the ball: delivery, impact, the D-plane | the club items in `assets/play/items/club/` (their `spec`; `src/game/club_definition.h`), `src/game/delivery.h`, `src/physics/impact.h`; `club_strike` in `src/game/shot_simulation.h`; speeds fitted with `tooling/shot_lab/` (`--fit`) to `tests/fixtures/reference/bag_targets.json` |
| Lies: the stance's slope, grass, water and sand between face and ball, tee or ground | `lie_delivery` and `lie_contact_for` in `src/game/delivery.h`, `stance_at` in `src/game/shot_simulation.h`, the sand in `src/physics/impact.h`; the tuning's `lies`; `tests/fixtures/reference/lies.json` |
| The ball on the ground: bounce, roll, stimp; cups and flags | `src/physics/turf_contact.h`, `src/physics/cup.h`; `assets/play/tuning/surfaces.json`, the tuning's `cup` (`cup_leniency`), each course's `green_stimp_feet`; `flagstick_solids` in `src/game/area_solids.h`, P in `flag_in_reach` (`src/game/game_state.h`) |
| Deterministic sine, cosine, atan2, exp, log and pow | `src/physics/det_math.h` |
| Offline vs online: progress, net commands, motion | `src/game/mode_dispatch.h`, `src/game/net_types.h`, `src/game/motion_sync.h` |
| The connection, my room's rows, rejoining | `src/net/net_client.h` over `src/net/stdb_bridge.h` (Rust in `net/client_bridge/`), `src/net/room_rows.h`, `src/core/online_session.h`; `assets/play/online.json` |
| An online round; other players as shown; a room's shot replayed (`room_shot_result`) | `src/game/online_play.h`; `src/game/remote_players.h`, `src/core/render_online.h` |
| The server: tables, views, reducers, its rules | `server/golfpp_module/src/lib.cpp`, `account_rules.h`, `play_rules.h`, `server_content.h` |
| Models drawn in the world: kinds, `base` variants, mounts | their files in `assets/world/props/` (`src/game/prop_loader.h`, `src/game/model_kinds.h`), `src/renderer/prop_pieces.h` |
| Items: what a player uses and what it does | their files in `assets/play/items/<kind>/` (`src/game/item_catalog.h`); `src/game/effects.h` (`effective_value`); `src/game/loadout.h`, `assets/play/items/default_loadout.json`; `record_item_use` in `src/game/mode_dispatch.h`, `use_item` in the server |
| How every model is lit and drawn | `src/renderer/prop_batch.h`, `src/renderer/prop_renderer.h`, `assets/picture/shaders/prop.vert` / `prop.frag` (`src/renderer/prop_light.h` on the CPU); what is held `src/renderer/held_props.h`, the club at a ball `src/renderer/swing_club.h` |
| The golf cart | `src/game/golf_cart.h`, `src/game/cart_seats.h` (shared with the server), `src/renderer/cart_batch.h` |
| How a cart drives: motor, slopes, grip, tipping, sinking | `src/physics/cart_dynamics.h` (fixed steps); the cart item's `spec` in `assets/play/items/cart/`; the surfaces' `cart` numbers in `assets/play/tuning/surfaces.json` (`cart_ground_at` in `src/game/play_area.h`); `overturn_cart`, `right_cart` and `tow_cart` in the server; `tests/fixtures/reference/carts.json` |
| Hole signs, tee boxes, floodlights, fences | `src/game/hole_sign.h`, `src/game/tee_box.h`, `src/game/floodlights.h` |
| Anything solid: what the ball, players and carts meet | `src/physics/solid.h`, `src/game/area_solids.h` (`play_area::solids`), `src/game/body_contact.h`, `src/physics/body_collision.h`; restitution and friction by kind in the tuning's `solids` |
| Up and down: the one surface, steps, hops and falls | `sample_area` in `src/game/play_area.h` (the ground or a walkable top), `settle_vertical` in `src/physics/body_collision.h`, `src/game/player_jump.h`; `step_up` in the player's and the cart's tuning |
| Course map (M), rangefinder (Left Shift), god menu (F1, with every solid's outline for checking an import) | `src/renderer/course_map_overlay.h`; `src/game/rangefinder.h`, `src/game/sight_line.h`; `src/core/god_menu.h`, `src/renderer/solid_outline.h` |
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
| How OSM shapes become zones: simplified outlines, joined bunkers, generated fairways (with their sources) | `tooling/osm_import/osm_outline.py`; `zones` and `generated_fairway` in `osm_golf_convert.py`'s config |
| Shots natively vs in WASM | `tooling/net/check_determinism.ps1` |
| A running server checked by a headless client | `tooling/net/online_check/` |
| What real golf measures, with sources; what each club does | `tests/fixtures/reference/` (the oracle tests check against); `tooling/shot_lab/` |
| What players download | `tooling/release/build_release.ps1`, the `install` rules in `CMakeLists.txt` |

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
-Wpedantic -Wshadow` and stays warning-free. Tests use `vendor/doctest.h`
(`TEST_CASE`, `CHECK`, `REQUIRE`). Gameplay tests run on `tests/fixtures/`, not
`assets/`; shipped content is tested in `tests/content_tests.cpp`. Golden shots
(`tests/fixtures/golden_shots/`) run on frozen copies of the tuning, items (the
clubs among them) and rewards: a new tuning field goes in that copy too. Environment variables
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
changes. Online needs Rust (`net/client_bridge`, target
`x86_64-pc-windows-gnu`); the test preset builds with `-DGOLFPP_NET=OFF` and
links `src/net/` against `tests/fake_stdb_bridge.h`. `--server <uri> --db
<name> --anonymous` point the game at a server.

The version is `project(VERSION)` in `CMakeLists.txt`. Players' builds set
`GOLFPP_NO_CONSOLE` (errors only in SDL's log): debug from the presets.

**Flag any new dependency first.** Approved: the SpacetimeDB CLI, C++ bindings
and `spacetimedb-sdk` (pinned to the CLI's version), the Emscripten SDK, the
crates in `net/client_bridge/Cargo.toml`, SpacetimeAuth, Inno Setup 6 and
Python for releases. The Steamworks SDK is approved but **blocked** until the
owner has a Steam app id.

---

## Hard rules

**Physics (`src/physics/`) is pure.** Inputs by value or const reference, a new
value out: no mutated parameters, globals, `static` locals, I/O or profiling.
`simulate_shot` keeps the same rules and runs a whole shot in fixed steps;
`game_state` only plays the result back. Nothing that affects results depends
on frame `dt`. Everything builds with `-ffp-contract=off`, so native and WASM
round the same. The shot path (and everything a shot's result depends on: the
area's build, aim, launch, flight, the weather) uses `src/physics/det_math.h`,
never the C library's trigonometry, exp, log or pow, whose last bits differ
between platforms; `tooling/net/check_determinism.ps1` proves it.

**One list of solids.** Anything solid is a solid (`src/physics/solid.h`) in
`play_area::solids`, built once with the area on its grid, or a dynamic solid
built from game state (carts, people); its flags say what it stops. Nothing
keeps a collision list of its own: the ball, walking, driving and the
rangefinder's sight line all read the same list. Nothing has a height rule
of its own either: the surface is the ground or a walkable solid's top
(`sample_area`), and people and carts step up onto it within their
`step_up`, fall off what is higher and land.

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

**A shot is decided by its setup, the golfer's skills and dice.** No timing,
no meter, no aim arc: the player never sees a club's distance, only the
preview (the shot as set up, in still air). Every random thing about a shot
comes from its seed (`src/game/shot_dice.h`); the spreads are the golfer's
(`src/game/shot_ability.h`), designed as if every skill is 99. Reference
fixtures are the oracle; the only departures from real are `cup_leniency` and
the greens' flattened relief.

**Game and server stay in step.** A game plays online only on a server with
its own `protocol_version` (`src/game/net_types.h`), reported in the one-row
`server_protocol` view, which never changes. Every change to the module's
tables, views or reducers bumps it and regenerates the bindings. Release the
module first, then the installer from the same commit (`server/README.md`).

**Content is data.** No gameplay numbers, rewards, item stats, sound choices or
text in C++: tuning in `assets/play/tuning/`, rewards and skills in
`assets/play/progression/`, items (what they do, how they are used, their
sounds) in `assets/play/items/`, a model's colours, shape, points and mounts in
its prop file, scene colours and light in `assets/picture/lighting.json`. A
new cigarette, beer, cart or trinket is files, never C++. Loaders never invent
fallbacks: missing content fails loudly at startup.

**Items change numbers only through `effective_value`.** Gameplay reads every
number an item can change (`src/game/effects.h`: XP, walking speed, the cart's
speed, …) through `effective_value` on the shared clock, offline and on the
server alike, never an item's raw numbers. A final value is `(base + Σ flat) ×
(1 + Σ increased) × Π (1 + more)`. Looks (a model) and rules (an item) are
separate files; only rules are compiled into the server.

**The weather is shared, replayable and in play.** A course's weather is a pure
function of the course and UTC time (`weather_at`), the same for everyone: no
row, no save field. A shot flies in the weather at `shot_input::shot_at`; the
server checks that time against its clock and works the weather out itself.
The weather code is plain arithmetic (no sine, cosine or powers) so WASM
matches. Its temperature, with the course's elevation, sets how thin the air
a shot flies through is. The god menu's weather is offline only.

**Time of day is visual only, but for the air's temperature.** It never
changes scoring or rewards, and physics only through the air's temperature,
which follows the day (`temperature_at` in `src/game/weather.h`, by
`game_day_at`, plain arithmetic, so the server replays it): a cool morning
drive flies a little shorter.
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
with the area's render meshes, never per frame. Every model (a cart, a club,
what is held, mine pinned to the camera too) is drawn by the one lit prop path
(`src/renderer/prop_renderer.h`, `prop.frag`): lit where it is in the world by
the sun, the shadow map and the night lights on the side facing them; a new
model file needs no shader or drawing code.

**The ball, cup and flag stay readable at every time of day.** Contrast tests
in `tests/content_tests.cpp` check them at every palette key and at night:
when one fails, fix the palette, not the check.

**Text is data.** Strings in `assets/interface/text/en.json`, styles in
`styles.json`, both through `src/game/text_ids.h`; sounds through
`src/audio/sound_ids.h`. Tests check every id exists. HUD text is the bitmap
font only (UTF-8, keyed by code point). The only text outside the table is the
`Ctrl` profiler overlay.

**Sound comes from where it happens.** Every sound in the world (a strike, a
landing, a cart, a bump, a use, mine and others') is pushed with its
position (`src/game/world_sounds.h`) and heard from the camera as drawn,
quieter with distance by inverse distance and panned by direction
(`src/audio/sound_placement.h`), carrying as far as its manifest entry says.
Menus, the HUD, thunder and the ambience say `placed: false`.

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
(never at address or during a shot; lightning only while walking). The
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

**One world.** Nothing changes mode: a course is one place, and what changes
is the state of my ball (`game_state::ball`: in my pocket, teed, at rest, in
flight, in its cup). Anything in reach can be used at any time, through the one
interaction list. Teeing up, holing out and giving up change only the ball, its
strokes and the shot, never the player's seat, an item in use or its effects,
the area or its meshes. Online the ball row is the server's word on my ball.

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
ahead and the server's answer wins (`src/game/online_play.h`), but for a
shot: a shot is a setup plus a seed the server rolls and the snapshot of the
room it takes from its rows (`shot_record`), written into its event; the
client sends the setup at Space, holds the swing at its top for the answer and
launches the ball from it, bit for bit as the server played it. A hole is over
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
written to disk; only the login's refresh token is kept, in Windows Credential
Manager.

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

## Planned work

A plan being built, when there is one, is `docs/plan.md`, phase by phase;
each phase says what to update here, and it is deleted once built.
`docs/launch_todo.md` is the commercial launch checklist;
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
