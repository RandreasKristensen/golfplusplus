# Renderer Speedup Agent Prompts

## Status: all 11 tasks implemented — do not redo them

This file is kept for reference. The original prompts below are unchanged, but
**every task in it has been implemented and merged.** Read
[`performance.md`](performance.md) before acting on anything here: it has the
budgets, the overlay counter reference, the regression symptoms, and a
per-task summary of what actually landed.

| # | Task | Status | Landed in |
|---|---|---|---|
| 1 | Renderer performance instrumentation | done | `src/profiling/profiling.*`, `src/renderer/gl_timer.*`, debug overlay on `Ctrl` |
| 2 | Spatial index for terrain sampling | done | `src/physics/terrain.cpp` (uniform XZ grid, CSR, `previous_sample` fast path) |
| 3 | Cache static terrain-anchored data | done | `game_state::static_anchors`, keyed on `terrain_render_revision` |
| 4 | Cache render mesh bounds | done | `render_static_mesh::bounds`, `compute_render_mesh_bounds` |
| 5 | Cache shader uniform locations | done | `src/renderer/shader.cpp`, `uniform_location_cache` |
| 6 | Instance or batch tree rendering | done | `src/renderer/tree_renderer.*` — 2 instanced draws total |
| 7 | Batch markers, pins, aim dots, panels | done | `src/renderer/world_marker_batch.*`, `world_marker_renderer.*` |
| 8 | Batched pixel UI buffer | done | `src/renderer/overlay_pass.*`, `overlay_batch.*`, `pixel_font.*` |
| 9 | Preallocate and stream dynamic buffers | done | `src/renderer/dynamic_buffer.*`, `course_map_fill.*` |
| 10 | Coarse culling and course render chunking | done | `src/renderer/render_mesh_chunks.*`, `frustum.*`, `renderer::cull_stats()` |
| 11 | Release-mode performance acceptance pass | done | `docs/performance.md`, `core/startup_options.*` (`GOLFPP_VSYNC`, `GOLFPP_COURSE`) |

Outstanding, and deliberately not closed: **nobody has launched the GUI yet.**
The budgets in `performance.md` are derived from code and unit tests, not
measured in game, and the `measured` columns there are waiting to be filled in
on the first real run. See its "Open follow-ups" section.

---

## 1. Add renderer performance instrumentation

Prompt:

Read `AGENTS.md` first. Work in `C:\Users\arand\Desktop\AU\sjov\golfplusplus`.

Add lightweight profiling instrumentation so we can prove where the 2 FPS course mode is spending time before changing behavior. Keep it behind the existing FPS/debug UI path or a small compile/runtime flag, and do not introduce new dependencies.

Measure CPU time for:

- `update_game`
- `refresh_render_mesh_cache`
- `make_render_data`
- `renderer::render`
- `renderer::render_scene`
- `renderer::render_overlay`
- `renderer::render_crt`
- `window::swap`

Add counters for:

- terrain sample calls per frame
- terrain triangles tested per frame
- GL draw calls per frame
- GL uniform sets per frame
- dynamic `glBufferData` uploads per frame

If practical within this task, add optional OpenGL timer queries for terrain, trees, overlay, and CRT. Keep this optional and robust if timer queries are unavailable.

Display a compact debug overlay when FPS display is enabled. Keep the normal game UI unchanged when FPS display is off.

Verification:

- Run `cmake --preset test` from `C:\Users\arand\Desktop\AU\sjov\golfplusplus`.
- Run `cmake --build C:\Users\arand\Desktop\AU\sjov\golfplusplus\build\test`.
- Run `C:\Users\arand\Desktop\AU\sjov\golfplusplus\build\test\golf++-tests`.
- Build release with `cmake --preset release` and `cmake --build C:\Users\arand\Desktop\AU\sjov\golfplusplus\build\release`.
- Report the measured bottlenecks for single-hole mode and 6-hole course mode.

## 2. Add a spatial index for terrain sampling

Prompt:

Read `AGENTS.md` first. Work in `C:\Users\arand\Desktop\AU\sjov\golfplusplus`.

Optimize `sample_terrain_mesh` in `src/physics/terrain.cpp`. It currently scans every triangle for every sample, which makes course mode far too slow. Add a deterministic acceleration structure to `terrain_mesh` that lets sampling test only nearby triangles.

Preferred approach:

- Add XZ bounds and a simple uniform grid or section-range index to `terrain_mesh`.
- Build the index when terrain meshes are created or transformed.
- Keep `src/physics/` pure: no global state, no mutation of function parameters, no I/O, no static locals.
- Preserve deterministic sampling results as closely as possible.
- Make the `previous_sample` path fast by checking the previous triangle and nearby candidates before falling back.
- Keep a safe fallback path for malformed or empty meshes.

Tests:

- Existing terrain sample behavior still passes.
- Add tests showing sampled height/material matches the old full-scan behavior on representative fairway, rough, green, bunker, water, edge, and outside-surface positions.
- Add tests showing repeated samples with `previous_sample` are deterministic.

Verification:

- Run `cmake --preset test` from `C:\Users\arand\Desktop\AU\sjov\golfplusplus`.
- Run `cmake --build C:\Users\arand\Desktop\AU\sjov\golfplusplus\build\test`.
- Run `C:\Users\arand\Desktop\AU\sjov\golfplusplus\build\test\golf++-tests`.
- Report before/after terrain sample call counts, triangle tests per frame, and FPS in course mode using the instrumentation from prompt 1 if available.

## 3. Cache static terrain-anchored render and collision data

Prompt:

Read `AGENTS.md` first. Work in `C:\Users\arand\Desktop\AU\sjov\golfplusplus`.

Remove per-frame terrain anchoring for static objects. `make_render_data` currently recomputes tee/pin positions, tree render bases, and hub markers every frame, and `anchored_tree_bodies` rebuilds collision data during ball update. Cache these when terrain/course state changes.

Implement a cache owned outside the renderer, likely in `app` and/or `game_state`, keyed by `terrain_render_revision`.

Cache:

- anchored tee position
- anchored pin position
- hub tee markers
- hub pin markers
- hub start markers
- `std::vector<render_tree>`
- `std::vector<tree_collision_body>`
- any other static terrain-anchored positions discovered while working

Rules:

- Do not add global mutable state.
- Do not put game logic in the renderer.
- Do not change save data for this transient cache.
- Keep cache invalidation explicit when `terrain_render_revision` changes.

Verification:

- Run the full test command chain from prompt 2.
- Confirm course start, single-hole start, hub walking, tree collision, and hole markers still behave correctly.
- Report before/after `make_render_data` CPU time and terrain sample count per frame.

## 4. Cache render mesh bounds and remove per-frame static scans

Prompt:

Read `AGENTS.md` first. Work in `C:\Users\arand\Desktop\AU\sjov\golfplusplus`.

Remove full static mesh scans from the render loop. `renderer::render_scene` scans terrain vertices each frame to find `terrain_min_y`. Move this into cached render mesh metadata built in `refresh_render_mesh_cache`.

Implement:

- Add bounds/min/max metadata to `render_static_mesh`.
- Populate it when cached terrain and overlay meshes are built.
- Use cached bounds in `renderer::render_scene` instead of iterating vertices.
- Keep fallback behavior for empty mesh data.

Verification:

- Run the full test command chain from prompt 2.
- Build release.
- Confirm visual output still has the background ground below the course.
- Report whether render scene CPU time changed.

## 5. Cache shader uniform locations and reduce GL state churn

Prompt:

Read `AGENTS.md` first. Work in `C:\Users\arand\Desktop\AU\sjov\golfplusplus`.

Optimize `src/renderer/shader.cpp` so uniform setters do not call `glGetUniformLocation` every time. Uniform locations are stable after successful program link until relink, so cache them per shader program.

Implement:

- Add a uniform location cache to `shader_program`.
- Preserve current setter API if possible.
- Clear the cache on `shutdown` and reload.
- Avoid excessive allocations in hot paths. A small fixed known-uniform table is acceptable if cleaner than a map.
- Optionally track redundant `glUseProgram`, `glBindVertexArray`, blend/depth state changes if this stays small and low risk.

Verification:

- Run the full test command chain from prompt 2.
- Build release.
- Use instrumentation to report before/after uniform location queries and uniform sets per frame.

## 6. Instance or batch tree rendering

Prompt:

Read `AGENTS.md` first. Work in `C:\Users\arand\Desktop\AU\sjov\golfplusplus`.

Replace per-tree draw submission with batched or instanced rendering. The course currently draws each tree as one cylinder trunk draw plus one cone leaf draw. For 99 trees, that is 198 draw calls before markers, UI, ball, cart, and CRT.

Preferred approach:

- Add instance buffers for tree trunks and tree leaves.
- Upload per-tree model/color data when tree render cache revision changes.
- Draw all trunks in one instanced draw and all leaves in one instanced draw.
- Keep OpenGL 3.3 core compatibility.
- Do not add new dependencies.
- Keep tree visuals equivalent or close enough for the VCR/CRT aesthetic.

If instancing is too invasive, batch tree geometry into one static mesh per revision instead.

Verification:

- Run the full test command chain from prompt 2.
- Build release.
- Confirm trees render in the correct positions/heights in single-hole and course mode.
- Report before/after draw calls and FPS.

## 7. Batch markers, pins, aim dots, and simple world panels

Prompt:

Read `AGENTS.md` first. Work in `C:\Users\arand\Desktop\AU\sjov\golfplusplus`.

Reduce draw calls for repeated small world geometry after tree batching is complete. Target markers, pin poles/flags, aim dots, and simple panels that currently issue separate uniform updates and draw calls.

Implement:

- Add a small world-quad/marker batch path, or use instancing for repeated disc/panel geometry.
- Preserve existing colors, alpha behavior, and depth behavior.
- Keep dynamic aim dots efficient without reallocating more than needed.
- Avoid changing gameplay logic.

Verification:

- Run the full test command chain from prompt 2.
- Build release.
- Check tee markers, pin markers, start markers, aim indicator, swing club, and flight path visuals.
- Report before/after draw calls and render scene CPU time.

## 8. Replace per-glyph-pixel UI draws with a batched pixel UI buffer

Prompt:

Read `AGENTS.md` first. Work in `C:\Users\arand\Desktop\AU\sjov\golfplusplus`.

The current pixel text renderer draws one quad per lit glyph pixel, with uniform updates and draw calls per quad. Replace this with a batched overlay path while preserving the bitmap/pixel aesthetic.

Implement:

- Build a dynamic overlay vertex buffer each frame for UI quads.
- Accumulate pixel glyph quads, panels, ticks, and simple overlay rectangles into batches.
- Draw by color/layer in as few draw calls as practical.
- Preallocate/reuse CPU vectors and GL buffers where possible.
- Keep all existing UI screens visually equivalent: startup menus, controls overlay, power meter, FPS/debug overlay, scorecard, course map, rangefinder, XP drops.

Do not add SDL_ttf or smooth font rendering.

Verification:

- Run the full test command chain from prompt 2.
- Build release.
- Manually check menus, scorecard, course map, and gameplay HUD.
- Report before/after overlay draw calls and CPU time.

## 9. Preallocate and stream dynamic buffers safely

Prompt:

Read `AGENTS.md` first. Work in `C:\Users\arand\Desktop\AU\sjov\golfplusplus`.

Clean up dynamic buffer uploads so hot-path data does not repeatedly allocate GPU storage. Focus on flight path, UI overlay batches, and any dynamic instance buffers added by earlier prompts.

Implement:

- Track dynamic buffer capacity.
- Use `glBufferSubData` when data fits existing capacity.
- Orphan with `glBufferData(..., nullptr, GL_DYNAMIC_DRAW)` only when resizing or intentionally avoiding synchronization.
- Avoid per-frame heap churn in CPU-side temporary vectors where practical.
- Keep behavior identical.

Verification:

- Run the full test command chain from prompt 2.
- Build release.
- Report dynamic buffer upload count, bytes uploaded per frame, and any FPS/render-time change.

## 10. Add coarse culling and course render chunking

Prompt:

Read `AGENTS.md` first. Work in `C:\Users\arand\Desktop\AU\sjov\golfplusplus`.

Add culling and render chunking so the renderer scales beyond the current 6-hole course. This should be done after sampling and batching optimizations, not before.

Implement:

- Split course terrain into logical chunks, preferably per hole plus apron or spatial chunks with bounds.
- Store bounds per chunk.
- Frustum-cull chunks against the current camera before drawing.
- Frustum-cull tree/marker batches or split them into chunks.
- Keep the current CRT framebuffer pipeline intact.
- Do not implement a seamless open world or backend.

Verification:

- Run the full test command chain from prompt 2.
- Build release.
- Confirm single-hole and 6-hole course rendering still look correct.
- Report visible chunk count, culled chunk count, draw calls, and FPS.

## 11. Add a release-mode performance acceptance pass

Prompt:

Read `AGENTS.md` first. Work in `C:\Users\arand\Desktop\AU\sjov\golfplusplus`.

After the renderer optimizations are complete, add a repeatable release-mode performance check and document the expected budgets. This is not a formal benchmark framework; keep it simple and useful for future agents.

Implement:

- Add a documented manual or automated way to start the 6-hole course and collect a short performance sample.
- Record target budgets in docs:
  - course mode should hit vsync at 60 FPS on the current development machine
  - normal world draw calls should stay under roughly 50 before UI
  - `make_render_data` should be well under 1 ms
  - terrain triangle tests per frame should be near the number of actual local candidates, not full mesh size times sample calls
- Add notes on how to temporarily disable vsync for profiling if implemented.
- Do not hardcode machine-specific assumptions into gameplay code.

Verification:

- Run the full test command chain from prompt 2.
- Build release.
- Run the performance check and paste the final numbers into the docs.
