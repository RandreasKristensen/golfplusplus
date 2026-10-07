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
(`build_ground` in `src/physics/ground_mesh.h`): hole geometry decides the
height on a hole's fairway, its rough eases into the land, and every height
and normal comes from that one grid, except on a tee box
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
| The sign at every tee (placed at runtime, posts stop shots) | `src/game/hole_sign.h`; its face `src/renderer/hole_sign_face.h`, drawn by `src/renderer/hole_sign_renderer.h` |
| The course map held up with Enter (fitted to the holes, numbered) | `src/renderer/course_map_overlay.h`; its ground `src/renderer/course_map_fill.h` (the one home of map inks) |
| The tee box at every tee (flat, placed at runtime, the surface on it) | `src/game/tee_box.h`; drawn by `src/renderer/world_marker_batch.h` |
| Fences in course worlds (shots stop at their nets) | `src/physics/fence_collision.h`; drawn by `src/renderer/fence_renderer.h` |
| Player settings (volumes, field of view): the screen, the file next to the save | `src/core/settings_menu.h`, `src/game/settings.h`; entries in `assets/ui/settings.json` |
| XP, collectibles, completion rules | `src/game/progress_rules.h` (pure functions over `save_data`) |
| Feel numbers | `assets/tuning/game_tuning.json` (struct: `src/game/game_tuning.h`) |
| Rewards and skills | `assets/progression/*.json` (`src/game/reward_rules.h`) |
| Ball flight, contact, terrain | `src/physics/` |
| HUD | `src/renderer/hud_overlay.cpp` (scorecards in `scorecard_overlay.cpp`); the GL side is `src/renderer/renderer.cpp` |
| Text size and layout | `src/renderer/pixel_font.h`, styles in `assets/ui/text_styles.json` |
| Content format | the loader next to it (`src/game/*_loader.cpp`), each with `parse_*_from_text` |
| The online server (SpacetimeDB module): tables, views, reducers | `server/golfpp_module/src/lib.cpp`; its rules next to it in `account_rules.h`, `play_rules.h`, `server_content.h` |
| Shots natively vs in the server's WASM | `tooling/net/check_determinism.ps1` |
| Tooling | `tooling/README.md` (OSM importer, hole editor, art generator) |
| Course backdrops, ground textures | `tooling/art/make_art.py`, drawn by `src/renderer/backdrop_pass.h` and `assets/shaders/terrain.frag` |

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
`server/README.md` (written for players who self-host). For local multiplayer,
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
self-hosted server. After changing the server's tables or reducers,
regenerate the bindings: `spacetime generate --lang rust --bin-path
server/golfpp_module/build/lib.wasm --out-dir net/client_bridge/src/module_bindings`.

The build uses `-Wall -Wextra -Wpedantic -Wshadow` and must stay warning-free.
Tests use `vendor/doctest.h`, a small runner with `TEST_CASE`, `CHECK` and
`REQUIRE` only. Gameplay tests run on `tests/fixtures/` (not `assets/`), so
content edits don't break them; tests of shipped content live in
`tests/content_tests.cpp`. Golden shots (`tests/fixtures/golden_shots/`) run
on frozen copies of the tuning, clubs and rewards, so feel edits don't move
them; a new tuning field must be added to that copy too. `GOLFPP_COURSE` and
`GOLFPP_VSYNC` are in `docs/performance.md`.

**Do not introduce new dependencies without flagging it first.** The server
uses the SpacetimeDB CLI, the Emscripten SDK and the SpacetimeDB C++ bindings
(fetched by `server/golfpp_module/CMakeLists.txt`, pinned to the CLI's
version). The client bridge uses Rust with the crates in
`net/client_bridge/Cargo.toml` (`spacetimedb-sdk` pinned to the server's
version). SpacetimeAuth is approved, configured in its dashboard. The
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
`server_errors.h`, which the client shows from its string table. The caller's
token is read with `connection_jwt_payload` in `lib.cpp`, never
`AuthCtx::get_jwt`, which never returns one in these bindings. A mid-hole
disconnect abandons the hole: scores exist only for completed holes.

**Content is data.** No gameplay numbers, rewards, item stats, sound choices or
text in C++. Feel numbers go in `game_tuning.json`, rewards in `rewards.json`,
skills in `skills.json`, club sounds in the club files. Loaders don't invent
fallback content: missing content fails loudly at startup.

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

**The CRT pass is never optional.** Scene → low-res FBO → nearest-neighbour
upscale → `assets/shaders/crt.frag` (scanlines and vignette). Never make it
toggleable or skip it. For every rendering decision, ask whether it looks more
like a consumer camcorder tape from 1989 or less.

**Layering.** `physics` depends on nothing; `game` on physics; the GL-free
renderer pieces and `core` on game. Game code reads `game_input` (intents),
never keys. No game logic in the renderer, no rendering in game code. The
libraries in `CMakeLists.txt` are SDL- and GL-free so tests link them; only the
`golfpp` executable touches SDL and GL.

**Offline always works.** Offline must never need a server, an account or
Steam (the "Stop Killing Games" idea: the game keeps working if the official
server shuts down, and the server stays self-hostable, no Maincloud-only
features). No Steam DRM, no forced relaunch through Steam. Offline and online
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
  what and why; never history ("used to", "the old …", "after the refactor").
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
  in new player-facing text, store material or file names. The existing ones
  are due to be renamed.
- Course data is OpenStreetMap (ODbL): never remove or hide OSM attribution.
- Heights are from the AWS Terrain Tiles (Terrarium) and their sources (SRTM,
  USGS NED, national DEMs): credit them alongside OSM.
- Record the source and licence of every audio, image or icon asset; only use
  assets that allow commercial use.
- Flag any AI-generated art, audio, text or trailer content to the owner (Steam
  requires disclosure). AI-written code needs no disclosure.

## Planned work

`docs/multiplayer_plan.md` is the owner-approved plan. Implement one phase per
run, in order, leaving the build warning-free and all tests passing.
`docs/steam_todo.md` is the store and release checklist. **Steam login (its
section 5) is blocked until the owner confirms a Steam app id: don't start it or
add any Steam code.**

| Phase | What |
|---|---|
| 1 | Alpha 0.1, the first release, to friends: installer, Maincloud, client and server versions in step. **Only when the owner says so**: the owner playtests and bug-tests first, and may add features before it |

When anything from the plan or the Steam todo is implemented, update this file
in the same change, delete it from the plan or todo (renumbering the remaining
phases and sub-sections, and every cross-reference to them), and add any rules
it brings.

## End of every session

- Search `AGENTS.md`, `README.md`, `docs/multiplayer_plan.md` and
  `docs/steam_todo.md` for todos, open decisions ("TBD", "decide whether",
  "owner decides"), "planned" wording and unchecked items that this session
  implemented or decided; remove them and make sure the result is described
  where it belongs (code comments first, this file only for rules).
- Check this file still matches the code: every path in it exists.
- `docs/ideas.md` is the owner's scratchpad: never clean it up or remove entries.
