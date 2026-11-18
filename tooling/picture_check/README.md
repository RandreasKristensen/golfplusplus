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
- Trees: each species projected from 69.99 and 70.01 m must retain pixel coverage
  within 2%, with a nonempty silhouette. The shadow caster must cover exactly the same
  pixels as the visible tree in wind. Every view and shadow uses the full model.
- Water: calm, wind, overcast, low sun and night compare water with what it mirrors.
  The check uses the game's planar reflection pass. A tree on a high bank outside
  the main view must appear in its reflection (`water_offscreen_tree.bmp`).
  `water_<case>.bmp` are linear scene results shown with a display gamma, before
  the camcorder.

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
upload bytes, capacity growth and uniform misses. REF contains the real
extra scenes and current-frame visibility queries. Stages can include
waits; repeated same-stage segments are not yet accumulated by the renderer,
so do not sum stage counters as the full-frame GPU cost.

`--profile --isolate` adds diagnostic no-water, no-tree and no-grass runs.
The tree case removes both trees and their blob shadows; the grass case
removes drawing, not the allocated field, so it does not isolate memory
pressure. `--profile --night` pins winter midnight. These flags affect only
the tool, not shipped settings, content or saves. Compare repeated runs in
reversed/interleaved order before attributing a precise component delta.
On pond-free Marienlyst, full/no-water should agree within noise.

Close competing GPU apps and avoid simultaneous builds for timing. Record
actual GL renderer, power and thermals; a driver preference is not proof of
which GPU was used. `all passed` means execution succeeded, not that a
performance budget was met. Full-app/low-end acceptance work is specified
in `docs/todo/rendering_performance.md`.
