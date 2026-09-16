# Performance check and budgets

Repeatable release-mode performance pass for golf++, plus the budgets the
renderer work in `docs/oom.md` (tasks 1-10) was aiming at.

This is deliberately not a benchmark framework. It is: build release, boot the
6-hole course, turn on the debug overlay, read eight lines of numbers, compare
them to the table below.

> **Status of the numbers in this document.** Every figure in the budget tables
> is **DERIVED** — read out of the code and out of the unit tests, by agents who
> could not launch the GUI. **Nobody has run the game and looked at the overlay
> yet.** The `measured` columns are empty on purpose. The first person to run
> the release build should fill them in and correct anything that is wrong.

---

## 1. Build release

```pwrshl
cmake --preset release
cmake --build build/release
.\build\release\golf++.exe
```

Or the helper (configure + clean rebuild + launch):

```pwrshl
.\gb -r
```

Run the performance pass on the **release** build. The debug build is not
representative: terrain sampling, chunk culling and the overlay batch builder
are all templated/inlined hot paths that only get optimised in release.

Tests, for reference:

```pwrshl
cmake --preset test
cmake --build build/test
.\build\test\golf++-tests.exe
```

The debug/test suite takes roughly 1-2 minutes; a large part of that is the
terrain spatial-index tests, which sample thousands of points against both the
indexed path and a full-scan oracle.

---

## 2. Start the 6-hole course

### Automatic (preferred)

Set `GOLFPP_COURSE` to a course id and the game skips the menu and boots
straight into that course:

```pwrshl
$env:GOLFPP_COURSE = "marienlyst_golfklub"
.\build\release\golf++.exe
Remove-Item Env:GOLFPP_COURSE
```

bash / MSYS:

```bash
GOLFPP_COURSE=marienlyst_golfklub ./build/release/golf++.exe
```

Course ids are the `"id"` field of the files in `assets/courses/`:

| id | name | holes |
|---|---|---|
| `marienlyst_golfklub` | Marienlyst Golfklub | 6 |
| `course_01` | The Big Three | 3 (placeholder holes) |

The id is matched exactly (case sensitive). An empty or unknown id is ignored,
the reason is logged, and the game starts at the main menu as usual — so a typo
can never leave you profiling something other than what you think.

### Manual (no env var)

From the main menu: <kbd>Down</kbd> to **COURSE**, <kbd>Enter</kbd>, then pick
*Marienlyst Golfklub* with <kbd>Up</kbd>/<kbd>Down</kbd> and <kbd>Enter</kbd>.
Mouse click on a menu row works too.

---

## 3. Turn on the debug overlay

Press <kbd>Ctrl</kbd> (left or right) in game. This toggles the FPS display
**and** the profiling overlay together — `app::run` flips `show_fps_` on
`input_.ctrl.pressed` and assigns it straight to `profiler_.enabled`, so the
profiler records nothing at all while the overlay is off (`profiler_frame()`
returns `nullptr` and every recording helper is a no-op branch).

Numbers are averaged over a 0.25 s publish window
(`profiler::publish_interval_seconds`), so they are steady enough to read but
still react within a quarter second when you move.

Other keys worth knowing for the scenarios below:

| Key | Effect |
|---|---|
| <kbd>Ctrl</kbd> | Toggle FPS + debug overlay |
| <kbd>Enter</kbd> (hold, walking) | Paper course map |
| <kbd>Tab</kbd> (hold, walking) | Compact scorecard |
| <kbd>Caps Lock</kbd> (hold) | Skills panel |
| <kbd>Space</kbd> | Interact / aim / swing |
| <kbd>R</kbd> | Re-tee |
| <kbd>Esc</kbd> / <kbd>Backspace</kbd> | Cancel shot setup / back |

---

## 4. Disable vsync for profiling

Vsync is **on by default and stays on** — the shipping build presents with swap
interval 1. With vsync on, `FPS` pins at the monitor refresh and `SWP` absorbs
all the slack, which is what you want for an acceptance check ("does it hold
60?") but useless for finding headroom.

To present without waiting for vblank:

```pwrshl
$env:GOLFPP_VSYNC = "0"
.\build\release\golf++.exe
Remove-Item Env:GOLFPP_VSYNC
```

Both switches together:

```pwrshl
$env:GOLFPP_VSYNC = "0"; $env:GOLFPP_COURSE = "marienlyst_golfklub"
.\build\release\golf++.exe
Remove-Item Env:GOLFPP_VSYNC, Env:GOLFPP_COURSE
```

Accepted "off" values, case-insensitive and whitespace-trimmed: `0`, `off`,
`no`, `false`, `disable`, `disabled`. **Anything else — including a typo — keeps
vsync on.** The chosen mode is logged at startup
(`vsync off (GOLFPP_VSYNC) (swap interval 0)`), and `SDL_GL_GetSwapInterval()`
is logged rather than the requested value, so a driver that overrides the
setting ("force vsync on" in the control panel) is visible in the log.

### Where these live

| | |
|---|---|
| Parsing | `core/startup_options.h` / `core/startup_options.cpp` — `parse_startup_options(vsync_value, course_value)`, pure, no I/O, unit tested in `tests/startup_options_tests.cpp` |
| Environment read | `core/main.cpp`, via `SDL_getenv`. The only place the environment is touched |
| Vsync applied | `core/window.cpp`, `window::init(..., bool vsync)` -> `SDL_GL_SetSwapInterval` |
| Course boot applied | `core/app.cpp`, `app::boot_into_course` called from `app::init` |

Nothing in `src/game/`, `src/physics/` or `src/renderer/` reads either option.
That is the point: no machine-specific assumption gets into gameplay code, and
the simulation behaves identically whichever way the game was launched.

---

## 5. Reading the overlay

The overlay is the output of `format_profile_overlay_lines`
(`src/profiling/profiling.cpp`). The bitmap font has no `.` glyph, so all
timings are whole microseconds.

```
UPD <n>US MESH <n>US
MRD <n>US SWP <n>US
REN <n>US CRT <n>US
SCN <n>US OVL <n>US
DRAW <n> UNI <n> ULOC <n>
BUF <writes>/<reallocs> <bytes> DBGUI <n>
TSAMP <n> TTRI <n>
CHUNK <visible>/<culled> R <n> IDX <n> TREE ON|OFF
GPU TER <n>US TRE <n>US        (only when GL timer queries exist)
GPU OVL <n>US CRT <n>US        (reported 2 frames late)
```

### Every counter

| Counter | Source | What it is | What it tells you |
|---|---|---|---|
| `UPD` | `profile_stage::update_game` | CPU µs in `update_game` | Simulation cost: physics step, player/cart movement, terrain sampling |
| `MESH` | `refresh_render_mesh_cache` | CPU µs rebuilding the cached render meshes + chunks | Should be ~0 except on the frame a hole/hub loads. Non-zero every frame = the terrain revision cache is thrashing |
| `MRD` | `make_render_data` | CPU µs building the frame's render data | The task-11 budget target: well under 1 ms |
| `SWP` | `window_swap` | CPU µs inside `SDL_GL_SwapWindow` | With vsync on this is the vblank wait — large `SWP` means you have headroom, not a problem |
| `REN` | `renderer::render` | CPU µs for the whole render call | `SCN + OVL + CRT` plus FBO setup |
| `SCN` | `render_scene` | CPU µs submitting the 3D scene | Culling, marker batch build, draw submission |
| `OVL` | `render_overlay` | CPU µs building + submitting the 2D overlay | Overlay batch build (text, HUD, menus) |
| `CRT` | `render_crt` | CPU µs for the CRT upscale pass | Should be tiny on CPU: one fullscreen triangle pair |
| `DRAW` | `draw_calls` | `glDraw*` submissions, **excluding** the debug overlay's own | The draw-call budget |
| `UNI` | `uniform_sets` | `glUniform*` calls | State churn per frame |
| `ULOC` | `uniform_location_queries` | `glGetUniformLocation` calls = uniform location cache **misses** | Must be 0 after warmup (task 5) |
| `BUF` writes | `buffer_writes` | `glBufferSubData` streaming writes | One per dynamic buffer actually written this frame |
| `BUF` reallocs | `buffer_reallocations` | `glBufferData` storage re-specifications | Must settle to 0; the streaming buffers are grow-only (task 9) |
| `BUF` bytes | `buffer_upload_bytes` | Bytes sent by both calls, rounded (`512B`/`12KB`/`34MB`) | Streaming volume |
| `DBGUI` | `debug_overlay_draw_calls` | Draw calls spent drawing the debug overlay itself | Subtracted out of `DRAW`/`UNI`/`BUF` so the overlay does not inflate what it reports. Normally 1 |
| `TSAMP` | `terrain_sample_calls` | `sample_terrain_mesh` calls this frame | How much terrain sampling the frame asked for |
| `TTRI` | `terrain_triangles_tested` | Triangles actually tested across those calls | The task-2 budget: near the local candidate count, not `TSAMP × mesh triangles` |
| `CHUNK v/c` | `visible_chunks`/`culled_chunks` | Terrain + material overlay chunks kept / rejected by the frustum test | Culling effectiveness (task 10) |
| `R` | `chunk_draw_ranges` | Merged `glDrawElements` ranges for those meshes | Capped at 4 (terrain) + 2 (material overlay) |
| `IDX` | `chunk_indices_drawn` | Indices submitted after culling and range merging | Compare against the mesh total; see §6 |
| `TREE` | `trees_visible` | `ON` when the instanced tree batch survived the whole-batch frustum test | `OFF` means both tree draws were skipped entirely |
| `GPU TER/TRE/OVL/CRT` | `gl_timer` | GPU µs per pass | Only present when `GL_ARB_timer_query` resolved; **reported 2 frames late**, so do not correlate them with a single-frame spike |

`IDX` is drawn indices only; the mesh total is `chunk_indices_total` in the
profile struct and is not printed — divide `IDX` by the total you know from
`MESH`-time logging, or just watch `CHUNK culled` instead.

---

## 6. Budgets

Scenario definitions, all on **Marienlyst Golfklub** (6 holes) in release:

| Scenario | How to get there |
|---|---|
| **Hub walking** | Boot the course, walk around the hub on foot (not in the cart) |
| **Driving the cart** | Board the golf cart in the hub and drive it around |
| **Aiming on a hole** | Walk to a hole start, interact, <kbd>Space</kbd> to enter aiming |
| **Ball in flight** | Take a shot, read the overlay while the ball is moving |
| **Course map open** | Hold <kbd>Enter</kbd> while walking |
| **Main menu** | Launch without `GOLFPP_COURSE` |

### Frame budget

| Scenario | Target FPS (vsync on) | Target frame ms | `MRD` | measured |
|---|---|---|---|---|
| Hub walking | 60 (vsync-locked) | 16.7 | < 0.30 ms | |
| Driving the cart | 60 (vsync-locked) | 16.7 | < 0.30 ms | |
| Aiming on a hole | 60 | 16.7 | < 0.60 ms | |
| Ball in flight | 60 | 16.7 | < 0.30 ms | |
| Course map open | 60 | 16.7 | < 0.40 ms | |
| Main menu | 60 | 16.7 | < 0.20 ms | |

`make_render_data` must be **well under 1 ms** everywhere. Aiming gets the
loosest budget because it is the only mode that does real work in
`make_render_data`: `estimate_aim_arc` walks up to 28 ballistic steps and
terrain-samples each one.

### GL and sampling budget — all values DERIVED

| Counter | Hub walking | Driving the cart | Aiming | Ball in flight | Course map | Main menu | measured |
|---|---|---|---|---|---|---|---|
| `DRAW` before UI | 11-13 | 11-13 | 13-15 | 12-16 | 11-13 | 8-12 | |
| `DRAW` total (incl. UI) | 12-14 | 12-14 | 14-16 | 13-17 | 14-17 | 9-13 | |
| `UNI` | 40-70 | 40-70 | 50-80 | 50-85 | 40-70 | 35-60 | |
| `ULOC` | 0 | 0 | 0 | 0 | 0 | 0 | |
| `TSAMP` | 1-4 | 1-4 | 29-34 | 3-6 | 1-4 | 0-2 | |
| `TTRI` | < 200 | < 200 | < 1400 | < 300 | < 200 | < 100 | |
| `BUF` writes | 2-3 | 2-3 | 2-3 | 3-4 | 2-3 | 1-2 | |
| `BUF` reallocs | 0 | 0 | 0 | 0 | 0 | 0 | |
| `BUF` bytes | < 64KB | < 128KB | < 96KB | < 96KB | < 128KB | < 64KB | |
| `DBGUI` | 1 | 1 | 1 | 1 | 1-2 | 1 | |
| `CHUNK` culled | > 0 | > 0 | > 0 | > 0 | > 0 | ≥ 0 | |
| `TREE` | ON (OFF facing away) | ON | ON | ON | ON | ON/OFF | |

**Hard ceiling from task 11: world draw calls before UI stay under roughly 50.**
Every scenario above is far under that, with a wide margin.

The golf cart used to be the exception. `draw_cart_model` was 16 immediate-mode
draws (6 panels, 2 detail cylinders, 4 canopy posts, 2 wheels × cylinder + hub
cap) with 5 uniform sets each, so driving the cart landed around **27-32 draws
before UI** and roughly **80 extra `UNI`**. Task 12 folded the cart into the
existing world marker batch: every piece is opaque, flat-colored
(`u_use_vertex_color = 0`, which is unlit) and depth-writing, so the cart is now
pre-transformed on the CPU and rides along in the marker pass's leading
depth-writing run. Driving costs **0 extra draws and 0 extra uniform sets** —
it is indistinguishable from walking on the overlay, except for `BUF` bytes
(1956 extra vertices × 28 B ≈ 55 KB per frame in the same grow-only buffer,
whose initial capacity was raised to 4096 vertices so it still never
reallocates). The one degenerate case is a frame with no markers at all, where
the batch goes from 0 draws to 1; the hub and every hole always draw markers,
so in practice `DRAW` drops by exactly 16 when the cart is active.

### Where the draw calls come from (`render_scene`, in order)

| Draw | Count | Note |
|---|---|---|
| Background ground quad | 1 | always |
| Chunked course terrain + apron | 1-4 | `max_terrain_draw_ranges = 4` |
| Chunked material overlay | 0-2 | `max_material_overlay_draw_ranges = 2` |
| Trees | 0 or 2 | instanced trunk + canopy, whole batch frustum culled (task 6/10) |
| World markers + cart | 3-5 | one `glDrawArrays` per depth-write run in the batch (task 7). The golf cart is appended first, into the leading depth-writing run, so `cart_active` adds vertices but no draws (task 12) |
| Emote props (smoke/beer) | 0-8 | only while an emote plays; still immediate-mode, because the smoke puffs alpha-blend and the batch is opaque-only |
| Flight path line strip | 0 or 1 | only while the ball is moving |
| Ball | 1 | always |
| **UI overlay** | 1 | one batched draw for the whole 2D overlay (task 8) |
| Course map | +2 | the map's retained fill splits the overlay: flush + retained draw (task 9) |
| Debug overlay | +1 `DRAW`, 1 `DBGUI` | the overlay flushes underneath itself so its own cost lands in `DBGUI` |
| CRT pass | 1 | mandatory fullscreen post-process, never optional |

### Why `TSAMP` and `TTRI` look like that

Per-frame `sample_terrain_mesh` callers:

- walking/cart: 1 sample to place the player on the ground
  (`state.player.position.y = terrain_height_at(...)`)
- ball at rest: 1 sample in `step_ball`
- ball moving: 2 samples in `step_ball` (before the step, then a
  `previous_sample`-hinted one after)
- aiming: up to 28 unhinted samples in `estimate_aim_arc`
- anchors (tee, pin, trees, hub markers): **0 in a steady frame** — they are
  cached in `game_state::static_anchors` keyed on `terrain_render_revision`
  (task 3) and only re-sampled when the terrain revision changes

`TTRI / TSAMP` is the interesting ratio. Measured by the unit tests in
`tests/physics_tests.cpp` on the real mesh builder:

| Sample kind | Triangles tested (avg) | vs full scan |
|---|---|---|
| Unhinted, 1392-triangle mesh | **39.2** | 1392 |
| Unhinted, small mesh | **9.2** | 1392 |
| `previous_sample`-hinted walk | **12.0** | 1392 |
| Unhinted, 4432-triangle mesh | **19.4** | 4432 |

So `TTRI` should track roughly `TSAMP × 10..40`, **independent of mesh size**.
Aiming: 28 unhinted samples × ~40 ≈ 1100, hence the `< 1400` budget.

---

## 7. How to tell if something regressed

Read these off the overlay. Each one maps to a specific optimisation.

| Symptom | What broke | Where to look |
|---|---|---|
| `ULOC > 0` after the first second | The uniform location cache is missing every frame — a shader is being re-created, or `uniform_location()` is being bypassed | `src/renderer/shader.cpp`, `uniform_location_cache` (task 5) |
| `TTRI ≈ TSAMP × mesh triangle count` | The terrain spatial index fell back to the full scan. `sample_terrain_mesh` calls `terrain_index_matches(mesh, index)` and, when the index no longer describes the mesh, runs *exactly* the original full scan. Something built or transformed a `terrain_mesh` without rebuilding `spatial_index` | `src/physics/terrain.cpp` ~line 1219 (task 2) |
| `TSAMP` grows with the number of trees / hub markers | The static anchor cache is being rebuilt every frame — `terrain_render_revision` is being bumped per frame, or `static_anchor_cache_is_current` returns false | `src/game/game_state.cpp`, `refresh_static_anchor_cache` (task 3) |
| `MESH` non-zero every frame | Same root cause as above, seen from the render side: the cached terrain/overlay mesh and its chunks are being rebuilt each frame | `core/app.cpp`, `app::refresh_render_mesh_cache` (task 4/10) |
| `BUF` reallocs > 0 every frame | A dynamic buffer is re-specifying storage every frame instead of streaming. Either the stream genuinely grows without bound, or `glBufferSubData` failed to resolve (a log line says so at startup and the fallback re-specifies on every upload) | `src/renderer/dynamic_buffer.cpp` (task 9) |
| `DRAW` jumps by hundreds | A batch path regressed. ~200 extra draws with ~105 trees on the hub = instancing fell back to per-tree draws; ~16 extra only while driving = the cart fell back to immediate draws; ~50+ extra = the world marker batch broke into per-marker draws; hundreds on a text-heavy screen = the overlay batch broke into per-glyph or per-quad draws | `tree_renderer.cpp` (6), `world_marker_*.cpp` / `cart_batch.cpp` (7, 12), `overlay_pass.cpp` / `overlay_batch.cpp` (8) |
| `CHUNK culled` always 0 while you turn on the spot | Frustum culling is not rejecting anything: the chunk list is empty (so the mesh draws as one range), the chunk bounds are invalid, or the frustum planes are wrong | `render_mesh_chunks.cpp`, `frustum.cpp` (task 10) |
| `R` pinned at its cap (4 / 2) with `IDX` ≈ the mesh total | Ranges are being merged so aggressively that culling buys nothing — the chunks are not spatially coherent any more | `build_render_mesh_chunks`, `limit_render_index_ranges` |
| `TREE OFF` while trees are clearly on screen | The whole-batch tree bounds are wrong | `tree_renderer::draw` |
| `DBGUI` climbing above ~2 | The debug overlay is no longer flushed as one block, so it is now paying for what it measures | `draw_debug_overlay` in `renderer.cpp` |
| FPS below refresh with a *small* `SWP` | Genuinely CPU/GPU bound — read `UPD`/`MRD`/`SCN`/`OVL` and the `GPU` lines to see which | — |

---

## 8. Background: what tasks 1-10 changed

Why the budgets above are achievable at all. Course mode started this work at
roughly **2 FPS**. Every before/after number here is **DERIVED** from the code
and the unit tests, not measured in the running game.

| # | Change | Before | After |
|---|---|---|---|
| 1 | Profiling instrumentation: `profiler` owned by `app`, nullable `frame_profile*`, debug overlay, optional GL timer queries | no visibility | 8 CPU stages, 13 counters, 4 GPU stages |
| 2 | Terrain spatial index — deterministic uniform XZ grid, CSR storage, `previous_sample` fast path, bit-identical results to the full scan | 1392 triangles tested *per sample* | 9-40 per unhinted sample, ~12 hinted; flat in mesh size |
| 3 | Static anchor cache (`game_state::static_anchors`) keyed on `terrain_render_revision`: tee, pin, ~105 tree bodies, hub markers | ~110+ anchor samples every frame | 0 in a steady frame |
| 4 | `render_static_mesh::bounds` AABB cached at build time | full vertex scan per frame | computed once per terrain revision |
| 5 | Per-program uniform location cache | a `glGetUniformLocation` per uniform per draw | `ULOC` 0 after warmup |
| 6 | Instanced trees (`tree_renderer`) | ~2 draws per tree, ~210 on the hub | **2 draws total** |
| 7 | Batched world markers (`world_marker_batch`) — one vertex stream, one draw per depth-write run | one draw per marker / aim dot / flagstick | **3 draws** (5 with the aim indicator) |
| 8 | Batched UI overlay (`overlay_pass` + `overlay_batch` + `pixel_font`) | per-quad, effectively per-glyph-pixel draws | **~1 draw** for the whole 2D overlay |
| 9 | Grow-only streaming via `dynamic_buffer`, `glBufferSubData` into existing storage; retained course-map fill cached by revision | `glBufferData` re-specification every frame, map fill rebuilt every frame | reallocs settle to 0; map fill uploaded once per revision |
| 10 | Chunked terrain (order-preserving spatial runs, ~360 chunks of ~48 triangles at `target_extent = 48 m`) + per-chunk frustum culling, merged into ≤ 4 (+2) ranges; whole-batch tree cull | whole mesh submitted every frame | 75-99% of indices culled walking the hub, ~0.1% extra indices from range merging |

The chunking scheme deliberately does **not** reorder the index buffer:
overlapping coplanar material zones and overlapping hole/apron terrain in the
hub resolve by draw order under `GL_LESS`, so a spatial re-sort would flip which
surface wins. Chunks are contiguous index ranges over the authored order.

Task 12, after this document was first written:

| # | Change | Before | After |
|---|---|---|---|
| 12 | Golf cart folded into `world_marker_batch` (`cart_batch.cpp`); unit cylinder/sphere generators moved to `primitive_mesh.cpp` so the GPU VBOs and the CPU batch share one source | 16 draws + 80 uniform sets while `cart_active` | **0 extra draws, 0 extra uniform sets** |

---

## 9. Open follow-ups — be honest about these

- **No one has launched the GUI.** Visual verification of *every* screen (hub,
  hole play, course map, scorecard, skills panel, shop, menus, results) is still
  pending a manual run. Tasks 1-10 and this pass were all verified by unit tests
  and by reading the code. The `measured` columns above are empty for that
  reason, and the derived figures should be treated as predictions until someone
  confirms them.
- **The test suite takes ~1-2 minutes** in the debug/test preset, mostly the
  terrain index tests comparing indexed sampling against a full-scan oracle over
  thousands of sample points. That is intentional — those tests are what
  guarantee the index is bit-identical to the old behaviour — but it makes the
  edit/test loop slow.
- **Markers and aim dots are rebuilt on the CPU every frame by design.**
  `build_world_marker_batch` runs in `render_scene` each frame because the aim
  indicator, swing club and ball position genuinely change every frame. It is
  cheap (a few hundred vertices into a reused vector, no allocation in steady
  state) but it is not cached, and it shows up in `SCN`.
- **The emote props are the only unbatched models left** (0-8 draws, and only
  while an emote plays). They stay immediate-mode on purpose: the smoke puffs
  alpha-blend, and `world_marker_batch` is documented opaque-only. Batching them
  would mean giving the batch a blended, order-dependent run type.
- **Folding the cart into the marker batch moved the *markers* ahead of the
  emotes.** The batch (cart first, then markers) is now drawn at the point in
  `render_scene` where the cart used to be, and the emotes are submitted after
  it. That keeps the cart in exactly its old position relative to the blended
  smoke — which matters, because the puffs rise through the canopy roof — and
  it is strictly more correct overall, since all opaque geometry is now drawn
  before the only blended geometry. But it is a visual change nobody has looked
  at: a smoke puff in front of a distant flagstick now composites over the
  flagstick instead of over the terrain behind it. Worth a glance next time
  someone runs the game.
- **GPU timer lines are 2 frames late** and are absent entirely on drivers
  without `GL_ARB_timer_query`. Do not correlate them with a one-frame spike.
- **Vsync can be forced on by the driver** regardless of `GOLFPP_VSYNC`. The
  startup log prints `SDL_GL_GetSwapInterval()`, so check the log before
  concluding the switch did nothing.
