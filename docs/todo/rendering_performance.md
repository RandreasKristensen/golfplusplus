# Rendering performance and picture

Owner priority (2026-11-18): broad low-end support comes first. Reaching
60 FPS on the gaming laptop is insufficient; it must have substantial
headroom. Preserve the fixed world and real reflections while reducing the
work needed to draw them. Offer optional picture resolutions. Remove the plan when its implementation
and evidence are complete.

Read the [rendering rules](../rendering_rules.md),
[development procedures](../development.md) and
[performance record](../performance.md) first. Keep OpenGL 3.3 / GLSL 330 and
the macOS path: no compute, bindless textures, persistent mapping,
base-instance draws or indirect multidraw. No new dependency. Never weaken a
visual test to meet a timing target. Do not restore camera-dependent grass
seeds, screen-space or sky-substitute reflections, or different sparse trees.

## Where things stand (2026-11-22)

The first recovery round is done, uncommitted on main: full-app benchmark and
accounting, rough-grass shimmer fix (`blade_min_pixels`), static tree/prop/
caster caches with once-per-frame dynamic uploads, conditional and cropped
water mirrors, tiled grass storage with a disposable cache file, stripe-free
short turf with a lamp-light cap for night greens, far-tree images baked from
the same models, cached sun/lamp shadow maps, merged terrain draw ranges,
depth-first dense crowns, and the shadow-map sampler fix for trees and grass.
Measurements and decisions are in [performance.md](../performance.md); rules
in [rendering rules](../rendering_rules.md). 1253 tests pass and
`golfpp-picture-check` passes on all 18 courses.

Full run `build/release/benchmarks/after-section-10` against the
[2026-11-19 baseline](../performance.md#2026-11-19-full-app-baseline): worst-view
frame p95 per course fell from 34-86 ms to 12-46 ms, but **every course still
fails the timing budgets**. View-repeats failing (of 444): CPU p95 440,
GPU p95 436, frame p95 381, frame p99 261, solo dynamic upload 226. Grass GPU,
grass RAM, logical GPU and uniform-miss gates now pass. Worst views: the
40-nearby crowd (`play_40`, `plaza_40`) and the zoomed night flag. The
investigation (section 1) found the solo CPU failures are driver waits on GL
state read-backs, not work, and the outliers environmental (battery, stalls in
`SwapWindow`); step 2 is its ranked fix list.

Step 2 is built (all fixes but the owner's budget decision). Full run
`build/release/benchmarks/after-section-2` ([result](../performance.md#second-recovery-round-result-2026-11-22), 501
view-repeats): solo CPU p95 now passes 295 of 333 views (4 before; `play` median 0.87 ms, worst 2.13), memory falls to
at most 740 MB working set and 160 MiB logical GPU, and the solo upload, working-set, load-peak and logical-GPU gates
pass. Still failing: GPU p95/p99 (501/500 view-repeats) and frame p95/p99 (483/370), but that session ran
thermally throttled (median 667 MHz against 1050), so GPU and frame figures are not comparable with
`after-section-10`; CPU p95 fails in 106 view-repeats: the walking 40-figure crowd (4.4-18 ms), the zoomed night
flag and a few `shot_rain`/walk repeats. A cool, back-to-back GPU table is the next evidence to gather.

Awaiting the owner: acceptance of the turf look (`build/test/turf_review/`,
`--turf=before|after`) and of trees and grass now receiving sun shadows
(`build/test/shadow_review/`), and an in-game look at standing flagsticks:
each view draws them at least one of its own pixels wide again (lost in
commit 3a615d7): a far pole is one pixel wide in the rangefinder too, not the
main view's width magnified (projected tests in `render_frame_tests.cpp`).

## Acceptance targets

Initial engineering budgets on the gaming reference (Ryzen 7 5800HS /
RTX 3050 Ti Laptop). **Owner decision (2026-11-18):** all testing happens on
this machine; no older device is needed to complete or sign off any step, so
do not stop or ask about one. These budgets are the gate and are already the
large headroom; record a derived low-end estimate beside them, labelled
estimated, not measured.

| Measurement | Target |
|---|---|
| Complete uncapped app loop | p95 <= 5 ms, p99 <= 8 ms on every course |
| CPU update + preparation + submission | p95 <= 2.5 ms; waits exposed separately |
| Complete GPU frame | p95 <= 3 ms, p99 <= 4 ms, asynchronous measurement |
| Grass resident resources | <= 8 MiB GPU, <= 16 MiB decoded CPU tiles, <= 64 MiB total grass RAM |
| Far-tree image cache | <= 24 MiB including every channel/view/mip |
| Whole game | <= 768 MiB steady working set, <= 1 GiB load peak, <= 256 MiB logical GPU |
| Warm stationary solo view | zero static uploads, uniform misses and buffer growth |
| Solo dynamic uploads across all passes | <= 256 KiB/frame |

Frame targets apply to shipped settings; developer tuning extremes must stay
correct and memory-bounded but are separate stress results.

## How to work

- One writer at a time in the shared tree. Agents stopped by a usage limit
  resume where they stopped (message them); never start a replacement while
  the stopped one can still resume.
- Test, screenshot and benchmark runs never play sound: `app::init` opens
  audio only when not a review run. Keep it that way.
- Quick check: `py -3 tooling/picture_check/full_app_benchmark.py --output build/release/benchmarks/<dir> --place <id> --ms 2000 --repeats 2`;
  full run without `--place`/`--ms`. The GPU throttles: compare only
  before/after pairs run back to back in one session, never across sessions.
  The runner strips `GOLFPP_*` environment variables, so A/B switches must
  not rely on them. Each run copies a ~130 MB `snapshot/`; C: is nearly
  full, so delete quick runs' snapshots once read (never the 2026-11-19
  baseline, `full-app-smoke`, `rtx3050ti`, `after-section-10` or `after-section-2`).
- After each step, record before/after in performance.md and update this plan.

## 1. Investigate the remaining cost (first task)

Done 2026-11-20 by a read-mostly investigation that edited no source beyond
temporary measurement switches, since removed. Numbers are in
[performance.md](../performance.md#remaining-cost-investigation-2026-11-20);
the conclusions are step 2's fix list.

- [x] Profile per-stage CPU in normal solo play on three courses (Ry,
  Odense golfklub, Mollerup): update, render-data build, submission, uploads,
  waits. CPU p95 fails almost everywhere: find what costs over 2.5 ms in a
  solo `play` view with no crowd. Include draw-call submission cost, and
  measure plain `glMultiDrawArrays`/`glMultiDrawElements` (core since GL 1.4,
  so allowed) for terrain, tree and prop runs against separate draws.
- [x] Profile the 40-nearby crowd (`play_40`, `plaza_40`): CPU figure/cart
  rebuild, blob shadows, uploads and GPU draws. Estimate what drawing figures
  and carts as instances of canonical meshes would save (the one open
  first-round box), and what it touches (prop path, marker runs, shadow and
  mirror views).
- [x] Explain the outliers (Odense golfklub play, Mollerup par 3 `shot_rain`,
  Horsens par 3, Himmerland new course p99): repeat them, separate real cost
  from thermal or background noise.
- [x] Break down the zoomed night flag (`zoom_flag_night`, Ry ~17-19 ms) and
  night air scattering (hub lamps ~2.6 ms). Scattering is the picture: look
  for cheaper evaluation of the same result, not removal.
- [x] Attribute observed VRAM: staging, in-flight frames, old-course
  resources across course changes and driver backing stores (the WDDM
  process sample exists; attribution does not). Old-course resources across a
  course change were not measured: nothing scripts a course change.
- [x] Write step 2 as ranked fixes with expected savings against the budgets,
  and say plainly if the budgets look unreachable for some views (e.g. 40
  nearby figures) so the owner can recalibrate them.

## 2. Second recovery round

Ranked by effect on the failing gates and by certainty; the investigation and its numbers are in
[performance.md](../performance.md#remaining-cost-investigation-2026-11-20). Each fix is its own measured
checkpoint: same-session before/after, tests, all-course picture check. Do not change simulation,
protocol, save truth, interest rules or player limits for crowd work. End
with a full all-course run compared against `after-section-10`. Done 2026-11-22
([performance.md](../performance.md#second-recovery-round-result-2026-11-22)).

- [x] **Stop reading GL state back.** `glGetIntegerv(GL_VIEWPORT)` in `grass_renderer::draw` and
  `renderer::render_scene` (the trees' `tree_eye`), `reflection_pass::draw` (draw framebuffer, viewport, front face,
  scissor enable) and `water_renderer::copy_beneath` (framebuffer, viewport) block the app thread until the driver
  has drained its queue. Pass what the renderer already knows (the scene or zoom target's size and id, the mirror's
  crop size, `GL_CCW`, scissor off) as arguments or from one small per-view struct on `renderer`; no globals. Saves
  1.2-1.7 ms of solo submission (Ry play `REN` 2.7-3.0 > 1.0-1.2), 5 ms at the Ry pond, 8 ms at the zoomed night flag;
  frame and GPU time unchanged (the wait moves into `SwapWindow`, which the CPU gate excludes), solo CPU p95
  2.3-2.8 ms against the 2.5 budget (Odense stays over until the cart memo). Verify: Ry/Odense/Mollerup A/B pairs
  as measured; add a `state_readbacks` counter to `frame_accounting.h` (count `glGet*` and `glIsEnabled` in the
  renderer) with a zero gate in the stationary views, like uniform misses; `golfpp-picture-check` unchanged.
  Done 2026-11-20: solo CPU p95 Ry 2.0, Mollerup 3.0 (a noisy repeat), Odense 3.4 (cart memo next), pond and zoom
  fell by 4-7 ms ([performance.md](../performance.md#state-read-backs-removed-2026-11-20)).
- [x] **Remember the cart's ground fit.** `drawn_cart` (`render_gear.cpp`) calls `cart_on_ground`
  (`cart_batch.cpp`: 4 x 9 wheel samples plus the tyre checks, about 70 `terrain_height` calls) every frame, again
  per strapped bag in `find_drawn_cart`. Keep the result per cart in an app-owned memo keyed on position, yaw,
  condition, model and the area's revision, as `blob_ground_memo` does. Saves 0.49 ms a cart a frame: solo `MRD`
  0.55 > about 0.06 (Odense CPU p95 2.8 > about 2.3), and every nearby cart online. Verify: the cart tests plus a
  test that a moved cart misses and a parked one returns the same pose; time `MRD` with 5 and 40 carts through the
  real path.
  Done 2026-11-20: `cart_fit_memo`; solo `MRD` 0.5-0.8 > 0.04 ms, Odense play CPU p95 2.0-2.7 > 1.6-2.2, Ry 2.0-2.3 >
  1.2 ([performance.md](../performance.md#cart-ground-fit-remembered-2026-11-20)). Timed through the real path by the walking
  crowd benchmark: `MRD` 4.0-4.2 ms for 5 moving carts and 15-17 for 40 (a moving cart never hits the memo), 0.04-0.07
  for 5 and 40 parked ones.
- [x] **Make the crowd benchmark honest.** `play_40` and `plaza_40` stood still (50-66 KB uploaded a frame) and added
  their carts after `make_render_data`. Add a walking variant (figures stepping, carts driving) and charge carts
  through `carts_in_world`/`drawn_cart`.
  Done 2026-11-20: `play_40_moving`/`plaza_40_moving` (carts enter `make_render_data` as `scripted_carts`): CPU 25-28 ms
  at Ry/Odense (14 on the practice ground) against 8-10 standing, 12.9 MB uploaded a frame; `MRD` 15-17 ms of it is
  cart fits (0.35-0.7 ms a cart, memo missing) and blob shadows (2.5-2.7)
  ([performance.md](../performance.md#walking-crowd-benchmark-2026-11-20)).
- [x] **Make the ground query cheap.** A `sample_area` cost 5-7 us on a real course, so a moving cart's fit and the blob
  shadows cost 14-18 and 2.5 ms at 40. Done 2026-11-20: three linear scans (mesh cells too coarse, every hole, every
  zone) fixed exactly with a finer mesh index and a `surface_index` on the area; `sample_area` 5.0-7.4 > 1.8-2.1 us,
  `play_40_moving` `MRD` 14-18 > 2-4 ms ([performance.md](../performance.md#ground-queries-indexed-2026-11-20)).
- [x] **Draw the crowd as instances.** Today every figure and cart is rebuilt as vertices each frame (357 k vertices,
  12 MB hashed and uploaded): `append_carts_and_bags` 3.95 ms, `append_remote_avatar` via `build_world_marker_batch`
  1.8, `prop_renderer::stage`'s change hashing 1.0, `world_marker_renderer::stage` 0.3 at Ry. Keep canonical part
  meshes (cart body, wheel, steering wheel, bag; figure limbs, torso, head, ball) uploaded once per course and draw
  per-part matrices and tints as instanced draws; sun shadow map, mirror, zoom and lamp maps read the same instance
  lists. Expected: 8.0-11.8 ms of submission > about 1.5-2.5 (est.), uploads 12 MB > 60 KB, CPU p95 at 40 nearby
  2.3-2.8 against 2.5: borderline, and the only route to it. It touches `prop_frame.cpp`, `prop_batch.*`,
  `prop_renderer.cpp` and `prop.vert` (still the one lit prop path), `world_marker_batch.cpp` and its runs
  (stencil, depth and offset state), `shadow_map_pass.cpp` and `caster_motion.h` (moving casters from matrices),
  `prop_frame::ranges` and `make_reflected_scene` (mirror), `lamp_shadow_pass.cpp`, and the projected placement
  tests in `render_frame_tests.cpp`, which must project instances (matrix times canonical vertex) so a wrong sign
  still fails. Cheaper first step if the walking benchmark shows mostly parked entities: skip rebuilding and
  hashing an entity whose pose, paint and bag are unchanged. The walking benchmark says that step is not worth
  doing alone: every figure and cart changes every frame (a standing crowd already costs only 13-51 KB of uploads),
  so it would pay only for an online crowd's standing share (unmeasured). Instancing removes about 8 of the
  walking crowd's 25-28 ms (props 4.4-6.0, hashing and upload 1.3-2.8, markers 2.0-2.8, marker upload 0.5);
  the other 3.9-17.5 ms was `MRD` ground queries, which instancing does not touch (now 2-4 ms after the ground
  queries were indexed). Verify: walking-crowd A/B, `golfpp-picture-check`,
  pictures identical to the vertex path before it is deleted.
  Done 2026-11-20: submission 8-13 > 1-3 ms, uploads 12.6 MB > 270 KB, standing 40 nearby CPU p95 1.3-2.6 ms
  (11-14 before), walking 40 at 4.6-7.4 ms of which 2.6-4.7 is `MRD`; pictures differ in 0.1% of pixels at most,
  one-pixel silhouette flips from GPU rounding ([performance.md](../performance.md#crowd-drawn-as-instances-2026-11-20)).
- [x] **Do not shade what the eyepiece throws away.** `eyepiece.frag` replaces the 36% of the picture inside its circle
  by the zoom image, but the main pass draws everything and the zoom pass is scissored to the circle's 46% square.
  Mask the circle out of the main pass (depth or stencil reject, one pixel inside the circle's radius), the square's
  corners out of the zoom pass (`eyepiece_pass::begin_zoom`), and skip trees, chunks and grass blocks whose bounds
  lie wholly inside the circle in the main pass (`tree_index` runs, `terrain_renderer`, `grass_visible_blocks`;
  trees are vertex bound, so only skipping them saves their cost). Expected `zoom_flag_night` 12.8 > about 10 ms
  (est.: fragment part 3.1 x 0.36, trees about 1.5, zoom corners 0.3). Verify: the pixels outside the circle and the
  zoom image are bit-identical to before (screenshot diff), the rangefinder tests, Ry zoom A/B.
  Done 2026-11-20: `zoom_flag_night` GPU p50 12.5 > 9.9 ms at matched clocks (4-run mean 11.5 > 9.9), every pixel the
  eyepiece keeps bit-identical (`--eyepiece` check); margin 4 pixels, not 1, and nothing masked with a pond in view
  ([performance.md](../performance.md#eyepiece-throwaway-not-shaded-2026-11-20)).
- [x] **Terrain level of detail for mirrors.** The Ry pond's mirror submits 6.6 M terrain indices (2.2 M triangles)
  into a target of at most 160 x 90 pixels: `reflection_TER` 1.72 of 7.0 ms, `reflection_GRS` 1.0. Build coarser
  index buffers per chunk at course load (`render_mesh_chunks.cpp`) and choose by projected error in pixels as the
  trees' `max_error_pixels` does, for the mirror (and far main chunks); keep the full mesh as the oracle. Expected
  pond 7.0 > about 5.5 ms (est.). Verify: the `--water` check compares mirrors against the full mesh, `--tiles`,
  pond A/B.
  Done 2026-11-20: three levels per chunk, mirrors only; Ry pond GPU p50 7.5 > 6.9 ms, `reflection_TER` 1.8-2.1 >
  1.2; the coarse lists cost 11.5 MiB at Horsens golf klub, paid for by moving `map_color` out of the uploaded
  vertex (253.4 > 227.3 MiB); course load +0.9-1.3 s
  ([performance.md](../performance.md#terrain-levels-for-mirrors-2026-11-20)).
- [x] **Index the tree meshes.** At the zoomed night flag trees are 7.0 of 12.8 ms and flat fragment shading saved
  only 0.2 ms: 5.3 M triangles a frame are drawn as unindexed lists, twice with depth-first. Index the models
  (`tree_renderer.cpp` meshes, `glDrawElementsInstanced`, GL 3.3) so a leaf card shades 4 vertices, not 6.
  Expected -25-35% of the trees' time (est.: 1.5-2.5 ms zoomed, 0.2-0.3 at play). Verify: `--tree-images` and the
  tree checks give identical pictures; Ry zoom and play A/B.
  Done 2026-11-21: tree stage at the zoomed night flag 2.65 > 1.86 ms (GPU p50 10.0 > 8.5), play 0.93 > 0.78; pictures
  bit-identical, ledger -1.1 MB at Ry ([performance.md](../performance.md#tree-meshes-indexed-2026-11-21)).
- [x] **Cull night lights per view.** All 16 light slots are uploaded to every view, so every fragment loops over
  16 lights in `scattered_light` (2.3 ms at the zoomed night flag, 1.3 at the hub's lamps) and `local_light`
  (0.5). A light whose reach sphere misses the view's frustum out to its far plane adds nothing to any pixel, so
  `lighting_uniforms::upload` can drop it per view (main, zoom, each mirror); the zoom's narrow frustum should drop
  many. Exact. Expected 0.3-1.0 ms (est.; count lights per view first). If it falls short, scattering per pixel from
  the depth in `sun_shaft_pass` is exact for opaque pixels but nets only 0.4-1.0 ms (one pass costs about 0.9 ms
  against the 1.3-2.3 saved), so do not start there.
  Done 2026-11-21, not adopted: 8-9 of the nearest 16 lights are in each view, but a culled light was already cheap
  (`beam_run` exits at once), so zoom_flag_night 7.80 > 7.77 ms and hub lamps 4.99 > 4.98 (ABBA means); code removed
  ([performance.md](../performance.md#night-lights-culled-per-view-not-adopted-2026-11-21)).
- [x] **Pack the terrain vertex.** `render_terrain_vertex` is 64 bytes (`map_color` moved out with the terrain
  levels); pack normal, colour and the five 0-1 floats into bytes, about 32 bytes in all (about -57 MB at Ry, lighter
  vertex fetch; est.). `describe_render_terrain_vertex`, `terrain.vert`, the flight path buffer (same struct),
  `ground_truth.vert` follows. Headroom, not a failing gate (the 256 MiB cap passes,
  Horsens golf klub at 227). Verify: the picture check's ground checks, ledger totals.
  Done 2026-11-22: 64 > 32 bytes, ledger Horsens golf klub 226.5 > 159.5 and Ry 198.4 > 144.1 MiB, turf pictures within 4/255,
  pond GPU -17% ([performance.md](../performance.md#terrain-vertex-packed-2026-11-22)).
- [ ] **Fix the gates, not the renderer.** (a) [x] The solo upload gate counts every phase: steady frames upload 16-66
  KB (Ry walk at dusk 260 KB, 4 KB over: trim the weather streak or trail upload), but a view's first frame uploads
  0.4-2.4 MB, and 13.8 MB with the crowd, which fails 226 view-repeats; gate the steady phase and report first-frame
  spikes separately. (b) [x] Report `SWP` beside `CPU` so waits stay visible. (c) [ ] Decide the budgets below (the owner's decision).
  Done 2026-11-22 for (a) and (b): `benchmark_report.py` gates `dynamic_bytes` of the steady phase and writes
  `first_frame_dynamic_bytes_max` (168 views exceed 256 KiB on their first frame, none steady) and `cpu_SWP_ms_*`,
  `swp_p95`/`swp_p99`; tested in `test_benchmark_report.py`. The Ry walk at dusk's 259,848 bytes is under 256 KiB
  (262,144), not 4 KB over, so no weather or trail trim was needed (the weather streak buffers are uploaded once).

- [ ] **Bare crowns no costlier than leafy ones.** Every card texel of a bare crown evaluates the twig spray
  (`tree_leaf.glsl`): winter tree stage up to +5% in play at Ry, +10-20% at Mollerup and +15-20% in the zoomed night flag
  ([performance.md](../performance.md#bare-trees-grown-branches-twig-cards-falling-leaves-2026-11-22)); with the
  spray off it is below summer. Per-card constants in the vertex shaders cost more (the trees are vertex bound).
  Next to try: fewer strokes a spray with more sprays' worth of haze, or a cheaper reject per side twig.
  Verify: `--trees` stays green and winter play <= summer at Mollerup.

**Proposal awaiting the owner, with 2026-11-22 numbers; no budget has been changed.** Measured in `after-section-2`
(a thermally throttled session, so GPU figures are pessimistic by roughly 1.5x against `after-section-10`): solo CPU
p95 2.5 ms is met by 295 of 333 views, so keep it for solo play (`play` worst 2.13); the 40-figure walking crowd is
4.4-18 ms, the zoomed night flag 1.9-10.8 and `shot_rain` up to 5.5: budget those as 8 ms and
10 ms, or stress views. Solo upload 256 KiB is met in every steady frame (largest 203 KB): keep. Memory budgets are
met with room (740 MB of 768, 160 of 256 MiB): keep. GPU p95 3 ms and frame p95 5 ms are met by no view in this
run (`play` GPU p50 4.9 ms throttled, 4.2 cool): the earlier text below stands, now with CPU no longer the
obstacle. Re-measure GPU cool before the owner decides.

Budgets that look unreachable, for the owner to recalibrate (this machine, 1080p window, shipped look; the
fixed TV, tape and camcorder cost is 0.45 ms of every frame): **GPU p95 3 ms** in solo play is already 3.6-3.9 ms at
Ry, Odense and Mollerup (p95 4.3) with grass 1.1-1.2 and trees 0.9, and only the par-3 courses are near 2-3: it needs
about 20% off grass or trees, a look decision; 4 ms p95 and 5 ms p99 is what these fixes can deliver. The **pond**
(7.0 ms, mirror 3.0) lands near 5.5-6 and the **zoomed night flag** (12.8 ms) near 7-8 after the eyepiece, trees and
light fixes: budget them separately (about 6 and 9 ms) or treat the rangefinder at night as a stress view. **CPU p95
2.5 ms** is reachable for solo play after the first two fixes and for 40 nearby only with instancing (borderline).
**Frame p95 5 / p99 8 ms** follows the GPU: reachable in solo play, not at the pond or the zoomed night flag.
**Solo dynamic upload 256 KiB** is reachable once the gate ignores first frames. Low-end is unmeasured: a driver
costing 2-5 us a draw would turn Ry's 700 to Odense's 1,800 draws into an estimated 1.4-9 ms of submission, so
hole signs (76-450 single draws) and tree image buckets (309-899) matter only then; `glMultiDrawElements` is
rejected (2 ms for 114 ranges here).

## 3. Picture sharpness and optional resolution presets

Owner decision (2026-11-22): build the presets now without timing them;
step 2's numbers suffice.
Preserve the camcorder, tape and TV treatment at every resolution. The
[picture sizing](../../src/renderer/picture_size.cpp) picks an integer
enlargement from window height and `target_lines` (3440 x 1440 at 360 lines
is 860 x 360, enlarged fourfold).

- [x] (360/720 chosen, table in performance.md; 480 and 720 collided at 1080 rows; 1080 dropped by the owner) Compare 360, 480 and 720 lines on both displays, fullscreen ultrawide
  and a smaller window; record scene sizes and enlargement factors; avoid
  presets that resolve identically. 360 stays the performance reference.
- [x] (setting `scene_resolution`, "PICTURE DETAIL", settings file, no save change; rule updated) If supported, add a scene-resolution setting through the existing
  settings path and update the "line count is art direction, never a
  setting" rule in the rendering rules with it.
- [x] (HUD, text, gain grain, TV beam in UI pixels; tape/bloom/smear/halation in picture height; captures in build/test/preset_review) Keep square pixels, even scaling and the full pipeline; check grain,
  smear, bloom, tape, HUD and text; effect sizes must not change silently.
- [x] (content, settings, startup tests; picture-check --presets) Test resizing, preset changes, target recreation and persistence
  (bump the save version if its shape changes).

## 4. Verification and sign-off

- [ ] Standard configure/build/test, then all-course GPU checks (tiles,
  crops, caches, tree images, turf, resolutions, reflections). Wrong signs,
  coordinates and source identities must fail.
- [ ] Full app matrix on the gaming reference; low-end labelled estimated.
  Publish full-frame tails, stage costs, RAM/load peaks and uploads in
  performance.md, without subtraction deltas or serialized-render FPS claims.
- [ ] Record accepted turf and preset choices (and the line-count rule
  change) in the rendering rules; update headers, code map and development
  commands; remove this plan.

## Research

- [Khronos buffer streaming](https://wikis.khronos.org/opengl/Buffer_Object_Streaming),
  [query objects](https://wikis.khronos.org/opengl/Query_Object).
- [NVIDIA: procedural filtering](https://developer.nvidia.com/gpugems/gpugems/part-iv-image-processing/chapter-25-fast-filter-width-estimates-texture-maps).
- [Valve: Advanced VR Rendering](https://media.gdcvault.com/gdc2015/presentations/Vlachos_Alex_Advanced_VR_Rendering_V2.pdf):
  normal-map filtering and specular aliasing.
