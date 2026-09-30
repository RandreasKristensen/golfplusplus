# AGENTS.md — golf++ project context

Read this before touching anything. It describes the code as it is, the rules
that are not negotiable, and where the project is heading.

For ANY directory specific command, always write the full directory for where to
run the command. The repo root is `C:\Users\arand\Desktop\AU\sjov\golfplusplus`.

---

## What this is

A lo-fi 3D golf game in C++17. It renders to a low-resolution framebuffer and
upscales through a CRT post-process so it looks like a 1989 camcorder tape.
Courses are real golf courses imported from OpenStreetMap. Each course is a
roamable hub: you walk or drive a cart between hole starts, pick up
collectibles, and level RuneScape-style skills (golf swing, fitness, cart
driving, drifting, smoking).

**Current state:** everything is unlocked. All clubs are in the bag, and the
rangefinder, cart and smoking are always available. There is no money, shop or
quest system. They were removed on purpose until the RPG layer is redesigned.

---

## Repo layout

| Path | What |
|---|---|
| `src/core/` | Entry point, main loop, startup menu flow, window, input, startup options |
| `src/game/` | Mutable game state, content loaders (incl. text assets), save data, skills |
| `src/physics/` | Pure-functional ball flight, collision, terrain, wind |
| `src/renderer/` | OpenGL renderer, CRT pipeline, batched overlay/markers |
| `src/audio/` | SDL_mixer wrapper + data-driven sound manifest |
| `src/profiling/` | Per-frame profiler behind the `Ctrl` overlay |
| `src/platform/windows/` | Windows icon resource |
| `assets/` | Game data: holes, courses, course worlds, clubs, text (font, strings, styles), shaders, audio, icons |
| `tests/` | Unit tests; `tests/fixtures/` holds small hand-made holes/courses for tests only |
| `tooling/hole_editor/` | Browser-based hole + course-world editor |
| `tooling/osm_import/` | OpenStreetMap → hole/course/world JSON converter (Python) |
| `tooling/gb.ps1` | Windows release build helper (`.\tooling\gb -r`) |
| `docs/` | `ideas.md` (the owner's personal scratchpad, do not restructure it), `performance.md` (profiling guide), `multiplayer_plan.md` (the phased online plan, see "Planned work"), `steam_todo.md` (store page and release checklist) |
| `vendor/` | `nlohmann/json.hpp`, `doctest.h` |

---

## Tech stack

| Concern | Library |
|---|---|
| Window + input | SDL2 |
| Audio | SDL2_mixer |
| OpenGL | GL 3.3 core; function pointers loaded via SDL in `src/core/gl_loader.*` |
| Math | GLM (`vendor/glm/` or system install) |
| JSON | nlohmann/json (`vendor/nlohmann/json.hpp`) |
| Tests | `vendor/doctest.h`, a small doctest-compatible reimplementation (no `REQUIRE`, no `-tc` filtering) |
| Build | CMake 3.25+ with Ninja presets |

**Do not introduce new dependencies without flagging it first.** Explain the
tradeoff and let the owner decide.

Already approved for the multiplayer plan, but not added yet (add each only in the
phase that needs it): SpacetimeDB CLI, Emscripten SDK, a Rust client bridge
(`spacetimedb-sdk`, OIDC/PKCE, `open`, `keyring`), SpacetimeAuth. The **Steamworks
SDK is approved but blocked** until the owner has a Steam app id (plan Phase 6).

---

## Build and test

```bash
cmake --preset debug   && cmake --build build/debug   # ./build/debug/golf++
cmake --preset release && cmake --build build/release # ./build/release/golf++
cmake --preset test    && cmake --build build/test && ./build/test/golf++-tests
```

The test binary also builds the game. The full suite takes about 2 minutes
(terrain index tests and full-size OSM holes dominate). The Python tooling tests
run with `python -m unittest test_osm_golf_convert` from `tooling/osm_import/`.

Profiling switches (`GOLFPP_COURSE`, `GOLFPP_VSYNC`) and the overlay are
documented in `docs/performance.md`.

---

## Modules

### core (`src/core/`)

`main.cpp` is tiny: it reads startup options and runs `app`. `app.cpp` owns the
main loop (poll events → update → render), save persistence, audio, loading the
text assets and render-data assembly (~800 lines).

`startup_flow.*` owns the menu flow: main menu, help, hole/course pickers and the
in-round "are you sure" menu. It updates a `startup_flow_state` from input,
returns actions (`start_course`, `quit`) and UI sounds for `app` to carry out,
and builds the menu render data from the string table. It has no SDL: `app`
turns mouse clicks into overlay clip space first. New screens (login, name
entry) go here, not into `app.cpp`.

`event_loop` fills `input_state`, including `text_typed` from `SDL_TEXTINPUT`.
SDL text input is off by default; call `set_text_input_enabled(true)` only while
a text field is focused, so typing never triggers game keys.

### game (`src/game/`)

Mutable core. `game_state` is a plain struct owned by `app` and passed by
reference. **No globals or singletons.**

| File | Responsibility |
|---|---|
| `game_state.*` | Update loop: walking, cart, aiming, swing, ball step, hub/hole transitions, emotes, XP drops |
| `game_tuning.*` | Gameplay feel constants and the loaded course/terrain for the active hole |
| `swing.*` | Timing-based swing state machine |
| `round_state.*`, `scorecard.*` | Strokes per hole, round progress, scorecard rows |
| `progression.*` | Generic `skill_id -> xp` model, level curve (1–99) |
| `save_data.*` | Persisted save struct, JSON (de)serialization, migration chain |
| `save_manager.*` | Save slot + local profile metadata on disk |
| `hole_loader.*`, `course_loader.*`, `course_world_loader.*`, `club_loader.*` | JSON content loaders |
| `game_content.*` | Loads clubs + courses for the menu |
| `text_assets.*` | Loads the font, string table and text styles (below) from `assets/` into one `text_assets` value |
| `pixel_font_data.*`, `string_table.*`, `text_style.*` | Parsers (from a string) and lookups: `find_glyph`, `font_charset`, `lookup_text`, `format_text`, `find_text_style` |
| `text_ids.h` | Every string key (`text_*`) and style name (`style_*`) code uses; a test checks they all exist in the JSON |
| `asset_resolver.*` | Finds the `assets/` folder at runtime |

### physics (`src/physics/`) — strict rules

**`src/physics/` is a pure functional zone. These are hard constraints.**

Every function in `src/physics/` must:
- take all inputs by value or `const` reference
- return a new value, never mutate a parameter
- read no global state (no `extern`, no `static`, no singletons)
- do no I/O (no logging, no file reads)
- use no `static` local variables

```cpp
// CORRECT
ball_state step(const ball_state in, const wind_state wind, const float dt);

// WRONG — mutates, reads globals, does I/O
void step(ball_state& state, const float dt);
```

Pure functions are trivially testable and deterministic: the same inputs always
give the same trajectory, so any bug reproduces exactly. Wind is a pure function
of `(seed, time)`; each hole's `wind_seed` gives it a stable wind character.

Files: `ball_physics` (top-level `step`), `flight_model` (drag, Magnus,
gravity), `collision` (terrain bounce/roll), `tree_collision`, `terrain` (spline
→ mesh, spatial index, height/normal sampling), `wind`.

### renderer (`src/renderer/`)

OpenGL 3.3 core. SDL2 owns the window and context.

**The CRT pipeline is not optional:**
1. Render the scene to a low-res FBO (sized from a 640×360 reference in `renderer.cpp`)
2. Upscale with **nearest-neighbor** filtering
3. Apply `crt.frag`: scanlines, chromatic aberration, vignette, bloom bleed

Never make the CRT pass toggleable or skip it during development.

Several pieces are deliberately GL-free so they can be unit tested:
`overlay_batch` + `pixel_font` (2D HUD/menus/text), `menu_overlay` (startup
menus and help screen, plus the tile layout the menu flow hit-tests against),
`control_icons` (key icons shared by the HUD and help screen), `text_input`
(single-line text field), `world_marker_batch`, `cart_batch`,
`course_map_fill`, `frustum`, `render_mesh_chunks`, `primitive_mesh`. Their GL
counterparts are `overlay_pass`, `world_marker_renderer`, `tree_renderer`
(instanced trees) and `dynamic_buffer` (grow-only streaming VBO).
`renderer.cpp` (~1800 lines) still does scene orchestration and HUD layout.
Split it before adding large new visuals.

Shaders live in `assets/shaders/`, GLSL `330 core`, loaded from disk at startup.

**Text.** All on-screen text is data. Strings come from the string table
(`assets/text/en.json`, flat `"key": "TEXT"` with `{name}` placeholders filled by
`format_text`), are drawn with the bitmap font (`assets/fonts/pixel_font.json`)
and placed with named styles (`assets/ui/text_styles.json`: pixel size, fit
minimum, colour, alignment). `renderer::render` takes the loaded `text_assets`
by const reference. Draw with `draw_text` / `draw_text_fitted` (anchor is the
top-left for left-aligned styles, the centre for centred ones); layout-driven
sizes and state colours use `with_pixel_size` / `with_color` / `with_align` on a
looked-up style. Numbers that are data (scores, XP) are formatted in code but
placed through styles. A missing key shows as `[key]`, a missing glyph as a
filled box with a `?` cut out, and a missing style in magenta. Names that come
from content (courses, holes, clubs) stay in their own JSON; skill names are
`skill.<skill_id>` keys. The only text not in the table is the `Ctrl` profiler
overlay, which is developer diagnostics from `profiling`. Lowercase is drawn
with the uppercase glyphs. Never use a smooth TTF font for HUD.

### audio / profiling

`audio_manifest` parses `assets/audio/sounds.json`; `audio_engine` plays it.
Game code pushes `audio_event`s into `game_state`; `app` drains them.
`profiling` is opt-in per frame (`frame_profile*` may be null) and never global.

---

## Content data (`assets/`)

All content is JSON. **Do not hardcode content in C++.**

| Folder | Contents |
|---|---|
| `holes/` | One file per hole: `id`, `name`, `par`, `wind_seed`, `tee`, `pin`, `spline {control_points, width, rough_width}`, `material_zones` (green/bunker/water), `trees` |
| `courses/` | Course manifest: `id`, `name`, `hole_count`, `holes` (paths or ids), optional `world` |
| `course_worlds/` | Hub data in shared course coordinates: `spawn`, `hole_starts`, `cart_roads`, `collectibles` (skill XP + world flag rewards, optional skill/flag requirements) |
| `clubs/` | Club stats and bag order |
| `fonts/` | `pixel_font.json`: glyph bitmaps (`"A": ["0110", ...]`, 7 rows), glyph height, fallback glyph |
| `text/` | `en.json`: every on-screen string, keyed as in `src/game/text_ids.h` |
| `ui/` | `text_styles.json`: named text styles |
| `audio/`, `shaders/`, `icons/` | Sound manifest + files, GLSL, window icon |

Terrain collision is derived at runtime from the hole spline; no baked data.
Holes and course worlds are produced by `tooling/osm_import/` and cleaned up in
`tooling/hole_editor/`. The tooling also writes `walking_shortcuts`,
`spawn_zones` and `interactables` into course worlds; **the game does not read
these yet**. They are placeholders for NPC/interaction work.

`make_initial_game_state()` boots the first course alphabetically (or one with
id `dev_course` if present) as the backdrop behind the main menu.

Content will also be compiled into the server module, which can't read files. Keep
every loader able to parse from a string, not only from a path.

**Licensing and naming** (see `docs/steam_todo.md`):
- Don't put trademarked course names (e.g. Augusta National, St Andrews / Old Course)
  in new player-facing text, store material or file names. The existing ones are due to be renamed.
- Course data comes from OpenStreetMap (ODbL), which requires attribution. Don't remove
  or hide OSM attribution once it exists.
- When adding any audio, image or icon asset, record its source and licence. Only use
  assets that allow commercial use.
- Flag any AI-generated art, audio, text or trailer content to the owner (Steam requires
  disclosure). AI-written code needs no disclosure.

---

## Save data

Persisted in `save_data` (`src/game/save_data.h`), currently **version 5**:
completed courses, current course/hole, hole scores, skills, collected ids,
repeatable collectible state, world flags.

- Every change to the persisted shape bumps `current_save_version` and adds a
  step to `migrate_save_data`. Parsing ignores unknown fields, so removed fields
  in old saves are harmless.
- Save on hole completion, course completion and clean exit. Never autosave mid-hole.
- Never save transient state: ball flight, emotes, input, renderer or audio state.
- This is, and stays, the **offline** save. Online progress (planned) lives only on the
  server and is never written into this file, and offline progress never moves online.
  Nothing imports, exports or merges between the two.

---

## Direction

Golf simulation first, RPG progression second. Online play is planned to become
the main draw, with offline kept as a complete solo mode.

- Treat golf physics as the stable foundation; build RPG systems around it.
- Prefer reusable systems: `skill_id -> xp`, not one-off counters like `smoking_xp`.
  Don't add bespoke fields like `smoke_emote`/`beer_emote` for every new activity
  unless it is purely temporary visual state.
- When unlocks return, put them behind one progression/unlock API instead of
  scattering checks across UI, game update and menus. Item effects (e.g. the
  cigarette stat modifiers in `game_state.cpp`) should move to data at that point.
  The plan makes this API `progress_rules`: pure functions over the progress struct,
  shared by offline play and the server. Write new progress, reward and unlock logic
  so it can move there, not inline in `game_state` update code.
- NPCs, signs and pickups: data-driven placement in course worlds plus one
  interaction system for everything nearby.
- Course selection stays menu-based; in-course play trends toward roamable hubs.
  No seamless open world until hubs are proven.
- Multiplayer (decided, see `docs/multiplayer_plan.md`): rooms of up to 40 players
  per course, groups of up to 4, hosted on SpacetimeDB.
  - The server is authoritative for shots, scores, XP and unlocks.
  - Movement is sent as sparse intents and predicted on clients.
  - Shots are replayed on the server with the same C++ physics.
  - Never share mutable `game_state` over a network.
- **Fully playable offline, always.** Offline must never need a server, an account or
  Steam. This follows the "Stop Killing Games" idea: the game must keep working if
  the official server shuts down, and the server must stay self-hostable (no
  Maincloud-only features).
- Accounts are not logins: progress will be keyed by an account id, and one account
  can have several logins (browser, later Steam) linked to it.
- Steam will be the primary online login (plan Phase 6, blocked on the app id), but
  never required: no Steam DRM, and the game starts without Steam running.

---

## Planned work

`docs/multiplayer_plan.md` is the owner-approved plan, in phases. Implement one phase
per run, in order, and leave the repo building with tests passing after each.
**Phase 6 (Steam login) is blocked until the owner confirms a Steam app id.** Don't
start it, or add any Steam code, before then.

| Phase | What |
|---|---|
| 1 | Deterministic fixed-step `simulate_shot` with playback, content parsing from text, rewards to data, `progress_rules`, network seam in `game_state` |
| 2 | SpacetimeDB server module (C++ to WASM): accounts and login linking, rooms, groups, shots, progress |
| 3 | Rust client bridge and browser login (SpacetimeAuth) |
| 4 | Online and offline menus, login and name entry, remote players and shots, groups |
| 5 | Self-hosting guide, dev tooling, docs; update this file with the new modules and rules |
| 6 | Steam login and linking Steam to existing accounts (blocked) |

Until a phase lands, this file describes the code as it is. **Whenever anything from
`docs/multiplayer_plan.md` or `docs/steam_todo.md` is implemented, update this file
in the same change** so it describes the result, and remove it from the plan or todo
(and from this section). No "planned", todo or decision wording may remain here or
there for things that already exist. The plan's "Rule changes to write into
AGENTS.md" lists rules to add as their phases land.

Rules that already apply, so new work doesn't have to be redone:
- **Known problem:** ball flight is currently frame-rate dependent (`step_ball` uses
  frame `dt`). Don't add more simulation that depends on frame `dt`; new gameplay
  simulation that affects results must use a fixed step.
- Don't grow `app.cpp`, `renderer.cpp` or `game_state.cpp` with new screens. Menu
  flow goes in `src/core/startup_flow.*`, menu drawing in `src/renderer/menu_overlay.*`.

---

## Coding conventions

- C++17, `snake_case` for everything (files, functions, variables, types)
- Structs for plain data; classes only for enforced invariants
- `const` aggressively
- No raw owning pointers; use `std::unique_ptr` or values
- No exceptions; return `std::optional` or a result struct
- No `using namespace std` in headers
- Include what you use
- If a `.cpp` grows past ~300 lines it probably has two concerns. `app.cpp`,
  `game_state.cpp` and `renderer.cpp` already do; split before growing them

## What not to do

- No singletons or global mutable state
- No game logic in the renderer; no rendering calls in game state or physics
- No mutation or global reads inside `src/physics/`
- No content (text, rewards, item stats) hardcoded in C++
- No string literals or hand-picked text sizes/colours in rendering or menu code: add a
  key to `assets/text/en.json` and a style to `assets/ui/text_styles.json`, with the
  ids in `src/game/text_ids.h`
- Never make the CRT pass optional
- No smooth TTF fonts in the HUD
- No new libraries without flagging first
- Never make offline play depend on a server, an account or Steam
- Never mix offline and online progress
- No Steam DRM and no forced relaunch through Steam

## End of every session

Before finishing any session, check that the documentation matches the code:

- Search `AGENTS.md`, `docs/multiplayer_plan.md`, `docs/steam_todo.md` and `README.md`
  for todos, open decisions ("to be decided", "TBD", "decide whether", "owner decides"),
  "planned" wording and unchecked items. Remove every one that this session
  implemented or decided, and make sure the result is described in `AGENTS.md`.
- When a phase of `docs/multiplayer_plan.md` is implemented and tested, **delete
  it** from the plan (don't just mark it done) and **renumber** the remaining phases
  and their sub-sections from 1. Update every cross-reference to the new numbers
  in the plan, `AGENTS.md` (including the "Planned work" table) and `docs/steam_todo.md`,
  e.g. "Phase 7", "see 3.9", "(5.5)".
- `docs/ideas.md` is the owner's scratchpad: don't clean it up or remove entries from it.

## Aesthetic

It should look like it was recorded on a consumer VHS camcorder in 1989 and
played back on a small TV: pixel-perfect upscaling, chunky geometry, slightly
wrong colours from chromatic aberration, scanlines. The lo-fi look is
intentional and committed. For every rendering decision, ask whether it makes
the game look more like that or less.
