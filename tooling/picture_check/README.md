# picture_check

Rendered checks through the game's shaders on every shipped course. The window
is hidden; an OpenGL GPU is required. These are separate from `golf++-tests`.

```powershell
cmake --build build/test --target golfpp-picture-check --parallel 2
./build/test/golfpp-picture-check
./build/test/golfpp-picture-check marienlyst_golfklub --grass
./build/test/golfpp-picture-check horsens_golfklub --water
```

- Grass: exact bare-ground exclusion at bunkers, greens, fairways, tee tops and
  paths. The same world field is used for all eyes. A second shader variant
  writes actual root positions into disjoint markers within their finest cell
  (so one diagnostic root cannot hide another); every retained root after walking must have
  existed in the original view. Ground-level views along three fairways check
  root materials at 30, 60 and 90 m. A fixed rough patch must retain projected
  coverage within 2% immediately either side of every density boundary. `grass_rings_<fairway>_<distance>.bmp`
  colours density levels with `GRASS_DEBUG_RINGS`; `grass_worst.bmp` marks any
  blade on bare ground red.
- Grass tiles (`--tiles` alone): the field's GPU tiles. Tiles uploaded under a hostile pixel-unpack
  state read back texel for texel in each of the three formats (odd 65-wide rows), the state is
  restored, and the table equals the cache after every eviction. Through the game's renderer, a
  150 m walk, turns, eyes on tile corners, zoom and (where there is water) a pond's mirror are drawn
  with every tile resident, with a cache just big enough for one view (tiles evicted and reloaded:
  no pixel may differ beyond the scene's own dither) and with one a third as big (drawn in batches:
  the same to within blending order). A warm pass uploads nothing, and no frame uploads more than
  its views need. Tiles a draw itself uploads are counted apart from those fetched ahead of the eye (at most 4 a frame, half the cache when the field or eye jumped). Frame cost of the three is printed. Every course is baked through a cache file in the system temp directory, so the second run also checks reuse. No cell may mix a node with no ground and one
  that grows grass.
- Grass motion: `grass_motion_check` captures the longest 100 m rough run through the game's whole
  renderer (`picture_rig`, trees/ponds/props left out) as the eye stands, turns, walks toward and
  across it, with the clock frozen or running and wind 0 or 6 m/s. The scene before picture
  processing is compared with the same frame at twice the lines box-filtered (aliasing, and how
  much the picture changes against how much the world does); the final TV picture's change is
  reported alongside. Beyond 8 m the scene may change at most 1.3 times the reference, and its
  mean luma stays within 12%. Default runs check 360 lines; `--motion` runs only this check at 360,
  480 and 720 lines (`--lines=N` for one, `--min-pixels=V` to replace `blade_min_pixels`, which
  reproduces the defect at 0). The tile check reruns the same walks and turns across cache transitions.
- Turf review (`--turf=<label>` alone, `--diagnose` adds the night's parts): on a hole with a floodlit green,
  putting, address, cup, approach and fairway views under noon, low sun, overcast, wet, night and wet night
  at 360 lines. Scene pictures (all), final TV pictures (five) and `<label>_metrics.csv` go to
  `build/<preset>/turf_review/` as `<label>_<view>_<case>_scene|tv.bmp`, so a run before and after a material
  change is matched. Motion over the fairway, the green's approach and a pond's reflection reuses the grass
  motion measures and is printed, not gated.
- Eyepiece (`--eyepiece` alone, also in every default run): the rangefinder held up at the start in four directions
  and, with water, at the largest pond from four banks, under noon and night, with and without ponds drawn. Each
  picture the eyepiece makes is drawn with the depth mask on and off (`renderer::mask_eyepiece`); every pixel must be
  bit-identical, failures write both as `eyepiece_<case>_shaded|trimmed.bmp`.
- Crowd review (`--crowd=<label>` alone, `--against=<label>` to compare): forty golfers with their carts, bags and
  balls standing in front of the course's start, five of them close, and (with water) fifteen on a pond's bank,
  under noon, low sun and night, through the game's whole renderer at 360 lines. The scene before picture
  processing is written as raw floats and a BMP to `build/<preset>/crowd_review/<label>_<view>_<case>.f32|bmp`
  (one label a course: `--crowd=ry-before ry_golf_klub`); `--against` prints how many pixels differ from another
  label's run and by how much. It is for a change to how figures, carts and gear are drawn: capture before, change,
  capture after, compare.
- Trees (`--trees` alone runs only these and the image gate): each species, in leaf and
  in a winter's bare crown with withered leaves, projected from 69.99 and 70.01 m must
  retain pixel coverage within 2%, with a nonempty silhouette. The shadow caster must
  cover exactly the same pixels as the visible tree in wind. Falling leaves drawn by the
  renderer must lie where `falling_leaf.h` puts them: every leaf in sight covering a few
  pixels shows at its centre, and no pixel they change lies off a leaf. A walking eye
  passing bare crowns (beech, oak, birch, willow at 30 and 70 m) must see them change from
  frame to frame at most 1.3 times as much as a picture of twice the lines does (the
  grass's motion bound): fine twigs must not shimmer. Summer and winter pictures of each
  species, near (its model) and far (its image), go to `trees/` for a person to look at. Tree images: each species is drawn whole and as its
  baked image (rendered eight times supersampled) from views between the grid's, and the
  silhouette error (a share of the crown radius, at most `radius_error`), mean coverage
  (2%), mean luminance (7%) and depth are compared, with the cache under 24 MiB, the
  bake time and which trees each pass (main, zoom, quarter-line mirror, wind) draws as
  images. The default run gates a subset (summer and bare); `--tree-images` alone runs the full sweep
  (every variant, season (winter with withered leaves too), three crown shapes, noon and night, crown size and wind;
  about nine minutes, rows in `build/<preset>/tree_images.csv`).
- Shadow receivers (`--shadow-receivers` alone, also in every default run): the game's whole renderer draws trees
  and rough at noon, in low winter sun and in the evening from four sides of the course's densest cluster
  of trees (`shadow_<case>_<side>.bmp`), then every linked program holding a `u_shadow_map` must read the
  shadow unit (a receiver whose sampler is never set reads unit 0 and shades with the wrong texture). Trees:
  a grove drawn depth first must equal the plain draw but for ties.
- Water: calm, wind, overcast, low sun and night compare water with what it mirrors.
  The check uses the game's planar reflection pass. A tree on a high bank outside
  the main view must appear in its reflection (`water_offscreen_tree.bmp`).
  `water_<case>.bmp` are linear scene results shown with a display gamma, before
  the camcorder. `water queries` mirrors two ponds at different heights under
  occlusion queries: drawn when shown (result pending or known), skipped when known
  hidden, drawn again the frame the water is exposed, each level its own texture,
  and GL state restored.
  `water crop` renders each of 14 views (calm to maximum wind, corners, grazing,
  on and under the water, turning, ponds at two heights) with cropped mirrors and
  with whole-picture mirrors, with scene content and with texel stripes, and
  requires the pictures to agree; failures write `water_crop_diff_*.bmp`.
  `terrain levels` (`terrain_lod_check`) draws the largest pond from four banks at 25 and 70 m through the
  whole renderer with the ground's coarser mirror levels and with the full ground; at most 1% of the pixels
  may differ by a visible step (`terrain_lod_*_full|levels.bmp` on failure), and the same views at 8 pixels
  of error must fail that test, so a level that is too coarse is seen.

The program exits 1 on failure; output is `build/<preset>/picture_check/<course>`.

For repeatable GPU measurement of the full renderer (1920 x 1080 window, the
shipped scene line count, no vsync), build the same target in release:

```powershell
cmake --preset release
cmake --build build/release --target golfpp-picture-check --parallel 2
./build/release/golfpp-picture-check marienlyst_golfklub --profile
```

It warms up for 60 frames, measures 240 at the play camera, beside a pond
when present, and across a full camera sweep. Each frame rebuilds render
data/lights/weather, renders, swaps with vsync off and calls `glFinish`.
This is serialized rendering cost, not achieved whole-game FPS: simulation,
network, audio and loading are excluded; submission includes driver/query
waits. A first sweep may include new-view resource growth.

It writes `benchmark_noon.csv`, `benchmark_noon_frames.csv`,
`benchmark_noon_gpu.csv` and `benchmark_noon_info.txt` per course. Results
include frame-time percentiles, preparation/submission time, draw calls,
buffer upload bytes, reallocations and uniform misses. The frame CSV also
reports swap and explicit finish times. GPU rows carry a valid sample count.
FRAME uses timestamps at the renderer boundaries. REF and ZOOM include their
children. Repeated segments of a stage accumulate in its row, including
the REF/, ZOOM/ and ZOOM/REF/ rows. The console reports the mean full GPU interval and its sample count.

`--profile` also fails a warmed stationary view (main, and the mirrored one at a
pond) that repeats a uniform lookup, reallocates a buffer or sends more than 256 KiB
a frame, and checks that the cached geometry (placed models, sign and fence casters,
tee boxes, cups) is sent on the first frame, on entering the course again and after
a palette reload, and never when only night glow or the clock moves.

`--profile --isolate` adds diagnostic no-water, no-tree and no-grass runs.
The tree case removes both trees and their blob shadows; the grass case
removes drawing, not the allocated field, so it does not isolate memory
pressure. `--profile --night` pins winter midnight. These flags affect only
the tool, not shipped settings, content or saves. Compare repeated runs in
reversed/interleaved order before attributing a precise component delta.
On pond-free Marienlyst, full/no-water should agree within noise.

The full-app matrix runs the release game with normal simulation, audio and presentation.
Run `py -3 tooling/picture_check/full_app_benchmark.py --output build/release/benchmarks/full-app` from the root.
See [the protocol and budget reports](../../docs/performance.md#scripted-full-app-benchmark).

Close competing GPU apps and avoid simultaneous builds for timing. Record
actual GL renderer, power and thermals; a driver preference is not proof of
which GPU was used. `all passed` means execution succeeded, not that a
performance budget was met. The full-app runner evaluates the gaming-reference gates in
[the rendering performance plan](../../docs/todo/rendering_performance.md).
