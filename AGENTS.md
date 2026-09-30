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
| `src/core/` | Entry point, main loop, window, input, startup options |
| `src/game/` | Mutable game state, content loaders, save data, skills |
| `src/physics/` | Pure-functional ball flight, collision, terrain, wind |
| `src/renderer/` | OpenGL renderer, CRT pipeline, batched overlay/markers |
| `src/audio/` | SDL_mixer wrapper + data-driven sound manifest |
| `src/profiling/` | Per-frame profiler behind the `Ctrl` overlay |
| `src/platform/windows/` | Windows icon resource |
| `assets/` | Game data: holes, courses, course worlds, clubs, shaders, audio, icons |
| `tests/` | Unit tests; `tests/fixtures/` holds small hand-made holes/courses for tests only |
| `tooling/hole_editor/` | Browser-based hole + course-world editor |
| `tooling/osm_import/` | OpenStreetMap → hole/course/world JSON converter (Python) |
| `tooling/gb.ps1` | Windows release build helper (`.\tooling\gb -r`) |
| `docs/` | `ideas.md` (the owner's personal scratchpad, do not restructure it), `performance.md` (profiling guide) |
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
main loop (poll events → update → render), the startup menu flow, save
persistence and render-data assembly. `app.cpp` is large (~1100 lines) and is
the first candidate for splitting (menu flow out of the main loop) before new
screens are added.

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
`overlay_batch` + `pixel_font` (2D HUD/menus/text), `world_marker_batch`,
`cart_batch`, `course_map_fill`, `frustum`, `render_mesh_chunks`,
`primitive_mesh`. Their GL counterparts are `overlay_pass`,
`world_marker_renderer`, `tree_renderer` (instanced trees) and
`dynamic_buffer` (grow-only streaming VBO). `renderer.cpp` (~2200 lines) still
does scene orchestration, HUD layout and menus. Split it before adding
large new visuals.

Shaders live in `assets/shaders/`, GLSL `330 core`, loaded from disk at startup.
All in-game text uses the bitmap `pixel_font`. Never use a smooth TTF font for HUD.

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
| `audio/`, `shaders/`, `icons/` | Sound manifest + files, GLSL, window icon |

Terrain collision is derived at runtime from the hole spline; no baked data.
Holes and course worlds are produced by `tooling/osm_import/` and cleaned up in
`tooling/hole_editor/`. The tooling also writes `walking_shortcuts`,
`spawn_zones` and `interactables` into course worlds; **the game does not read
these yet**. They are placeholders for NPC/interaction work.

`make_initial_game_state()` boots the first course alphabetically (or one with
id `dev_course` if present) as the backdrop behind the main menu.

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

---

## Direction

Golf simulation first, RPG progression second, multiplayer later.

- Treat golf physics as the stable foundation; build RPG systems around it.
- Prefer reusable systems: `skill_id -> xp`, not one-off counters like `smoking_xp`.
  Don't add bespoke fields like `smoke_emote`/`beer_emote` for every new activity
  unless it is purely temporary visual state.
- When unlocks return, put them behind one progression/unlock API instead of
  scattering checks across UI, game update and menus. Item effects (e.g. the
  cigarette stat modifiers in `game_state.cpp`) should move to data at that point.
- NPCs, signs and pickups: data-driven placement in course worlds plus one
  interaction system for everything nearby.
- Course selection stays menu-based; in-course play trends toward roamable hubs.
  No seamless open world until hubs are proven.
- Multiplayer order: hot-seat → ghost/replay → async score/challenge sharing →
  lightweight backend (cloud saves, scores, ghosts, cosmetics) → real-time later.
  Define a session boundary first; never share mutable `game_state` over a network.
  No authoritative OSRS-style backend unless a feature truly needs it.
- Keep the game client-first and fully playable offline.

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
- Never make the CRT pass optional
- No smooth TTF fonts in the HUD
- No new libraries without flagging first

## Aesthetic

It should look like it was recorded on a consumer VHS camcorder in 1989 and
played back on a small TV: pixel-perfect upscaling, chunky geometry, slightly
wrong colours from chromatic aberration, scanlines. The lo-fi look is
intentional and committed. For every rendering decision, ask whether it makes
the game look more like that or less.
