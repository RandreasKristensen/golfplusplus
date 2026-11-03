# AGENTS.md - golf++

Shared project instructions for Claude and Codex. The code is the documentation;
this file holds durable intent, constraints and directions to task-specific rules.

## Working agreement

- Work directly on `main` in the existing working tree. Never create, switch or
  merge branches; never `git add`, commit, push or stash. The owner handles source
  control. Read-only git commands are fine.
- For every directory-specific command, use an explicit absolute working directory.
  Repo root: `C:\Users\arand\Desktop\AU\sjov\golfplusplus`.
- One agent edits at a time in this shared tree. Other agents may review a specified
  diff or investigate a bounded question without editing. Do not start another
  writer while one is active; this also applies to generated files and builds.
- Inspect existing changes before editing. Preserve the owner's and other agents'
  uncommitted work; never revert changes merely because you did not make them.
- End tasks with what changed, checks run and remaining concerns. Reviews report
  concrete findings with file references. Neither model has a permanent role.
- Record durable decisions in shared code or docs, never only in private agent
  memory. `docs/ideas.md` is the owner's scratchpad: never clean it up.
- Flag new dependencies before introducing them; approved dependencies are listed
  in [development procedures](docs/development.md). Steam integration is blocked
  until the owner confirms a Steam app id: add no Steam code.

## Intent

C++17 golf simulation first, RPG progression second. The picture is a lo-fi 1989
consumer camcorder tape played on a small TV. Real OSM courses are continuous
places; the practice ground is scripted in `tooling/practice_ground/`. Imports
should be playable directly; the hole editor is for touch-ups.

Online is the main mode: up to 40 players per course, groups of up to 4, each with
their own ball and pace. Offline remains complete solo play. Everything is unlocked
today: no money, shop, quests or unlock gating.

Real golf decides unspecified rules, equipment and physics. Look up uncertain
facts rather than guessing. Departures require an explicit owner design decision,
a name in code or data, documentation where they live, and tests.

Early alpha: change saves, protocol, server and content together without shims,
migrations or staged rollouts. Still bump save and protocol versions as required.

## Read for the task

Before editing, read the applicable documents below, including when changing a
caller or data that affects that system. These are project rules, not optional
background. Read only what the task needs; do not load every document by default.
Use [the code map](docs/code_map.md) to find entry points, then read the code.
Every header's first comment says what it is for; read it before changing it.

| Task touches | Read first |
|---|---|
| Simulation, shots, abilities, items, collision, coordinates, weather or ball lifecycle | [Gameplay rules](docs/gameplay_rules.md) |
| Server, client bridge, online state, progress, accounts or saves | [Networking rules](docs/networking_rules.md) |
| Rendering, shaders, models, lighting, camera, UI, text layout or sound | [Rendering rules](docs/rendering_rules.md) |
| Content, loaders, courses, importer, art, text, sound assets, dependencies or distribution | [Content rules](docs/content_rules.md) |
| Tests, builds, imports, bindings, determinism, dependencies or releases | [Development procedures](docs/development.md) |

## Architecture invariants

- Physics is pure: const inputs, a value out; no mutated parameters, globals,
  static locals, I/O or profiling. Shots run in fixed steps and results never
  depend on frame `dt`. The entire shot path uses `src/physics/det_math.h` for
  trig, exp, log and pow; preserve native/WASM determinism and `-ffp-contract=off`.
- One collision list: `play_area::solids`, plus dynamic solids from state. One
  ground (`build_ground`), one surface query (`sample_area`, ground or walkable
  solid top). No separate collision lists or height rules.
- No globals or singletons. `app` owns and passes state. Exceptions: the GL function
  table and server `module_cache` for parsed content and built courses only.
- `physics` depends on nothing; `game` on physics; core and GL-free renderer pieces
  on game. Game reads intents, never keys. Only the executable touches SDL/GL.
  No game logic in renderer, rendering in game, or `src/net/` includes in game/menus.
- Server authority uses the game's simulation and progress rules. Progress changes
  go through `mode_dispatch`; read through `active_progress`. Offline and online
  progress never mix. Offline needs no server, account or Steam; ship the server.
- Replicated things have one truth: their server row. Confirm seats, acknowledge
  predictions, restore whole rows on refusal, and adopt complete room snapshots
  only (see networking rules).
- Change module tables/views/reducers with a `protocol_version` bump and regenerated
  bindings. Change both sides of the bridge's C structs together. Save shape changes
  bump `current_save_version`; unreadable saves are backed up, never overwritten.
- Gameplay numbers, rewards, item stats, sounds and text are data. Loaders fail
  loudly on missing content. Item-modifiable numbers go through `effective_value`.
- Teeing up, holing out and giving up change only ball, strokes and shot; preserve
  player seat, items/effects, area and meshes. Use the one interaction list.
- Preserve the one lit prop path and scene -> camcorder -> tape -> overlay -> TV pipeline.
  Renderer time never drives gameplay; static meshes rebuild only on course load.
- Preserve attribution and licences. No trademarked course names in player-facing
  text, store material or filenames. Art is drawn by code: no external images or
  AI image tools. Flag AI-generated art, audio, player-facing text or trailers.

## Code and verification

- Search before writing helpers. Reuse `vector_math.h`, `json_util.h`,
  `content_files.h`, `test_support.h` and `camera_local.h` (see the code map).
- Comments explain what and why, briefly, only what code cannot. Do not retain
  historical commentary. Delete dead code, unused fields and unused JSON keys
  affected by the task; plans belong in docs, not unreachable code.
- Use names for what things are now: no `_ptr`, `legacy_`, `old_`, `new_`.
  `snake_case`, aggressive `const`, structs for data, classes with private members.
  Include what you use; no raw owning pointers or `using namespace std` in headers.
- No exceptions: use `std::optional` or result structs and filesystem error codes.
  Struct defaults never duplicate tuning. A `.cpp` beyond roughly 300 lines likely
  has two jobs; `renderer.cpp`, `game_state.cpp` and `app.cpp` are at their limit.
- From the repo root, run separately and stop on failure: `cmake --preset test`,
  `cmake --build build/test`, then `./build/test/golf++-tests` (Windows: `.exe`).
  An argument such as `"cart"` selects tests by name. Keep builds warning-free.
- Run checks appropriate to the change; details and specialized commands are in
  development procedures. Gameplay tests use fixtures; shipped content has its own
  tests. Update frozen golden-shot content when adding tuning fields.
- Visual placement tests project through the camera and assert screen position and
  orientation; a wrong sign must fail. Imports must verify with zero errors before
  hole touch-ups: fix the importer, not generated output.

## Keep instructions small

Target 100-150 lines here. Do not add feature summaries, completed work, file
inventories or session notes. Add a rule only for a durable owner decision or a
recurring mistake that code, tests or tooling cannot adequately prevent. Prefer
replacing or shortening an existing rule. Put implementation details beside code,
file entry points in the code map, and specialized rules/procedures in task docs.
Do not automatically import those documents into startup instructions.

Update only docs affected by the task. Remove resolved todos and planned wording
there; record outcomes where they belong rather than expanding this file. Check
links and paths you add or move. Active plans (what is built next) belong in `docs/todo/`;
remove a plan when built. Long-term plans stay out of the repo, in the owner's notes. `docs/todo/launch_todo.md` is the commercial launch checklist. Keep shared rules
in this file and the linked docs; `CLAUDE.md` only imports `AGENTS.md`.
