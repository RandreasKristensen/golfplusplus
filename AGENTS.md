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
and normal comes from that one grid. Courses should be right out of the
importer; the hole editor is for touch-ups.

Golf simulation first, RPG progression second. Online play (SpacetimeDB rooms)
is planned to become the main draw; offline stays a complete solo mode.
Everything is unlocked today: no money, shop, quests or unlock gating.

## Where to start reading

| To change… | Start at |
|---|---|
| The frame loop, saving, audio dispatch | `src/core/app.cpp` |
| Keys → game actions | `src/core/key_bindings.cpp` |
| Menus (new screens go here) | `src/core/startup_flow.h`, drawing in `src/renderer/menu_overlay.h` |
| What the renderer is told each frame, camera rigs | `src/core/render_frame.h`, `src/renderer/render_data.h` |
| Per-frame gameplay | `src/game/game_state.h` |
| Starting courses and holes, the hub | `src/game/course_session.h`, `src/game/play_area.h` |
| XP, collectibles, completion rules | `src/game/progress_rules.h` (pure functions over `save_data`) |
| Feel numbers | `assets/tuning/game_tuning.json` (struct: `src/game/game_tuning.h`) |
| Rewards and skills | `assets/progression/*.json` (`src/game/reward_rules.h`) |
| Ball flight, contact, terrain | `src/physics/` |
| HUD | `src/renderer/hud_overlay.cpp` (scorecards in `scorecard_overlay.cpp`); the GL side is `src/renderer/renderer.cpp` |
| Text size and layout | `src/renderer/pixel_font.h`, styles in `assets/ui/text_styles.json` |
| Content format | the loader next to it (`src/game/*_loader.cpp`), each with `parse_*_from_text` |
| Tooling | `tooling/README.md` (OSM importer, hole editor) |

Reuse these instead of writing your own: `src/physics/vector_math.h` (yaw,
horizontal distance, safe normalize, `clamp01`; use `glm::radians` and
`glm::pi`, never a local `pi`), `src/game/json_util.h` (every JSON read and
directory scan), `tests/test_support.h` (fixtures, `started_hole()`,
`started_game()`, `near()`).

## Build and test

```bash
cmake --preset test && cmake --build build/test && ./build/test/golf++-tests
./build/test/golf++-tests "cart"        # only tests whose name contains "cart"
cmake --preset release && cmake --build build/release   # ./build/release/golf++
python -m unittest test_osm_golf_convert                # in tooling/osm_import/
```

The build uses `-Wall -Wextra -Wpedantic -Wshadow` and must stay warning-free.
Tests use `vendor/doctest.h`, a small runner with `TEST_CASE`, `CHECK` and
`REQUIRE` only. Gameplay tests run on `tests/fixtures/` (not `assets/`), so
content edits don't break them; tests of shipped content live in
`tests/content_tests.cpp`. `GOLFPP_COURSE` and `GOLFPP_VSYNC` are in
`docs/performance.md`.

**Do not introduce new dependencies without flagging it first.** Approved but
not added yet (add each only in the plan phase that needs it): SpacetimeDB CLI,
Emscripten SDK, a Rust client bridge (`spacetimedb-sdk`, OIDC/PKCE, `open`,
`keyring`), SpacetimeAuth. The Steamworks SDK is approved but **blocked** until
the owner has a Steam app id.

---

## Hard rules

**Physics (`src/physics/`) is pure.** Every function takes inputs by value or
const reference and returns a new value: no mutated parameters, no globals, no
`static` locals, no I/O, no profiling. Same inputs, same result, so the server
can replay shots. Gameplay simulation that affects results must not add new
frame-`dt` dependence (ball flight still steps with frame `dt`; plan Phase 1
replaces it).

**No globals or singletons.** `app` owns everything and passes it by
reference. The one exception is the GL function table in
`src/renderer/gl_loader.h` (OpenGL is global state).

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
by an account id, and one account can have several logins.

**Saves.** `src/game/save_data.h` is the offline save. Any change to its shape
bumps `current_save_version` and adds a migration step; parsing ignores
unknown fields and refuses newer versions. Save on hole completion, course
completion, leaving a round and clean exit; never mid-hole, never transient
state. An unreadable save is backed up, never overwritten silently.

## Writing code that documents itself

Bad patterns replicate: assume whatever you write will be copied.

- Each header's first comment says what the file is for. Comments explain
  what and why; never history ("used to", "the old …", "after the refactor").
- One home per job. Before writing a helper, search for it (see the shared
  helpers above). Never copy a function into a second file.
- Delete dead code, unused fields and unused JSON keys instead of leaving them
  for later. Planned work lives in `docs/`, not in unreachable code. (The
  exception is `src/renderer/text_input.*`, the tested name-entry field for plan
  Phase 4.)
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
**Phase 6 (Steam login) is blocked until the owner confirms a Steam app id:
don't start it or add any Steam code.** `docs/steam_todo.md` is the store and
release checklist.

| Phase | What |
|---|---|
| 1 | Deterministic fixed-step `simulate_shot` with playback; network seam in `game_state` |
| 2 | SpacetimeDB server module (C++ to WASM): accounts and login linking, rooms, groups, shots, progress |
| 3 | Rust client bridge and browser login (SpacetimeAuth) |
| 4 | Online and offline menus, login and name entry, remote players and shots, groups |
| 5 | Self-hosting guide, dev tooling, docs |
| 6 | Steam login and linking Steam to existing accounts (blocked) |

When anything from the plan or the Steam todo is implemented, update this file
in the same change, delete it from the plan or todo (renumbering the remaining
phases and sub-sections, and every cross-reference to them), and add any rules
from the plan's "Rule changes to write into AGENTS.md".

## End of every session

- Search `AGENTS.md`, `README.md`, `docs/multiplayer_plan.md` and
  `docs/steam_todo.md` for todos, open decisions ("TBD", "decide whether",
  "owner decides"), "planned" wording and unchecked items that this session
  implemented or decided; remove them and make sure the result is described
  where it belongs (code comments first, this file only for rules).
- Check this file still matches the code: every path in it exists.
- `docs/ideas.md` is the owner's scratchpad: never clean it up or remove entries.
