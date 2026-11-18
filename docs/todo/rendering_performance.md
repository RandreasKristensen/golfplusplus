# Rendering performance, turf and picture

Owner priority (2026-11-18): broad low-end support comes first. Reaching
60 FPS on the gaming laptop is insufficient; it must have substantial
headroom. Preserve the fixed world and real reflections while reducing the
work needed to draw them. Fairways and greens should have stripe-free, fine,
fluffy texture; greens should read as more closely cut than fairways. Keep the
rough's character while diagnosing its apparent movement. Compare optional
picture resolutions and shell turf without committing to their adoption.
This plan's performance targets and visual prototypes have not yet been achieved.
Remove the plan when its implementation and evidence are complete.

Read the [rendering rules](../rendering_rules.md),
[content rules](../content_rules.md),
[development procedures](../development.md), and
[measured baseline](../performance.md#current-renderer-benchmark) first.
Work in the existing main tree and preserve unrelated changes. No new
dependency or graphics backend is proposed. Keep OpenGL 3.3 / GLSL 330 and
the existing macOS path: no requirement for compute, bindless textures,
persistent mapping, base-instance draws, or indirect multidraw.

## 1. Evidence and acceptance targets

Fixed positions are useful for caching. The expensive decisions are full
geometry at every distance, repeated scene work for water, and an entire
half-metre field retained as float triples. A cheaper representation is
allowed when it depicts the same world and passes projected comparisons.
Do not restore different sparse trees, camera-dependent grass seeds,
screen reflections, or a sky replacement for a real reflection.

The release baseline covers 18 courses / 51 views at a 1920 x 1080 window,
640 x 360 scene. Serialized rendering p95 reached 53.82 ms in Hedensted's
sweep and 47.51 ms beside Mollerup's pond; 32 of 51 views exceeded a
16.67 ms rendering interval. These are not whole-game FPS or an old/new
percentage. Thermal limiting was observed. See the baseline's limitations.

Use these **initial engineering budgets**, then calibrate against real
low-end hardware. These are work targets, not a newly claimed minimum spec:

| Measurement | Target |
|---|---|
| Gaming reference, complete uncapped app loop | p95 <= 5 ms, p99 <= 8 ms on every course |
| Gaming reference, CPU update + preparation + submission | p95 <= 2.5 ms; expose waits separately |
| Gaming reference, complete GPU frame | p95 <= 3 ms, p99 <= 4 ms, asynchronous measurement |
| Verified older integrated GPU / CPU | sustained 60 FPS; complete app p99 <= 16.67 ms at shipped settings |
| Grass resident resources | <= 8 MiB GPU, <= 16 MiB decoded CPU tiles, <= 64 MiB total grass RAM including indexing/staging |
| Impostor cache, if needed | <= 24 MiB including every channel/view/mip |
| Whole game, initial recovery goal | <= 768 MiB steady working set, <= 1 GiB load peak, <= 256 MiB logical GPU allocations |
| Warm stationary solo view | zero static instance/mesh uploads, uniform misses, and buffer capacity growth |
| Solo dynamic uploads across all passes | <= 256 KiB/frame initially; upload each payload once and reuse it |

Frame-time targets apply to shipped picture/content settings. Accepted
developer tuning extremes must remain correct and memory-bounded; they are
separate stress results, not a hidden promise of the same timing budget.
The gaming reference is Ryzen 7 5800HS / RTX 3050 Ti Laptop. The low-end
result must come from an actual older device and driver, not a lowered
window resolution on this GPU or an assumed vendor preference. The AMD
power-saving probe still created an NVIDIA context: integrated-GPU
performance is **unmeasured**. Retain the existing 1 ms effects guard, but
it cannot replace full-frame and memory gates.

## 2. Work order

1. Establish reliable full-app measurement and diagnose rough movement
   at the shipped 360-line reference (sections 3-4).
2. Recover static batching, reflection and grass storage/selection costs
   (sections 5-7). Integrate a demonstrated rough fix into the grass checkpoint.
3. Implement the stripe-free material baseline (section 8) as a separate
   measured change. It can land once the benchmark exists if it adds no
   material workload; it need not wait for every optimization or for shells.
4. Evaluate distant-tree representations if still required, then recover
   remaining terrain, lighting, shadow and crowd costs (sections 9-10).
5. With recovered baseline headroom, compare optional resolution presets
   and shell turf independently (sections 11-12). Ship only combinations
   with measured benefit and explicit hardware/timing acceptance.
6. Complete visual, full-frame, memory and actual low-end sign-off (section 13).

Move grass memory work earlier if allocation pressure is the immediate
problem. Candidate-resolution captures may inform the rough diagnosis early,
but do not change the default or combine resolution and grass changes in a
benchmark result. Keep the existing line-count rule until an accepted preset
design is implemented; automatic main-resolution cuts are not a proposed fix.

Preserving the picture means preserving accepted world identity and appearance.
It does not freeze a reproduced rough defect or require keeping the stripes
the owner wants removed. Record accepted appearance changes separately, then
use that reference for optimization comparisons. Cache geometry/material data
without making mowing inputs permanent.

Use one measurement protocol and the budgets in section 1 for every phase,
including reflected views. A higher-resolution option does not replace the
fast baseline. Optional presets need explicit hardware/timing acceptance;
only configurations tested on an older device carry its low-end claim.
Shell turf has no separate extra 1 ms allowance on the gaming laptop.

Keep checkpoints independently reviewable and record before/after results.
Diagnostic component removal belongs in tooling, not player settings.
Never weaken a visual test to meet a timing target.

## 3. Reliable benchmark and accounting

Files: tooling/picture_check/profile_check.cpp, picture_check_main.cpp,
check_support.cpp; src/profiling/profiling.h/.cpp;
src/renderer/gl_timer.h/.cpp; src/core/startup_options.h/.cpp and frame loop.

- [ ] Retain the current serialized probe, clearly named in output. Add a
  full-app benchmark using normal update, render-data, render, and present
  paths. Put the scripted driver/results writer in a separate component;
  app.cpp is already at its size limit.
- [ ] Remove per-frame glFinish from throughput mode. Capture CPU update,
  preparation, submission, swap, and complete-frame times. Use delayed GPU
  start/end timestamp pairs around the stage queries; do not nest
  GL_TIME_ELAPSED queries.
- [ ] Accumulate repeated stage segments. gl_timer_pool currently has one
  query per stage/slot: a second draw_props can replace the first result.
  Keep REF inclusive and its child measurements separately tagged. Do not
  add inclusive and child times as a frame total.
- [ ] Mark query reads, uploads, swaps, and other synchronization separately.
  Current submission time includes waits; it is not pure CPU computation.
- [ ] Count per-pass tree instances/triangles, grass instances, terrain
  indices before/after range merging, water candidates, actual captures,
  reflection pixels, texture allocations, and cache uploads.
- [ ] Measure load/bake time, peak/steady CPU memory, logical GPU allocations
  and observed VRAM separately. Count staging, mipmaps, in-flight frames,
  old-course resources and backing stores. RGB32F payload is not proof of
  the driver's exact VRAM allocation.
- [ ] Warm every camera direction and shader variant, then use timed warmup
  and at least 10 measured seconds per view. Save raw frames, machine/driver,
  power, thermals/clocks, source/content identity, and parameters. Preserve
  a matching executable and assets for a trustworthy before/after baseline.
- [ ] Repeat at least three times with reversed/interleaved component order.
  Marienlyst has no ponds: full versus no_water is a negative control that
  must agree within measured noise before water deltas are claimed.
  Reject competing game, golf++ instance, and build workloads.
- [ ] Add the shipped hub using start_hub in src/game/hub_session.h. Cover its
  plaza, range, bus and night lamps, not just course ids.
- [ ] Cover all 18 courses: play, visible pond, full sweep, walking over tile
  boundaries, zoom, flags, a moving shot, wind/rain/dusk/true winter night.
  Stress 0/5/40 nearby figures, balls and carts with controlled data;
  this does not change online rules or player limits.
- [ ] Report p50/p95/p99/max and slow-frame counts. Keep a first-turn/cold-cache
  traversal and a steady traversal. Tool "all passed" currently means it ran,
  not that performance targets passed.
- [ ] Assert stable operation/memory limits in tests. Timing gates run on
  named hardware under known conditions; noisy CI does not redefine them.
  Update profiling/startup tests and the picture-check README.

Research: [Khronos query objects](https://wikis.khronos.org/opengl/Query_Object)
and [synchronization](https://wikis.khronos.org/opengl/Synchronization)
describe unavailable-result and resource-update waits. This is why the
current serialized results and GPU-stage averages need separate accounting.

## 4. Diagnose rough movement

In [grass_blade.vert](../../assets/picture/shaders/grass_blade.vert), roots are
world-fixed, but blade width and thinning depend on eye distance. Wind also
bends and flutters blades. Subpixel geometry can shimmer in the low-resolution
picture. These are hypotheses for the reported effect, not a reproduced diagnosis.

- [ ] Reproduce at a fixed rough patch: stand still, turn in place, walk towards
  and across it, and cross density boundaries. Repeat with frozen world time and
  zero wind, then with wind restored, at the candidate picture resolutions.
- [ ] Compare the scene before picture processing with the final TV output.
  Separate actual geometry/coverage changes from aliasing and moving tape grain.
- [ ] Inspect distance widening, thinning and representative selection. Preserve
  roots and the same field in every view; fix the demonstrated cause rather than
  treating fewer tufts or lower contrast as the default solution.
- [ ] Extend the existing rendered walking and density-boundary checks to catch
  the reproduced defect. Root identity alone does not establish stable shape,
  coverage or shading; include projected appearance and motion sequences.
- [ ] Extend these appearance checks through section 7's cache transitions:
  eviction, reload, tile corners, zoom and reflected views must reproduce
  the same accepted rough. Intentional wind motion remains part of the world.

## 5. Cache static work and fix dynamic streaming

Files: src/renderer/render_tree.h/.cpp, tree_renderer.h/.cpp,
shadow_casters.h/.cpp, shadow_map_pass.h/.cpp, prop_renderer.h/.cpp,
dynamic_buffer.h/.cpp, shader.h; src/core/app_scene.cpp.

- [ ] Build tree offset/scale/yaw/tint/variant and conservative animated bounds
  once per course revision. Spatially index immutable ranges per species and
  variant, reusing the existing identity/value functions.
- [ ] Select those ranges for main/light/cropped-mirror frustums. Remove
  repeated hashes and std::map/vector bucket construction from each view
  in build_tree_batch. Keep scratch capacity in app/renderer-owned objects.
- [ ] Upload static instances once. GL 3.3 attribute-buffer offsets support
  range draws without base-instance APIs. Merge ranges only when saved
  calls justify the extra submitted geometry.
- [ ] Draw static props, signs and fence casters from persistent GPU buffers.
  build_shadow_casters currently walks/copies static triangles and appends
  static props to streamed batches. Split these from moving figures/carts,
  wind flags, bus doors and handled gear.
- [ ] Preserve the common lit prop/material path. Static caches must not bake
  the current sun, haze or night glow into permanent vertex data. Preserve
  source ordering where coplanar surfaces depend on it.
- [ ] Upload each dynamic payload once per frame and reuse its range in every
  scene/shadow view. For the remaining streaming, compare whole-buffer
  orphaning with explicit double/triple buffering. Never overwrite data
  still in use by the GPU.
- [ ] If orphaning wins, count intentional storage orphans separately from
  capacity growth and bound in-flight storage. Do not claim zero allocations
  by renaming a counter. Unsynchronized mapping needs proven non-overlap and
  fence ownership, not merely an UNSYNCHRONIZED flag.
- [ ] Fix uniform-cache coverage: backdrop_pass sets 35 names while
  uniform_location_cache holds 32. Warm views currently repeat lookups.
  Include array/optimized-out names, clear on relink, and assert zero warmed
  misses in main and mirrored views.
- [ ] Exercise new course, palette reload, foliage/looks changes and repeated
  entry. Clock/wind remains a uniform; it must not rebuild static instances.

Verify cached selections against the current brute-force value functions
in tests, including negative coordinates, large crowns and maximum wind.
Project picture/caster silhouettes through their respective cameras.
Keep an oracle in tests, not two production selection paths. Measure CPU
cost, upload reduction, draw counts and actual GPU cost together.

Research: [Khronos buffer streaming](https://wikis.khronos.org/opengl/Buffer_Object_Streaming)
explains in-use storage stalls and the orphaning/multiple-buffer approaches.
Use a method that wins on the older integrated device as well as this
laptop; persistent mapping is not required.

## 6. Make real reflections cover useful work

Files: src/renderer/water_batch.h/.cpp, water_renderer.h/.cpp,
water_reflection.h/.cpp, reflection_pass.h/.cpp, renderer_scene.cpp,
gl_loader.h; assets/picture/shaders/water.frag.

- [ ] Retain exact water-level ranges, bounds and spatial metadata at load.
  Reuse them for candidates and surfaces instead of scanning all triangles
  twice per view. Group exactly equal physical levels, never epsilon-merge
  ponds at different heights.
- [ ] Compute conservative coverage rectangles from homogeneous clipped
  water triangles. Handle the eye/near-plane crossing and underwater branch;
  projecting only original triangle corners is unsafe.
- [ ] Expand coverage for resolved-wave lookup distortion, unresolved-slope
  blur, filtering and sampling guards. Derive the bound from shader/data.
  Fall back to a full real capture when a finite crop cannot be proven.
- [ ] Apply a crop matrix to the mirrored projection and pass its tighter
  frustum to tree/terrain/prop selection. Scissor alone reduces fragments,
  not vertex processing.
- [ ] Size captures from expanded covered pixels, initially retaining the
  current quarter-scale sampling density. Store the complete cropped
  world-to-texture transform and change water lookup with it. Crop the
  unoblique sky projection too; retain the water clip plane for geometry.
- [ ] Reuse static caches. Preserve real off-screen objects, my figure/gear,
  flags, shared light and two-leg mist. Do not reuse stale pictures after
  camera/world/clock/weather changes. Keep water recursion disabled.
- [ ] Replace CPU-blocking GL_QUERY_RESULT reads with a measured conservative
  path. Prototype current-frame GPU conditional rendering after the water
  query, using GL_QUERY_NO_WAIT. Pending results draw the candidate;
  earlier-frame visibility may not suppress newly visible water.
- [ ] Load entry points explicitly. Start the GL 3.3 conditional prototype
  with GL_SAMPLES_PASSED, a target accepted by that specification, rather
  than assuming the current ANY_SAMPLES query works on every required
  driver. Do not use BY_REGION: main and cropped-mirror coordinates differ.
- [ ] Compare this with drawing all cropped current candidates. NO_WAIT can
  spend extra GPU work and still submits CPU commands. Retain the lowest
  total cost. CPU polling, if retained, checks availability and draws on
  uncertainty; it neither waits nor treats unavailable as invisible.
- [ ] Pool targets with bounded growth. Count all visible levels and turning
  churn. Do not silently cap captures, substitute a sky, merge different
  planes, or share images whose covered rays differ.

Verify against an uncropped reference at calm/maximum wind, corners,
grazing angles, fast turns and multiple heights. Include objects outside
the main view but inside reflected rays. Exercise pending/visible/hidden
queries and newly exposed water; verify no stale texture, correct winding,
and framebuffer/viewport/scissor restoration. Assert clip-plane signs and
projected composition. Extend water_scene_check; keep all Fresnel/glint
and all-course rendered checks passing.

Research: [GPU Gems 2, chapter 19](https://developer.nvidia.com/gpugems/gpugems2/part-ii-shading-lighting-and-shadows/chapter-19-generic-refraction-simulation)
describes the mirrored, clipped capture. Cropping its rays is our optimization
of that same scene. [OpenGL 3.0 section 2.14](https://registry.khronos.org/OpenGL/specs/gl/glspec30.pdf)
permits conservative NO_WAIT conditional execution; clear/draw operations
can be conditional. This is not the newer GL_QUERY_RESULT_NO_WAIT API.

## 7. Bound grass storage and selection

Files: src/renderer/grass_field.h/.cpp, grass_bake.cpp,
grass_renderer.h/.cpp, grass_blade.vert; src/core/app_scene.cpp.
Keep global finest-cell hashes, nested representatives and fixed clearance.
Residency chooses which immutable samples are stored, not different roots.

- [ ] Replace monolithic GPU RGB32F storage with fixed world tiles, initially
  32 m / 64 cells per side plus required nodes/interpolation guards. Use one
  canonical global sample lattice.
- [ ] Precompute tile min/max height, maximum clearance and eligible-grass
  metadata. Query it directly for grass-block selection instead of scanning
  every terrain/overlay chunk for each ring/view. Skip only provably empty
  or out-of-view blocks.
- [ ] Use a bounded texture-array/atlas cache for the union of main, zoom and
  reflected requests plus guards. Derive capacity from maximum accepted
  tuning and real GL limits, not just default 88 m reach.
- [ ] Upload only residency misses. Neighboring tiles share identical border
  samples and node-centre conventions. Prevent filtering across atlas tiles
  and stale slot/table mappings after eviction. Set and restore pixel-unpack
  alignment/stride explicitly; test odd-width rows in each channel format.
- [ ] Split precision by meaning: initial resident candidates are R32F height,
  R16 normalized clearance rounded down, and a filterable exact 0/1/2 kind
  encoding. Investigate R8_SNORM kind-minus-one and verify GL 3.3 sampling.
- [ ] Preserve conservative clearance through quantization and interpolation.
  Add a proven arithmetic bound to the existing 0.354 m allowance as needed.
  Never round clearance upward and permit blades on bare ground.
- [ ] Bound CPU backing too. Measure packed tiles; if they exceed 64 MiB,
  retain immutable tiles in a disposable course backing/cache file and a
  bounded decoded/prefetch set. Build the logical field once per load;
  do not rerasterize when the eye crosses a cache window.
- [ ] A candidate packed height uses a fixed global step with per-tile integer
  bases, so shared border nodes decode identically. Start with <= 1 mm error;
  use float tiles where the bound cannot hold. Independent per-tile min/max
  quantizers must not create seams. Keep full precision in the test oracle.
- [ ] Bake tile-by-tile into backing storage without first allocating the full
  dense float field. Preserve canonical triangle order for height ties.
  Reuse the exact bare-edge index and existing zone/path/tee/terrain data;
  do not introduce another ground or collision rule.
- [ ] Key disposable cache data by content and bake/format inputs. Rebuild stale
  or corrupt caches; do not migrate saves for them. Use filesystem error
  results and define a bounded I/O-failure fallback. Flag any compressor
  dependency before introducing it; no compressor is currently proposed.
- [ ] Bound prefetch work/staging and upload safely before tiles are required.
  Warm the initial/teleported visible set before showing it; never show
  missing or wrong grass while it catches up. Texture sub-updates may also
  synchronize, so test destination rotation/staging on the low-end driver.
- [ ] If extreme tuning requests more tiles than fit simultaneously, use a
  correct bounded tile-batch draw/upload path rather than exceeding the
  cache or dropping samples. Report its cost separately from shipped settings.
- [ ] Meet both RAM and GPU budgets on every course and accepted style.
  Moving a 471 MiB allocation from GPU to CPU does not complete this phase.

Verify exact bare-distance exclusions, sub-cell slivers, negative indices,
ponds, tees, course borders, packed error bounds, every tile corner and
bilinear tap, eviction/reload order, long walks, turns, zoom and mirrors.
Retained roots must match across residency changes. Keep fairway/ring views
and projected coverage tests. Assert bounded allocation/upload work and no
hot-frame full-course texture upload.

Research: [GPU Gems 2 geometry clipmaps](https://developer.nvidia.com/gpugems/gpugems2/part-i-geometric-complexity/chapter-2-terrain-rendering-using-gpu-based-geometry)
demonstrates bounded windows and incremental updates over a larger dataset.
Use that storage principle, not camera-dependent resampling/synthesis.
[Khronos array textures](https://wikis.khronos.org/opengl/Array_Texture)
and the [GL 3.3 specification](https://registry.khronos.org/OpenGL/specs/gl/glspec33.core.pdf)
supply the compatible storage baseline; query limits and count every layer.

## 8. Stripe-free short-turf material baseline

Files: src/renderer/terrain_render_mesh.h/.cpp and cached terrain/material
batches; assets/picture/shaders/terrain.frag; picture data and generated turf
textures. Reuse existing material identities, exact zone coverage and the
common light/shadow/haze path. This is a production candidate in its own right,
not a placeholder that commits us to shells.

- [ ] Make a stripe-free material baseline: consistent base colour, subtle
  code-generated fibre/tuft texture and restrained normal detail. Use this as
  the reference for any later shell comparison and its incremental GPU cost.
  Evaluate this as the first production candidate; adopt shells only if their
  demonstrated benefit fits the shared budgets.
- [ ] Distinguish closely cut greens from fairways with restrained texture scale
  and material controls. Keep both calmer than rough and avoid broad cloudy
  mottling. Draw art by code; add no external images or new dependency.
- [ ] Filter fibre colour and normal detail at the pixel footprint, preserving
  unresolved roughness. Keep texture positions fixed in the world. Reuse
  material coverage rather than creating a new dense field for each turf type.
- [ ] Compare standing, address and crouched views, slopes, zone edges, paths,
  ponds and cups under noon, low sun, overcast, wet and night conditions.
  Check the ball touching/rolling over the turf and into the cup. No decorative
  treatment changes the gameplay surface or obscures the ball/cup.
- [ ] Capture scene and final-TV motion at the reference picture, including
  reflected turf. Measure complete-frame and memory cost separately from the
  architecture changes. Present the matched material baseline for acceptance.
- [ ] When the replacement ships, remove affected mowing/stripe fields, shader
  inputs and tuning keys instead of retaining a dead stripe path. Update the
  relevant content/shader tests and use the accepted look as the new reference.

## 9. Far trees from the same source, if still needed

Files: tree_model.h/.cpp, tree_renderer.h/.cpp, render_tree.h/.cpp,
tree.vert/.frag, tree_pose.glsl, tree_leaf.glsl, shadow_map.vert/.frag,
and tooling/picture_check/tree_check.cpp. Start after static caching and
reflection cropping, to address remaining cost rather than duplicate work.

- [ ] Profile full geometry on the densest main/reflected views and integrated
  hardware. Separate sun/lamp costs. If it meets every budget after earlier
  phases, keep it and record that evidence; do not add an unused renderer.
- [ ] If it misses, bake each species/variant's own full model for the active
  course season. Capture unlit material/albedo, coverage, normal/depth and
  required leaf attributes; relight with the shared runtime path. A noon
  photograph must not stay bright at night.
- [ ] Start with a small view grid, e.g. 8 azimuths x 2 elevations, 64-square
  tiles. Seven looks x three variants is 21 models. Compute all channels and
  mips and stay within 24 MiB. These are prototype values, not tuning to
  freeze before testing. Cover low/elevated mirrored and zoomed views.
- [ ] Validate angular interpolation and depth correction against the source.
  A single camera-facing photograph does not establish invariance. Preserve
  base, scale, yaw, tint, crown extent and leaf holes.
- [ ] Select by projected error, initially <= 0.5 scene pixel plus the existing
  coverage gate. Use each pass's actual projection/viewport; zoom, mirror
  and shadow footprints differ. Do not restore an untested 70 m switch.
- [ ] Preserve clock/wind pose. Warp/reconstruct captures with the shared pose
  where image comparisons pass; otherwise keep source geometry until the
  animation error is subpixel. Do not freeze crowns. Include look/foliage
  changes in cache identity and bound old-cache replacement lifetime.
- [ ] Switch only where representations already meet the image bound; add
  hysteresis if needed. Any transition must prove coverage/lighting.
  Naive translucent double-draws can thin/darken crowns; screen-random or
  per-frame dither can flicker on the tape.
- [ ] Keep full shadow casters initially if their cost fits. If shadows also
  need an impostor, derive it from the same model in the light direction
  with correct depth and independent sun/lamp comparisons. Do not cast the
  camera-facing billboard as if it were the physical tree.
- [ ] Compare all species/variants, seasons, threshold/sector boundaries,
  maximum wind/time, grazing reflections, night light and zoom. Test
  silhouette, coverage, depth and luminance. Two images around 70 m cannot
  validate the new representation by themselves.

Research: [GPU Gems 3, True Impostors](https://developer.nvidia.com/gpugems/gpugems3/part-iv-image-effects/chapter-21-true-impostors)
explains angle limits and depth-based representations. This plan does not
require its volume ray marcher; fragment cost must not exceed the small
geometry it replaces. [Guerrero's forest rendering report](https://www.cg.tuwien.ac.at/research/publications/2006/G_P_06_RFS/G_P_06_RFS-Report.pdf)
examines spatial batching, impostors and transition artifacts. Historic
timings from either source are not predictions for this game.

## 10. Remaining performance recovery

- [ ] Revisit terrain_renderer's four ground / two overlay draw-range limits.
  Merging spans can resubmit much culled geometry inside a narrow mirror.
  Compare extra draws with indices saved. Preserve overlay ordering rather
  than blindly sorting coplanar triangles spatially.
- [ ] Share spatial caches with sun/lamp selection. A wind change invalidates
  a cached shadow image, not static geometry buffers. Measure windy night
  lamps; noon's zero LMAP does not cover night performance.
- [ ] Measure remaining lighting, leaf overdraw, texture bandwidth and TV cost
  on older hardware. Keep the common lit path and scene -> camcorder ->
  tape -> overlay -> TV pipeline. Keep the shipped 360-line performance
  reference while section 11 evaluates optional scene-resolution presets.
  Any selected preset gets its own measured dimensions and full-pipeline gate;
  do not introduce automatic main-resolution cuts to hide renderer costs.
- [ ] Check crowd scaling after static uploads are removed. Reuse canonical
  meshes/instances for dynamic models where beneficial. Do not change game
  simulation, protocol/save truth, interest rules or player limits.

- [ ] Demonstrate recovered headroom with the accepted material baseline before
  production shell turf or higher-resolution defaults are selected. Keep the
  baseline's low-end hardware gate explicit; prototype captures can proceed
  without claiming low-end sign-off on an untested device.

## 11. Picture sharpness and optional resolution presets

The reference performance recovery remains at 360 lines. This phase selects
optional picture quality after baseline headroom is demonstrated; exploratory
captures for diagnosis may be taken earlier. Preserve the 1989 camcorder,
tape and TV treatment at every accepted resolution.

The current [picture sizing](../../src/renderer/picture_size.cpp) chooses an
integer enlargement factor from window height and `target_lines`. At 3440 x
1440, the shipped 360-line target produces an 860 x 360 scene enlarged fourfold.
The target is approximate at other window heights, rather than an exact render
height. Monitor size and viewing distance also affect perceived blockiness.

- [ ] Compare candidate targets of 360, 480 and 720 lines on both displays,
  including fullscreen ultrawide and a smaller window. Record actual scene
  dimensions and enlargement factors; avoid presets that resolve identically.
  Treat these as controlled comparisons initially. Retain 360 as the performance
  reference while recovery is measured; higher targets increase the work of
  scene effects and reflected views as well as the visible scene.
- [ ] Choose the default, preset names and persistence through the existing
  settings path. Implement a scene-resolution setting if the comparisons support
  it; update the current "line count is art direction, never a setting" rule in
  [rendering rules](../rendering_rules.md) alongside that change.
- [ ] Keep square pixels, even line scaling and the full picture pipeline.
  Check grain, smear, bloom, tape effects, HUD and text at each preset. Increased
  scene resolution must not silently change effect sizes in the final picture.
- [ ] Test resizing and preset changes, target recreation and setting persistence;
  follow save-version rules if the saved shape changes. Measure resolution cost
  separately from grass changes. Keep a separate reference for each accepted
  preset; the first turf baseline remains at the shipped reference resolution.

## 12. Compare shell turf and choose the final picture

This is a measured prototype and selection phase, not a commitment to add
layers. Use the accepted material baseline from section 8. Derive any
incremental allowance from remaining whole-frame headroom, including all
scene passes, uploads, storage and the selected resolution. A prototype that
misses those gates remains an experiment even if its own pass looks cheap.
The shipped baseline must meet the low-end targets; optional higher presets
need explicit hardware/timing acceptance and the same bounded-memory and
correctness checks.

Shells are repeated shallow layers above the existing turf, with world-fixed
density patterns forming a short grass volume. They offer actual depth, but can
show their layers at grazing angles and repeatedly process the same screen area.

- [ ] Prototype shallow shells for fairway and green surfaces, with separate
  height and texture controls in picture data. Reuse course geometry and instance
  layers; introduce no per-blade game entities or new dependencies. Use the
  cached batches and exact material coverage from sections 5-7: do not
  duplicate full-course geometry for each layer or allocate another dense
  half-metre field for each turf material. Rough's bare-ground clearance is not
  the growth mask for short fairway/green turf.
- [ ] Use the existing shared light, shadow and haze paths. Evaluate a cheap
  occupancy test before expensive shading and compare depth-writing cutouts with
  blending for both stability and overdraw; neither is automatically cheapest.
- [ ] Preserve exact zone edges, paths, bunkers, ponds and cup openings. Inspect
  the ball touching turf and rolling into the cup: decorative volume must not
  obscure the ball, fill the cup or invent a second gameplay surface.
- [ ] Filter colour and occupancy at the pixel footprint. If normal detail is
  used, preserve its unresolved roughness so distant turf does not become shiny.
  Keep patterns anchored to the world, without animated texture noise or reseeding.
- [ ] Compare standing, address and crouched views, especially grazing angles,
  hill silhouettes and zone boundaries. Check clear noon, low sun, overcast, wet
  turf and night through the complete picture pipeline.
- [ ] Draw the same turf in reflections and other scene views. Any cheaper distant
  representation must match the same turf in projected appearance across its
  transition, under the existing one-world rule. Include maximum shell height
  in culling bounds and update shared cache identities only when their inputs
  change. Count every layer in main, reflected and any applicable shadow work.

- [ ] Follow [profiling procedures](../performance.md) in release with vsync off;
  record the actual GPU, window and scene sizes, layers and sample counts. Give
  shell turf its own GPU timer and compare total frame cost as well as that pass.
- [ ] Measure the material baseline and shell variants at each candidate picture
  preset. Include a turf-filled putting view, a long fairway, rough in view and
  visible water with reflected turf. Account for reflection and shadow work.
- [ ] Judge clutter by ball/cup readability, readable slopes, coherent surface
  colours and stable motion, not by geometry counts alone. Keep texture contrast
  subordinate to gameplay landmarks and avoid broad cloudy mottling.
- [ ] Present matched captures/motion and measured timings for the owner's choice
  of resolution presets and turf representation. Record the accepted budget and
  design in the affected docs, including shell adoption and layer counts;
  remove rejected prototype paths. Stripe cleanup for the material replacement
  belongs to section 8 and must not wait for this experiment.

## 13. Verification and sign-off

- [ ] Run the standard configure/build/test commands separately, stopping on
  failure, then all-course GPU checks. Include tile, crop, cache, impostor,
  short-turf, multi-resolution and reflection comparisons. Wrong signs,
  coordinates and source identities must fail. Picture and performance checks
  are independent gates.
- [ ] Run the complete app matrix on this gaming reference and at least one
  genuinely older integrated device with a weak CPU and limited memory.
  Record driver/API support and sustained thermals. Expand Intel/AMD/macOS
  coverage as devices become available; label untested hardware plainly.
- [ ] Publish full-frame tails, stage costs, RAM/load peaks, uploads and raw
  artifacts in docs/performance.md. Do not add diagnostic subtraction deltas
  or present serialized rendering as achieved gameplay FPS.
- [ ] When implementation lands, update rendering_rules.md: immutable field
  residency uploads do not rebake/reseed the world, and a tested image of
  the full tree is a representation of that same source. Record accepted turf
  and preset choices, including any selected change to the line-count rule.
  Update headers, affected code-map entries and development commands;
  remove this plan.

No checkpoint is signed off solely because Marienlyst looks smooth or one
effect counter is under 1 ms. Preserve the picture, meet whole-frame/memory
budgets, and supply actual low-end evidence.

## Turf and picture research

- [Rendering Fine Grass](https://dknazar.github.io/fine-grass/): close-cut sports
  grass, cards, matching ground shading and overdraw; a useful alternative if
  shells fail the comparison.
- [NVIDIA: shells and fins](https://developer.download.nvidia.com/SDK/10/direct3d/Source/Fur/doc/FurShellsAndFins.pdf):
  layered volume rendering and its silhouette/grazing-angle limitations.
- [NVIDIA: procedural filtering](https://developer.nvidia.com/gpugems/gpugems/part-iv-image-processing/chapter-25-fast-filter-width-estimates-texture-maps):
  average unresolved patterns over a pixel footprint to prevent aliasing.
- [Valve: Advanced VR Rendering](https://media.gdcvault.com/gdc2015/presentations/Vlachos_Alex_Advanced_VR_Rendering_V2.pdf):
  normal-map filtering, lost roughness information and specular aliasing.
