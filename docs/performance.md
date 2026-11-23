# Profiling

Profile the **release** build; debug is not representative.

## Tee book requests through the real SDK (2026-11-23)

Fresh disposable local database per run, `golfpp-online-check --tee-book-timing`
(`tooling/net/online_check/tee_book_timing.cpp`): one headless client at the book, module
reducer time from the server's `/v1/metrics` (`reducer_wasm_time_usec`), client time from
dispatch to the answering row. Unoptimised wasm (no wasm-opt), module built on this
machine. Before: `open_tee_book` and `book_tee` built the destination course
(`cached_course`); after: they check its manifest (`find_server_course`).

| Request | Before: client / module | After: client / module |
|---|---|---|
| Open, never-opened large course (Hedensted, 11 MB) | 4090 ms / 4047 ms | 51 ms / 0.2 ms |
| Open, another large course (Jelling) | 3467 ms / 3418 ms | 34 ms / 0.2 ms |
| Open, another small course (Jelling par 3) | 713 ms / 668 ms | 35 ms / 0.2 ms |
| Second open, same course | 54 ms / 0.2 ms | 44 ms / 0.2 ms |
| Manual refresh (5 samples) | 36-61 ms / 0.1-0.2 ms | 48-68 ms / 0.1-0.2 ms |
| Back to an opened course | 38 ms / 0.1 ms | 42 ms / 0.2 ms |
| Periodic refresh after 61 s idle | 46 ms / 0.2 ms | 57 ms / 0.1 ms |
| Open Kalo (8 MB, travel course) | 3324 ms / 3273 ms | 38 ms / 0.1 ms |
| `book_tee` for it | 96 ms / 0.4 ms | 126 ms / 0.4 ms |
| Cold `take_bus` (client until the destination room is adopted) | 2401 ms / 0.5 ms | 5765 ms / 3301 ms |

The cold cost was the destination build (about 3-4 s of module time for a large course,
fuel 2.6-3.2e10), and it was paid by the first player to open a course's book after the module
started. It now falls on the first physical entry to a course (the travel screen), as
the build is needed there: the first bus to a course after a module (re)start costs
about 3.3 s more. The built course stays cached for the module instance's life (still warm
after 61 s idle); nothing builds all courses. Warm requests were never the delay: a warm
`open_tee_book` runs in 0.1-0.2 ms in the module and 35-70 ms round trip. The client
shows the times on the next frame whatever the answer's latency (native tests), the counts
following. Periodic and manual refreshes send the same request, so they cost the same.

## Scene resolution presets (2026-11-22)

Scene size and window pixels per scene pixel (factor), by window; not timed.
The pixel counts say what each costs against the 360-line reference.

| Window | 360 | 720 |
|---|---|---|
| 3440x1440 | 860x360, x4 | 1720x720, x2 (4x pixels) |
| 1920x1080 | 640x360, x3 | 960x540, x2 (2.25x) |
| 1280x720 | 640x360, x2 | 1280x720, x1 (4x) |

Candidates 480 and 720 resolved identically on 1080 rows (both x2), so the
presets are 360 and 720: multiples of the HUD's 360 lines, distinct at every
window height. A 1080-line preset was dropped by owner decision (2026-11-22; 9x to
16x the pixels). Captures: `build/test/preset_review/`.

## Startup switches

They are environment variables, read once at startup in `src/core/main.cpp` and
parsed by `src/core/startup_options.cpp`. Nothing in game, physics or renderer
code reads them.

| Variable | Effect |
|---|---|
| `GOLFPP_COURSE=<course id>` | Skip the menu and boot straight into that course (the `id` in `assets/world/courses/*/course.json`). Unknown ids fall back to the menu. `practice_ground` (the hub's id) boots the offline hub at its bus-door arrival; with `GOLFPP_SCREENSHOT` and `GOLFPP_GAME_TIME` it photographs the plaza by day or night. |
| `GOLFPP_VSYNC=0` | Present without vsync so FPS shows real headroom. Also accepts `off`, `no`, `false`, `disable(d)`; anything else keeps vsync on. |
| `GOLFPP_WINDOW=<W>x<H>` | Open the window at this size in pixels (e.g. `3440x1440`) instead of 1280x720. Anything else keeps the default. The fullscreen setting still wins. |
| `GOLFPP_SCREENSHOT=<path.bmp>` | 120 frames after loading (into `GOLFPP_COURSE`, or the menu), save the window's picture after the TV pass as a 24-bit BMP and quit. Runs with default settings, silent (no sound device is opened), and never writes the save. For reviewing the picture. |
| `GOLFPP_GAME_TIME=<hour>` or `<YYYY-MM-DD>T<hour>` | Pin the sky at this hour on the course's clock (`0` to `24`, e.g. `19.5`; the course file's `sky.utc_offset_hours`) on the course's date (its `sky.date`), or on the given date (`1989-12-21T23`: a Danish midsummer night never gets darker than about −10.5°, so a winter date shows a true night), so screenshots show the same light every time. A given date also dates the booted offline course, so its trees stand in that day's season (bare in winter). Anything else leaves the clock running. |
| `GOLFPP_WEATHER=<kind>[,<wind>[,<wetness>]]` | Hold the weather offline from the start (the god menu's hold, `src/core/god_menu.h`): `clear`, `cloudy`, `light_fog`, `fog`, `light_rain`, `rain`, `heavy_rain`, `thunderstorm` or `sun_shower`, then optionally the wind in m/s (at 10 m, blowing towards +Z) and the ground's wetness from `0` to `1`, e.g. `heavy_rain,8,1`. Anything else holds nothing. Online play ignores it. |
| `GOLFPP_TOUR=<n>` | Start the visual tour (`src/core/visual_tour.h`, the main menu's VISUAL TOUR) at its n-th stop, from 1 (`assets/picture/visual_tour.json`), on `GOLFPP_COURSE` or the first course. With `GOLFPP_SCREENSHOT`, a picture of that stop; with the profiler overlay (the god menu's PROFILER row), its GPU timings; with `GOLFPP_GOD=1`, F2 opens the look panel to step the air's numbers live (`docs/development.md`). |
| `GOLFPP_SCENE_RESOLUTION=<n>` | With `GOLFPP_SCREENSHOT`: draw at the n-th scene resolution preset (`scene_lines` in `assets/picture/picture.json`, 0 the standard 360) instead of the default setting. |
| `GOLFPP_GOD=1` | The god menu can be opened with F1 (as `--god` does): hold the time of day, the weather, the wind and the wetness to try them. Arrows change a row, Backspace lets it run live again. In the visual tour, F2 opens the look panel. |

```pwrshl
$env:GOLFPP_VSYNC = "0"; $env:GOLFPP_COURSE = "marienlyst_golfklub"
.\build\release\golf++.exe
Remove-Item Env:GOLFPP_VSYNC, Env:GOLFPP_COURSE

$env:GOLFPP_COURSE = "marienlyst_golfklub"; $env:GOLFPP_WINDOW = "3440x1440"; $env:GOLFPP_SCREENSHOT = "wide.bmp"
.\build\release\golf++.exe
Remove-Item Env:GOLFPP_COURSE, Env:GOLFPP_WINDOW, Env:GOLFPP_SCREENSHOT
```

## Overlay

Turn the god menu's (F1, with `GOLFPP_GOD=1`) PROFILER row on to show FPS +
the profiling overlay. While it shows, hold `T` to run the sky
forward (two game hours a second); the offset lasts until the game quits and
only this player sees it. Values are averaged over 0.25 s; timings are
whole microseconds to keep the lines short.

| Counter | Meaning |
|---|---|
| `UPD` | CPU time in `update_game` (physics, movement, terrain sampling) |
| `MESH` | Refreshing static anchors and render meshes. Should be ~0 except on the frame an area loads |
| `MRD` | Building the frame's render data. Should stay well under 1 ms |
| `SWP` | Wall time in `SDL_GL_SwapWindow`, including presentation and driver waits |
| `REN` / `SCN` / `OVL` | Wall time in renderer submission / scene / overlay |
| `QRY` / `UPL` | Wall time in query polls and result reads / GL storage and upload calls. These overlap submission. |
| `DRAW` | Draw calls, excluding the overlay's own (`DBGUI`) |
| `UNI` / `ULOC` | Uniform sets / uniform location lookups. `ULOC` must be 0 after warmup |
| `BUF` | Streaming buffer writes / reallocations / bytes. Reallocations must settle to 0 |
| `TSAMP` / `TTRI` | Terrain sample calls / triangles tested across them |
| `CHUNK v/c R IDX` | Terrain chunks visible / culled, merged draw ranges, indices drawn |
| `TREE` | `ON` when the instanced tree batch passed frustum culling |
| `GPU SMAP` / `LMAP` / `TER` / `GRS` / `SHD` / `TRE` / `PRP` / `SKY` / `REF` / `WAT` / `WTR` / `SHF` / `BLM` / `SMR` / `CAM` / `EYE` / `TAPE` / `OVL` / `CRT` / `ZOOM` | GPU timestamp intervals, collected once available after at least two renders: the shadow map of trees, floodlights, players and carts (`shadow_map_pass`, 0 with the sun down) / the floodlights' shadow maps (`lamp_shadow_pass`, redrawn when a lamp, the casters or the crowns' wind pose changes) / terrain / the fixed world grass tufts / blob shadows (`blob_shadow_renderer`) / trees, signs, fence posts and floodlights / every model: carts, clubs and what is held, mine pinned to the camera too (`prop_renderer`) / the sky and land panorama behind them, its rainbow too (`backdrop_pass`) / the real scene mirrored at each visible water level (`reflection_pass`) / the ponds: the copy of the scene under them and their surfaces, or the picture seen from under one (`water_renderer`, 0 with no pond in view) / rain streaks and the wind's specks (`weather_renderer`, two line draws, 0 with neither) / the sun shafts taken out of the haze and the trees' shadows out of the nearest floodlights' beams (`sun_shaft_pass`, 0 with neither) / the camcorder's quarter-size copy, metering and bloom / its column smear / its recording of the picture (`camcorder_pass`) / the rangefinder's eyepiece (`eyepiece_pass`, 0 when not held up) / tape pass / overlay / TV pass / the eyepiece's zoomed view, the scene drawn again within the eyepiece at its zoom (0 when not held up). While paused only the tape, overlay and TV run |

The overlay text comes from `format_profile_overlay_lines` in
`src/profiling/profiling.cpp`.

## Per-frame records

A driver can pass a callback to `app::run`. It receives one `frame_profile` per
loop iteration, identified by `frame_number`, after the CPU work and GPU query
results can be joined. Copy a record to retain it. The callback executes on the
app thread, outside that record's measured interval. Use frame numbers for
chronological order. A loop without a render, a full query ring, unavailable
timestamps, or busy queries at shutdown leave `gpu_timers_available` false. CPU and work counters are
available for those frames too. Timestamp polling charges its wall time to the
iteration doing the polling. Turning on the callback leaves the FPS overlay
under the god menu's control.

`frame_ms` spans input through the end of the loop, including swap and online
work. The stage array times simulation, mesh refresh, render-data construction,
renderer submission and swap. Query reads, uploads and screenshot pixel
readback have separate wall-time counters. Driver calls can wait internally.
The full GPU interval spans the renderer's first and last timestamp. The
reflection and zoom intervals include nested work. Repeated draws add to
`gpu_ms[group][stage]`. Groups are scene, reflection, zoom and zoom reflection.
Use `gpu_frame_ms` for the whole render.

`work.passes` counts submitted tree instances and triangles, grass instances,
and visible/submitted terrain indices for each scene and shadow view type.
`work` also records candidate water levels, captures and pixels actually drawn,
storage growth and host transfers. Dynamic upload bytes exclude the debug overlay; its upload time is not separated.
Cached transfers occur when filling retained
geometry or textures. Dynamic transfers include payloads rebuilt or streamed
on every draw, including trees. Logical GPU bytes track live buffer capacities,
texture levels (including mip chains), and renderbuffer payloads. These counts exclude driver overhead and deferred deletion.
The process sample reports current and lifetime peak resident bytes when available.
Course load and render-mesh bake durations describe the most recent course
entry and revision bake.

## Scripted full-app benchmark

From the repository root, run these commands separately. The last command needs a fresh output directory.

```powershell
cmake --preset release
cmake --build build/release
py -3 tooling/picture_check/full_app_benchmark.py --output build/release/benchmarks/full-app
```

The runner freezes the executable and its assets, then starts a fresh offline app for each place and repeat.
Defaults are 1920 x 1080, shipped picture settings, swap interval zero, three repeats and 10,000 measured
milliseconds per view. Place and view order alternate between repeats. Use `--reverse` to reverse the
first repeat. For a short diagnostic, add `--place marienlyst_golfklub --ms 500 --repeats 1`. `--date 01-15`
dates the courses (`GOLFPP_GAME_TIME`), so their trees stand in that day's season; the hub always stands at
midsummer.
Linux uses `python3` and the release executable without an extension.

Each process first visits every scripted view, stepping the heading through 32 directions during
the cold traversal. Each steady view has a timed warmup of one second (capped at the measurement
duration for quick runs). Cold denotes first use in a fresh process. OS file caches and the driver's
on-disk shader cache can persist between launches. Gameplay input, simulation, lighting, render preparation, GPU submission,
audio and window swap follow the offline play loop. Script setup and CSV writes fall outside
the frame timer. The shot simulation itself is charged to update. CPU combines UPD, MESH, MRD and REN.
Nested CPU and GPU stages overlap their parents. Empty GPU fields mean unavailable timestamps.
The course and hub stress views add local render figures and balls before frame lighting, and their carts
through the frame's own cart path (`make_render_data`'s `scripted_carts`, so `cart_fit_memo` sees them).
The 0/5/40 count is the number added of each kind. Raw cart totals also include the golfer's
ordinary course cart where present. The 40-nearby views come twice: `play_40` and `plaza_40` stand
still; `play_40_moving` and `plaza_40_moving` walk the figures up and down the fairway (1.4 m/s over 12 m)
and drive the carts across it (4.5 m/s over 10 m, headlights on, steering turning), each from a fixed 1/60 s
a frame, so every cart and figure changes every frame at any frame rate. The crowd moves in
`benchmark_driver::prepare`, outside the timed stages, as a server row's interpolation is.
These offline cases measure render-data population, lighting
and drawing for the crowd. Online row decoding and interpolation (`shown_avatar`) have a separate workload.
The pond-free water control uses the same play camera.

`manifest.json` records parameters, CPU, source revision/dirty state and executable/asset hashes.
The matching files live in `snapshot/`, with source changes alongside it. Each
`repeat-N/<place>/` contains `frames.csv`, `app.log` and `context.txt` (actual GL renderer/driver,
drawable, swap interval, power, content fingerprint and grass payload). `gpu-telemetry.csv` samples
device-wide clocks, temperature, thermal limiting, power, utilization and used memory once per second
when NVIDIA telemetry is available. Windows also samples WDDM dedicated and shared process GPU memory
in each place's `gpu-memory.csv` when its performance counters are available. SDL power states 1/2/3/4 mean battery/no battery/charging/charged.

`metrics.csv` gives nearest-rank p50/p95/p99, maxima, sample counts and threshold counts by phase,
view and repeat. `views.csv` evaluates the section 1 budgets per steady view and identifies missing
evidence. `places.csv` takes the worst value of each statistic across that place's steady views
and repeats. The solo dynamic upload cap uses every phase of that view. The logical GPU cap uses
the highest sampled allocation across all views and phases in the process. Stationary operation
limits apply to every steady frame of the eligible solo views.
The estimated 60 Hz multiplier divides 16.67 ms by the worst complete-frame p99.
It expresses the uniform change in frame cost that would reach a 60 Hz interval at shipped settings.
Values below one require a reduction even on the reference laptop. Each output directory retains
the exact report evaluator alongside the raw observations.
The load-memory gate uses the OS lifetime peak at the end of frame zero, after course entry, baking and
the first render. The lifetime peak through the last measured view is reported separately.
Grass GPU includes the field, tuft geometry and mipmapped ground detail. Grass RAM counts retained field
capacity and simultaneous bake-index capacity. A conservative upper bound includes vector growth overlap
and measured draw scratch. A budget between the lower and upper bounds remains unverified. These logical
payloads exclude allocator/driver overhead. The whole-renderer
logical GPU ledger covers all tracked buffers, textures and renderbuffers.

`memory.csv` reports WDDM sample counts and peak dedicated/shared bytes summed across the process's adapters.
`hottest.csv` ranks stage p95 by place and view, taking the worst repeat. `water-control.csv` pairs
Marienlyst's full and no-water play p95 by repeat.

The runner returns 0 for all evaluated gates passing, 2 for measured budget failures and 1 for
incomplete evidence or execution errors. A shortened run is labelled diagnostic. To regenerate reports
from saved frames, run `py -3 tooling/picture_check/benchmark_report.py <output-directory>`.
Direct driver switches are `GOLFPP_BENCHMARK=<directory>`, `GOLFPP_COURSE=<place>`,
`GOLFPP_BENCHMARK_MS` and `GOLFPP_BENCHMARK_REVERSE=1`. The direct executable writes one process's
raw records. The Python runner supplies the matrix, reproducibility artifacts and budget verdicts.

## 2026-11-19 full-app baseline

**FAIL on the gaming reference.** All 148 views ran forward, reverse and forward on AC on the Ryzen 7 5800HS / RTX 3050 Ti Laptop (NVIDIA 610.47, 16 GB system RAM). The 57 release processes used the shipped 640 x 360 scene, a 1920 x 1080 drawable and swap interval zero. Every measured traversal accumulated at least 10 s after its cold traversal and timed warmup. Of 353,778 measured frames, 218,920 exceeded 8 ms and 85,163 exceeded 16.67 ms. GPU timestamps resolved for 353,714 frames.

Each pair gives the **worst steady p95 / p99 in milliseconds** across that place's views and repeats, calculated independently per column. The named view has the worst complete-frame p95. Memory columns give maximum steady working set, lifetime peak working set and logical GPU allocation in MiB. The complete-frame targets are 5 / 8 ms, CPU p95 is 2.5 ms and GPU targets are 3 / 4 ms.

| Place | Worst frame view | Frame | CPU | GPU | Steady / peak / GPU MiB | Result |
|---|---|---:|---:|---:|---:|---|
| blommenslyst_9_holes | play_40 | 33.4/35.6 | 33.3/35.5 | 16.9/18.3 | 440 / 450 / 112 | FAIL |
| blommenslyst_golfklub | zoom_flag_night | 41.4/42.7 | 41.2/42.6 | 33.4/34.3 | 840 / 846 / 324 | FAIL |
| hedensted_golf_klub | play_40 | 83.7/88.4 | 81.4/86.7 | 48.4/56.4 | 1330 / 1391 / 710 | FAIL |
| hedensted_pay_play | play_40 | 56.5/61.1 | 56.3/60.9 | 22.2/24.8 | 607 / 615 / 218 | FAIL |
| himmerland_new_course | zoom_flag_night | 41.9/43.8 | 41.7/43.7 | 37.1/37.8 | 1166 / 1207 / 545 | FAIL |
| himmerland_par_3 | play_40 | 51.3/54.4 | 51.2/54.2 | 38.5/39.5 | 759 / 759 / 251 | FAIL |
| horsens_golfklub | play_40 | 42.7/45.7 | 42.6/45.5 | 30.7/35.6 | 1046 / 1086 / 409 | FAIL |
| horsens_par_3 | play_40 | 35.7/37.9 | 35.5/37.7 | 15.9/16.9 | 444 / 446 / 118 | FAIL |
| jelling_golfklub | play_40 | 48.2/50.0 | 48.1/49.8 | 30.3/32.6 | 1147 / 1161 / 561 | FAIL |
| jelling_par_3 | play_40 | 39.5/43.7 | 39.3/43.5 | 24.9/26.2 | 426 / 447 / 96 | FAIL |
| kalo_golf_club | play_40 | 49.6/51.9 | 49.5/51.7 | 29.9/34.5 | 1062 / 1104 / 517 | FAIL |
| kalo_par_3 | play_40 | 43.1/51.0 | 43.0/50.8 | 18.4/20.6 | 436 / 444 / 112 | FAIL |
| marienlyst_golfklub | play_40 | 38.1/45.7 | 38.0/45.6 | 18.9/21.6 | 386 / 389 / 81 | FAIL |
| mollerup_golf_club | play_40 | 86.0/104.3 | 85.3/104.1 | 59.0/70.6 | 903 / 918 / 381 | FAIL |
| mollerup_par_3 | play_40 | 34.7/39.7 | 34.6/39.6 | 18.3/20.1 | 394 / 394 / 85 | FAIL |
| odense_golfklub | play_40 | 45.6/48.9 | 44.4/47.7 | 24.7/31.2 | 950 / 950 / 377 | FAIL |
| odense_pilebanen | play_40 | 54.2/61.6 | 53.7/61.3 | 21.2/23.7 | 701 / 702 / 271 | FAIL |
| ry_golf_klub | pond | 64.9/69.5 | 64.8/69.3 | 65.0/69.0 | 1026 / 1028 / 442 | FAIL |
| practice_ground | plaza_40 | 40.4/44.3 | 40.1/44.1 | 21.6/24.6 | 462 / 462 / 112 | FAIL |

In this baseline the CPU column still included GPU query waits inside submission; later runs exclude `query_read` from CPU time. Scene submission dominated the five most expensive CPU views. Mollerup play with 40 nearby figures, balls and carts reached 85.33 ms CPU p95, including 78.98 ms in scene submission. Ry pond reached 64.96 ms GPU p95, with 47.70 ms in the reflection pass and 23.31 ms drawing trees within that pass. CPU query polling at Ry pond reached 40.28 ms p95. These nested intervals overlap. Marienlyst's full and no-water play views both recorded zero captures. Disabling water changed complete-frame p95 by −0.36, +1.43 and −0.87 ms across the paired repeats.

Nine places exceeded 768 MiB steady working set, five exceeded 1 GiB during load and ten exceeded 256 MiB logical GPU. Hedensted golf klub reached 1,304 MiB at load and 986 MiB observed dedicated GPU memory. The largest observed shared GPU allocation was 201 MiB at Odense Pilebanen. Grass at Hedensted occupied 471 MiB logical GPU and an estimated 543–616 MiB total CPU RAM. The 64 MiB grass RAM gate remained unresolved at Blommenslyst 9 holes, Horsens par 3 and Kalø par 3 because their measured bounds straddled the limit. Every place exceeded the 8 MiB grass GPU cap and the 256 KiB solo dynamic-upload cap. The hub range reached 14.23 MiB in one solo frame. Himmerland new course had the longest load (5.98 s) and render-mesh bake (19.97 s).

Preflight found an idle RTX. GPU temperature reached 89 °C and software thermal slowdown was active in 5,393 of 6,003 telemetry samples. Interpret repeat differences in that thermal context. The **estimated** 60 Hz proxy at shipped settings requires worst-view p99 frame costs of 0.16–0.47 times their measured values, depending on place. Driver backing stores, in-flight frames and old-course resources still lack separate attribution.

Raw frames, phase statistics, budget verdicts, WDDM and GPU telemetry, and the frozen executable and assets are under `build/release/benchmarks/full-app-2026-11-19/`. Source was `3a615d706485dc0ff0bfe55e47ddbb7663158c68` with local changes captured alongside the manifest. The retained `benchmark_report.py` reproduces the final tables from the raw frames.

## Tree index and static instance buffer (2026-11-19)

Section 5, trees. Each course revision builds every tree's instance (offset,
scale, yaw, tint, variant) and its wind-proof bounds once, sorted by species,
variant and place into 16 m cells within 128 m blocks, and uploads them once
to a static buffer. A view tests blocks then cells against its frustum and
draws the runs it selects through attribute-buffer offsets. The old path
re-hashed and re-bucketed every tree, per view, every frame, and streamed the
instances each time. Bounds now hold at the 60 m/s tuning cap instead of the
earlier 0.2 height sway, so culling is a little looser than before.

Full-app benchmark, release, 1920 x 1080, quick runs (`--ms 2000 --repeats 1`),
before and after executables run back to back twice and averaged (the GPU
throttles thermally, so single runs drift by 10-20%). Steady p50, milliseconds.
CPU excludes query waits.

| View | CPU before > after | GPU before > after | Frame before > after | Draws before > after |
|---|---:|---:|---:|---:|
| Mollerup play | 10.8 > 5.7 | 13.0 > 12.1 | 13.6 > 12.1 | 290 > 611 |
| Mollerup pond | 7.6 > 4.1 | 7.0 > 5.4 | 9.0 > 5.7 | 181 > 255 |
| Mollerup sweep | 6.0 > 3.4 | 5.4 > 4.5 | 7.2 > 4.8 | 107 > 196 |
| Mollerup walk, windy dusk | 10.6 > 5.4 | 14.1 > 13.2 | 14.3 > 13.2 | 294 > 669 |
| Mollerup zoom flag, night | 20.1 > 10.5 | 21.1 > 15.6 | 25.7 > 17.1 | 387 > 649 |
| Hub plaza | 5.5 > 5.7 | 5.6 > 5.7 | 5.7 > 5.8 | 86 > 93 |
| Hub range | 5.9 > 5.5 | 5.8 > 5.4 | 6.0 > 5.6 | 85 > 93 |
| Hub bus | 5.6 > 5.5 | 5.6 > 5.5 | 5.8 > 5.7 | 77 > 115 |

Mollerup's CPU halves (about 5 ms a frame, with four tree views a frame); the
hub's roughly 900 trees show no change beyond noise. Draw calls rise because
a view is now several runs per species and variant instead of one. Submitted
tree instances rise 9% (Mollerup play, main 2,082 to 2,295; sun map 75 to 90;
mirrored 4,162 to 4,589) from cell granularity and the looser bound, and the
tree GPU stage (`TRE`) reads 0.1 to 0.25 ms higher in every view (1.08 > 1.31
in play), the cost of those extra instances. Dynamic upload bytes fall by only
0.2 MiB a frame (Mollerup play 4.76 to 4.55 MB), because trees were a small
part of the stream; the rest belongs to the other section 5 boxes. Warm views
perform no static uploads. Sun-map GPU time (`SMAP`) swung widely between runs
(sweep 1.4 > 0.14 ms) under thermal throttling and is not claimed. Raw runs:
`build/release/benchmarks/ab-{before,after}-{1,2}-{mollerup_golf_club,practice_ground}/`.

## Static props, casters and frame uploads (2026-11-19)

Section 5, everything but trees. The placed models, the sign and fence casters,
the tee boxes and the cups are uploaded once per course (and again after a
palette reload for the casters, tee boxes and cups); the frame's moving models
and markers are built and uploaded once and every view and the sun's map draw
the parts they show; a buffer sends only the parts whose bytes changed. The
backdrop sets 35 uniform names against a cache of 32: three lookups a view
every frame; the cache now holds 128.

Full-app benchmark, release, 1920 x 1080, `--ms 2000 --repeats 1`; before one
run, after the mean of two (the GPU throttles, so single runs drift by 10%).
Steady p50, milliseconds; CPU excludes query waits. Dynamic bytes are every
upload a frame, the HUD's included.

| View | CPU | GPU | Frame | Draws | Dynamic KiB | Uniform misses |
|---|---:|---:|---:|---:|---:|---:|
| Hub plaza | 6.4 > 3.1 | 6.3 > 3.1 | 6.5 > 3.1 | 93 > 98 | 14,537 > 66 | 3 > 0 |
| Hub plaza, 40 figures, balls, carts | 23.7 > 11.7 | 18.2 > 3.4 | 23.8 > 11.8 | 93 > 102 | 39,244 > 70 | 3 > 0 |
| Hub range | 6.3 > 3.1 | 5.9 > 2.8 | 6.4 > 3.1 | 93 > 97 | 14,546 > 66 | 3 > 0 |
| Hub night lamps | 9.1 > 5.7 | 9.1 > 5.6 | 9.2 > 5.7 | 80 > 80 | 7,299 > 66 | 3 > 0 |
| Mollerup play | 5.8 > 3.3 | 14.5 > 10.4 | 14.5 > 10.3 | 611 > 625 | 4,447 > 57 | 9 > 0 |
| Mollerup play, 40 figures | 42.7 > 14.7 | 36.3 > 10.5 | 45.7 > 17.8 | 611 > 632 | 54,496 > 70 | 9 > 0 |
| Mollerup pond | 4.1 > 2.5 | 6.3 > 4.3 | 6.2 > 4.4 | 255 > 263 | 3,245 > 56 | 6 > 0 |
| Mollerup walk, windy dusk | 5.6 > 3.4 | 15.9 > 10.6 | 15.9 > 10.7 | 669 > 683 | 4,442 > 138 | 9 > 0 |
| Mollerup zoom flag, night | 10.4 > 7.1 | 18.4 > 13.2 | 18.9 > 13.9 | 649 > 657 | 4,844 > 60 | 12 > 0 |

The GPU falls too because the shadow map no longer redraws the placed models
and signs from freshly streamed copies and the crowd no longer copies 40
figures through the driver each view. Mollerup's GPU stays near 10 ms: trees
and the mirrored scene, the next sections.

Section 1 targets, solo: every warm stationary view has zero static uploads,
uniform misses and capacity growth, and sends 56-72 KiB a frame (the HUD's
112 KB overlay is sent only when its words change); the 256 KiB limit holds in
every steady frame (largest: Mollerup's walk, 249 KiB, whose moving bag is
re-sent whole; drawing the bag instanced with a transform would cut it, not done). It does not hold for the first frame of each view, which sends 0.6 MB
(hub), 0.9 MB (Mollerup) and up to 13 MB with 40 figures, so the benchmark's
per-view maximum over every phase still exceeds it.

Streaming compared with crowds changing every frame (the benchmark's crowd
stands still, so the same bytes were forced through each method by temporary
code, since removed): in-place `glBufferSubData`, whole-buffer orphaning
(`glBufferData(nullptr)` then the data) and a three-buffer ring sent 10 MB a
frame in the crowd views. Frame p50, two runs each in reversed order: hub
plaza 40 figures 10.49 / 10.98 / 10.57 ms, Mollerup 40 figures 18.76 / 19.06 /
18.62 ms; GPU 4.65 / 4.79 / 5.06 and 13.5 for all three. Differences are inside
the run to run noise, so the plain buffer stays. Only changing parts are sent,
which is where the saving is (10 MB to 72 KB at a standing crowd). No storage
is orphaned, so there is nothing to count; `buffer_growths` counts capacity
growth and is zero when warm.

Picture identity: old and new executables screenshot all 18 courses and the
hub at noon with calm weather; block means (8 x 8 pixels) of the signed
difference exceed 5 grey levels in about 150 blocks in every course, as in two
runs of the same old executable (the tape's grain and clock). The unit tests
compare each view's props with what the previous build assembled and the
sun's selected casters against the old per-triangle rule; the `--profile`
check exercises entering a course again and a palette reload.

Raw runs: `build/release/benchmarks/props-{before,final-1,final-2}-{mollerup_golf_club,practice_ground}/`.

## Reflection candidates and conditional mirrors (2026-11-19)

Section 6 part A. `render_water` keeps its triangles sorted by exact level and
32 m cell, with each level's range and bounds and each cluster's bounds
(`index_render_water`); candidates, queries and the upload read those, not the
triangles. The CPU-blocking `GL_QUERY_RESULT` read is gone: after the opaque
scene, one `GL_SAMPLES_PASSED` query per candidate level is issued and the
mirror capture and the surface draw run under `glBeginConditionalRender(...,
GL_QUERY_NO_WAIT)`, so the GPU skips a hidden level, and draws whenever the
result is not yet known. Nothing from an earlier frame is used.

Three ways compared on the same build, `--place` quick runs (2 s, 2 repeats,
steady phase p50/p95 ms, release, 1920 x 1080, run back to back; a run made
earlier on the hot GPU is not comparable). `QRY` is `query_read`, CPU is the
benchmark's CPU column (without QRY), REF is GPU time of the reflection stage:

| view | blocking read | draw every frustum candidate | conditional (shipped) |
|---|---|---|---|
| Mollerup play: frame | 12.4 / 13.7 | 34.9 / 37.1 (9 captures) | 11.5 / 12.8 (10.2 / 11.6 final run) |
| Mollerup play: CPU, QRY, REF | 4.1, 8.0, 8.4 | 34.8, 0, 30.2 | 11.4, 0, 7.1 |
| Mollerup pond: frame | 4.6 / 5.9 | 4.0 / 4.7 | 4.1 / 4.9 (3.5 / 4.2) |
| Hedensted play: frame | 8.0 / 9.0 | 31.5 / 32.6 (5 captures) | 7.1 / 8.0 (6.6 / 7.3) |
| Hedensted pond: frame | 21.1 / 22.5 | 31.1 / 32.0 | 21.6 / 22.6 (18.5 / 19.3) |
| Hedensted sweep: frame | 7.8 / 29.4 | 6.8 / 53.6 | 6.9 / 29.2 (6.4 / 25.7) |
| Marienlyst (no water) play | 3.8 / 4.5 | 3.8 / 4.5 | 3.8 / 4.5 (3.5 / 4.3) |

Drawing every candidate is the worst: most frustum candidates are hidden behind
terrain and trees (9 captures at Mollerup's play view, 1 visible). The blocking
read cost 8-17 ms of CPU stall while the GPU sat idle waiting for the CPU to
submit the mirror; conditional rendering removes the stall and the bubble, so
frames are 0.4-1.2 ms shorter and the sweep tails shorter, at the price of the CPU still submitting
the hidden mirrors (its CPU column rose from 3-4 ms to 7-11 ms where several
candidates are hidden; that CPU time was the stall's counterpart). Marienlyst
agrees across all three within noise (negative control). Polling for
availability was not kept: a same-frame query is never ready when the CPU asks.
Cropping the mirrors (the rest of section 6) is what should shrink those
submissions; re-measure after it. New counters: `reflection_levels` (candidate
levels reaching the mirror stage) next to `captures`; the target pool grows to
the most levels captured in a frame and is trimmed after 240 idle frames.
Raw runs: `build/release/benchmarks/refl-a-{before,mode0,mode1,mode2,final}`
(mode 0/1/2 = blocking/draw all/conditional).

## Cropped mirrors (2026-11-19)

Section 6 part B. Each mirror captures only the picture rectangle its water's
lookups reach (clipped footprint, widened by the most `water.frag` can tilt or
smear the lookup, plus two texels), through a crop matrix on its projection, so
terrain, trees, grass and props are selected by the tighter frustum. A rectangle
that cannot be bounded captures the whole picture. Before is the same build with
every rectangle forced to the whole picture (part A's behaviour). Quick runs
(`--ms 2000 --repeats 1`, release, 1920 x 1080), two runs of each executable in
alternating order, steady p50 / p95 ms; CPU excludes query waits. Marienlyst (no
water) agrees between builds: play 5.48 / 5.54, sweep 5.04 / 5.05 (one outlier
before run of 33 ms excluded).

| View | Frame before > after | CPU | GPU | REF | Mirror px | Mirror trees |
|---|---|---:|---:|---:|---:|---:|
| Mollerup play | 16.6 / 17.6 > 9.9 / 11.5 | 16.5 > 9.8 | 16.6 > 8.0 | 9.8 > 1.8 | 129,600 > 500 | 20,651 > 1,930 |
| Mollerup pond | 5.8 / 7.0 > 4.6 / 6.1 | 5.7 > 4.5 | 5.9 > 3.9 | 2.1 > 0.6 | 14,400 > 4,949 | 286 > 7 |
| Hedensted play | 9.3 / 10.6 > 9.2 / 10.5 | 9.2 > 9.1 | 9.2 > 9.2 | 0.17 > 0.16 | 72,000 > 248 | 13,399 > 1,263 |
| Hedensted pond | 29.4 / 30.7 > 20.9 / 21.5 | 29.3 > 20.8 | 29.6 > 20.8 | 13.9 > 5.1 | 28,800 > 9,950 | 11,522 > 3,130 |
| Hedensted sweep | 9.0 / 43.6 > 9.1 / 23.8 | 8.9 > 9.0 | 8.9 > 9.1 | 0.08 > 0.09 | 28,800 > 180 | 4,804 > 512 |

Captures per frame are unchanged (hidden candidates are still submitted under
conditional rendering) but are tiny. Draw all cropped candidates against
conditional rendering, same session, alternating order (frame p50 / p95; GPU):
Mollerup play 8.88 / 10.82 cond against 9.85 / 13.73 all (GPU 4.7 / 9.6);
Hedensted play 6.83 / 8.82 against 13.68 / 15.31; Hedensted pond 17.14 / 20.87
against 20.97 / 23.90; sweep 8.33 / 21.08 against 9.88 / 24.47. Where the
candidate shows (Mollerup pond, sweep) drawing all is within noise (3.25 against
3.48 ms), so conditional rendering stays. The GPU throttles, so compare only
rows from one table. Raw: `build/release/benchmarks/refl-b-*` (before/after) and
`refl-c-*` (cond/drawall); their frozen snapshots were deleted to save disk.

## Rough motion fix (2026-11-19)

`blade_min_pixels` 0 against 1.5 on Marienlyst, full app, 1920 x 1080, steady
p50 frame ms: play 2.93 / 2.58, sweep 2.87 / 2.53, walk_windy_dusk 3.62 / 3.22,
play_40 19.1 / 17.5 (one repeat, noisy: no cost from the fix). Motion
measurements (`golfpp-picture-check --motion`) are in the rendering plan's
section 4.

## Grass tiles (2026-11-20)

Section 7 part A. The monolithic RGB32F field texture (12 bytes a half-metre node
for the whole course) is replaced by fixed 32 m tiles of 65 x 65 nodes on the one
global lattice: height R32F, clearance R16 (rounded down), kind R8_SNORM, 7 bytes a
node, in three texture arrays of 248 layers (the 7 MiB budget; the driver allows at
least 256) with a tile table. A draw makes resident the tiles its visible blocks
stand in (misses only, one layer at a time, least recently needed out) before
drawing, and blocks are selected from per-tile height and clearance bounds instead
of by scanning every terrain and overlay chunk per ring and view. The CPU still holds
the full logical field (part B).

Full app, release, 1920 x 1080, `--ms 2000 --repeats 1`, steady p50 ms; the earlier
column is the build just before this change from the previous session (the GPU
throttles: compare only the GPU grass stage, and read the rest as noise).

| | Before | After |
|---|---:|---:|
| Hedensted grass GPU memory (`grass_gpu_bytes`) | 471 MiB | 7.04 MiB |
| Mollerup grass GPU memory | 203 MiB | 7.02 MiB |
| Hedensted place GPU memory (all logical GPU) | 718.6 MB | 232.5 MB |
| Mollerup place GPU memory | 373.4 MB | 168.2 MB |
| Hedensted play frame / GPU / `GRS` | 9.14 / 9.16 / 1.31 | 9.45 / 9.33 / 1.40 |
| Hedensted sweep frame / `GRS` | 9.08 / 1.30 | 9.21 / 1.39 |
| Mollerup play frame / GPU / `GRS` | 9.70 / 8.03 / 1.41 | 8.63 / 8.68 / 1.51 |
| Mollerup sweep frame / `GRS` | 6.53 / 1.37 | 6.41 / 1.47 |

The stage costs about 0.09 ms (7%) more per frame: the vertex shader reads a table
entry, two filtered texels and four clearance texels where it read one filtered
RGB32F texel. Roots and tuft counts are unchanged (main grass instances are equal
before and after). The grass GPU memory is the same for every course because the arrays
are fixed at start-up; it no longer scales with the course. Warm views upload no
tile. Raw: `build/release/benchmarks/tiles{,2}-hedensted`, `tiles-mollerup`.

Uploads while moving (`--tiles`, the 150 m walk at 3 m a frame, 36 turns of 10
degrees, eyes on tile corners, zoom, a pond): a course sends 97 to 115 tiles (30 KB
each, 3 MB) over all of it, at most 27 tiles (0.8 MB) in one frame, once each (a warm
pass sends none). The first frame of a view sends its whole set (up to 32 tiles), so a
teleport costs one such frame and shows no missing grass. A cache just big enough for
one view (tiles evicted and reloaded 112 to 199 times) draws the same pictures; one a
third that size draws every frame in 3 to 4 batches, and its frame cost is within
noise of the others on Hedensted (resident 21 to 24 ms, evicting 21 to 23, batched
21 to 27 ms in the checker's capture loop, readbacks included). At the widest
accepted tuning (reach 100 m, four rings, 800 m; its square alone is 2,601 tiles)
Hedensted's view wants 435 tiles and is drawn in 2 batches, which re-send those tiles
every frame (about 13 MB): 56.5 ms a capture against 21 ms. That is a stress result,
not a shipped setting; it stays correct and within the 7 MiB cache. Rough motion
measures with a 6-layer cache (every frame in batches) equal the resident ones.

## Grass RAM and cache file (2026-11-20)

Section 7 part B. The CPU no longer holds the logical field (570 MB at Hedensted).
The field is baked a 32 m tile at a time (`grass_baker.h`) straight into packed tiles
(`grass_pack.h`: 1 mm global height step with per-tile integer bases, GPU encodings for
clearance and kind, at most 5 bytes a node; a tile no blade can grow in is dropped), written to
a disposable cache file (`<pref>/cache/grass`, `grass_cache.h`, keyed by a hash of every bake
input and the format). The packed field is 101 MB at Hedensted and 49 MB at Mollerup (it
exceeded 64 MiB, so a file is needed); the next load of the same course skips the bake.
Each frame a few tiles round the eye are fetched ahead (4 a frame, the whole set of about 80
when the field or the eye jumped, before any view), only into layers no draw used in the last 12
draws. Decoded CPU tiles: one tile (59 KB with its source).

Full app, release, 1920 x 1080, `--ms 2000 --repeats 1` (the GPU throttles: frame times are
noise, memory is not):

| | Part A | Cold (file written) | Warm (file reused) |
|---|---:|---:|---:|
| Hedensted grass RAM, steady (field index + decoded) | 570 MiB | 0.5 MiB | 0.5 MiB |
| Hedensted grass RAM at load (baker indices) | 570 MiB | 29.3 MiB | 0 (no bake) |
| Mollerup grass RAM at load | 203 MiB | 22.0 MiB | 0.25 MiB |
| Hedensted process peak working set | 1,360 MiB | 839 MiB | 819 MiB |
| Hedensted steady working set | 1,302 MiB | 799 MiB | 777 MiB |
| Mollerup steady working set | 804 MiB | 603 MiB | 600 MiB |
| Hedensted course bake (`bake_ms`, all meshes and grass) | 11.8 s | 14.5 s | 9.3 s |
| Hedensted / Mollerup grass GPU memory | 7.04 MiB | 7.04 MiB | 7.04 MiB |
| Hedensted sweep frame p95 / GPU `GRS` p50 (ms) | 24.75 / 1.43 | 24.13 | 24.36 / 1.43 |
| Mollerup play_40 frame p95 (ms) | 17.17 | 17.25 | 17.22 |

Grass RAM is under 64 MiB at load on every measured course and about 0.5 MiB steady, GPU 7.04
MiB. The grass-only tile bake is 5.1 s at Hedensted cold (dense bake: 3.0 s); a warm load reads
the file's index in 0.02 s. Warm views fetch 0 tiles on their own draws; walking fetches at most
4 tiles (120 KB) a frame ahead of the eye. Without a usable cache file the field stays in memory
(`grass_storage=memory` in the benchmark context): 101 MB at Hedensted, over the RAM gate. Raw:
`build/release/benchmarks/ram-{cold,warm}-{hedensted_golf_klub,mollerup_golf_club}`.

## Stripe-free short turf and night lamp cap (2026-11-20)

Section 8. Fairways and greens lost their mow stripes (`mow`, `mow_across`, `stripe_*`, the
patches on mown grass) for one even colour with world-fixed tuft fibre (`turf_fibre` in
`terrain.frag`: two octaves of hashed noise, colour share, side slope, faded out as the pixel
footprint covers a tuft; the wet shine's lobe widens by the tufts' slope variance and never sees
their normals). Coverage is one float per terrain vertex (`turf`, 1 fairway, 2 green), replacing
three; no new field. Greens are finer and calmer than fairways (`fibre_green_*` against
`fibre_fairway_*` in `lighting.json`).

Night shine, reproduced with `golfpp-picture-check marienlyst_golfklub --turf=before --diagnose`
(hole 1, 360 lines, lower half of the scene; mean luma / p99 / share of pixels at or above 0.9; a
noon green is 0.50):

| Night, putting view | Mean | p99 | Clipped |
|---|---:|---:|---:|
| Before, dry | 0.989 | 1.274 | 68% |
| Before, no mow stripes | 0.996 | 1.124 | 98% |
| Before, no floodlights | 0.242 | 0.402 | 0.1% |
| Before, wet, no `wet_gloss` | 0.694 | 0.884 | 0.7% |
| Before, wet | 0.723 | 0.988 | 3% |
| After (lamp cap, fibre), dry | 0.600 | 0.619 | 0.15% |
| After, wet | 0.451 | 0.555 | 0.15% |

The cause was lamp diffuse on the pale green, not gloss (dry ground has none): two floodlights
reach the cup with 1.59 of light (a noon surface gets about 1.0), the green's 0.68 channel times
that clips, so the green read as flat mint-white. Stripes added only a pattern; `wet_gloss` added
0.03 to 0.1 on wet nights. The fix is `grass.lamp_light_cap` (0.9): the night lights' light on the
ground rolls off towards it from half, keeping its colour (`capped_lamp_light`). Fairway night
brightness is unchanged (0.167 to 0.164); the approach view's p99 falls from 0.94 to 0.58. Raw
pictures: `build/test/turf_review/{before,after}_*` (Marienlyst hole 1; five final-TV pictures and
every scene), `--turf=<label>` in `tooling/picture_check/README.md`.

Motion at 360 lines (Marienlyst, no wind; scene change over the 2x-line reference, ratio below 1
means no extra shimmer): fairway turn 0.50, walk toward 0.71, across 0.63; green turn 0.47, toward
0.62, across 0.60. Alias (rms against the box-filtered reference) 0.017-0.022 before and
0.017-0.025 after; final-TV change when walking over a green rose from 0.0006-0.0012 to
0.0018-0.0019 because the fibre is now texture that slides past. A pond at Mollerup reflecting
fairway and green: alias 0.10-0.11, change ratio 0.5-0.9. Mollerup's fairway run exceeds the
rough's 1.3 limit in the 44-88 m band when walking toward (1.33) with and without fibre: course
relief, not the material.

Cost, release, 1920 x 1080, `--ms 2000 --repeats 1` on Mollerup and Hedensted (the GPU throttles;
frame times are noise-level): steady frame p50 8.87 to 8.57 ms (Mollerup play) and 9.70 to 9.24
(Hedensted play), GPU p50 9.04 to 8.54 and 9.72 to 9.26; no view is slower beyond run noise.
Memory: logical GPU 160.4 to 150.6 MiB (Mollerup) and 221.9 to 207.3 (Hedensted) from the smaller
terrain vertex (12 bytes fewer, 4 more); steady working set 588 to 585 MiB (Mollerup, warm cache)
and 757 to 749 (Hedensted). Raw: `build/release/benchmarks/turf-{before,after,after2}-*`.

## Far tree images (2026-11-20)

Section 9. Whole-model trees missed the budgets by a wide margin on the gaming
reference, so a tree whose projected image error is within 0.5 pixel of the pass's
own picture is drawn as its baked image (renderer/tree_impostor_renderer.h).
Full app, release, 1920 x 1080, `--ms 2000 --repeats 1`, steady p50 ms, before and
after run the same session (the GPU throttles: read ratios, not decimals). TRE is
the scene tree stage, SMAP the sun map, ZOOM the rangefinder pass.

| View | Frame | TRE | Main tree triangles | SMAP |
|---|---:|---:|---:|---:|
| Ry play | 13.69 > 6.52 | 8.42 > 1.20 | 5.54 M > 0.41 M | 0.15 |
| Ry pond | 33.56 > 10.64 | 10.92 > 0.32 (REF 16.8 > 4.3) | 7.76 M > 0.002 M | 0.03 |
| Ry walk, windy dusk | 14.76 > 7.67 | 8.34 > 1.46 | 5.51 M > 0.55 M | 1.79 > 1.79 |
| Ry zoom flag, night | 25.18 > 22.27 | 10.03 > 7.24 (ZOOM 6.91 > 6.90) | 3.37 M > 1.41 M | 0 |
| Blommenslyst play | 13.28 > 6.00 | 7.60 > 0.22 | 5.40 M > 0.03 M | 0.10 |
| Blommenslyst zoom flag, night | 21.19 > 12.64 | 8.02 > 1.14 (ZOOM 3.61 > 1.58) | 4.96 M > 0.02 M | 0 |
| Jelling play | 11.72 > 6.40 | 7.10 > 1.68 | 4.38 M > 0.53 M | 0.23 |
| Jelling pond | 21.68 > 9.61 | 7.78 > 2.00 (REF 9.0 > 2.8) | 4.83 M > 0.73 M | 0.14 |

Verdict against the section 1 gaming-reference budgets (whole-frame GPU p95 3 ms):
trees were 7-11 ms alone and now are 0.2-2.0 ms in play and windy views, a share
the other stages must still shrink around; they no longer dominate. Derived
low-end estimate (not measured): vertex and triangle work scales down with the
GPU, so a 4x slower integrated GPU would have spent 30-45 ms on whole-model trees
at Ry and Blommenslyst and now spends about 1-8 ms; the zoomed pass at Ry (about
28 ms on that estimate) and wind shadow maps (7 ms) would still miss.

Representation: 12 azimuths x 6 elevations of 32-px tiles per model (colour+coverage
RGBA8, normal+depth RGBA8, leaf code R8, mips), 17.7 MiB for 7 looks x 3 variants,
baked in 349 ms when looks or a season change. Grid choice, measured silhouette
error of the nearest view (silhouette displacement over the boundary of a blurred
coverage map, eight times supersampled, as a share of the crown radius, worst over
all species, variants, seasons, shapes, light and views):

| Grid | Worst error | Mean coverage bias |
|---|---:|---:|
| 8 az x 2 el (-10, 35), 64 px | 0.18-0.26 | +12-19% |
| 8 az x 3 el, 64 px | 0.14-0.16 | +5-8% |
| 8 az x 6 el, 40 px | 0.07-0.10 | 0-3% |
| 12 az x 6 el, 40 px (gate subset) | 0.05-0.06 | within 1.3% |
| 12 az x 6 el, 32 px, full sweep | 0.105 | within 1.9% |

Blending four views was tried and dropped (ghost leaves beyond the silhouette).
The shipped `radius_error` is 0.11 (worst measured 0.105); tile size 32 to 128 made
no difference. Wind excess error is at most 0.00033 of the height per m/s (shipped
0.0005). Mean luminance is within 6% of the model per species (single views 0.2 to
2.0 on bare or night crowns, few pixels); mean depth error 0.11-0.15 radii.
A defect was noticed here and fixed in section 10: tree.frag, tree_image.frag and
grass_blade.frag never had their `u_shadow_map` sampler set, so they read texture unit 0
instead of the sun's map (and the numbers above were measured with it).

Remaining tree cost: sun shadow maps in wind (1.8 ms, 1.2 M caster triangles, full
models, redrawn every frame) and the zoomed rangefinder pass (whole models by the
error bound). Raw runs: `build/release/benchmarks/trees-{a,c}-{ry_golf_klub,blommenslyst_golfklub,jelling_golfklub}/`.

## Remaining recovery: shadow cache, terrain ranges, depth-first trees, crowd (2026-11-20)

Section 10. Release, 1920 x 1080, `--ms 2000 --repeats 1`, steady p50 ms, one session (the GPU
throttles: read ratios, not decimals). "Before" is the tree after the shadow-sampler fix
below and before these changes (`build/release/benchmarks/s10-base-ry`); "after" is
`s10-final-ry_golf_klub`.

**Sampler fix (visible change).** `tree.frag`, `tree_image.frag` and `grass_blade.frag` include
`shadow_map_light.glsl`, but only the terrain, tee box and prop programs had `u_shadow_map` set,
so trees and rough read texture unit 0 instead of the sun's map. `shadow_map_pass::bind` now sets
the sampler for every receiver; `golfpp-picture-check` reads it back from every linked program
(a mutation that drops the call fails it: 6 programs unset). Trees and grass now take the sun's
shadows: on the densest cluster at Ry from four sides, the share of pixels that changed by more than
8 grey levels is 0.6-2.9% at noon, 4-24% in low winter sun and 0.6-16% in the evening (trunks shaded
on the side away from the sun, grass under crowns shaded). Matched pictures:
`build/test/shadow_review/{before,after}_<case>_<side>.bmp` and `pair_*.png` (before | after).

| View | Frame | GPU | CPU | TER | TRE | SMAP | ZOOM |
|---|---:|---:|---:|---:|---:|---:|---:|
| Ry play | 6.59 > 5.41 | 6.50 > 5.39 | 6.53 > 5.33 | 2.36 > 1.43 | 1.22 | 0.15 > 0.00 | |
| Ry play, 40 nearby | 16.95 > 10.91 | 6.23 > 5.05 | 16.83 > 10.80 | 1.82 > 0.92 | 1.22 | 0.23 > 0.00 | |
| Ry pond | 10.75 > 9.80 | 10.79 > 10.00 | 10.68 > 9.73 | 3.48 > 3.04 | 0.32 | 0.02 > 0.00 | |
| Ry walk, windy dusk | 7.69 > 5.69 | 7.82 > 5.65 | 7.61 > 5.63 | 1.79 > 1.40 | 1.47 | 1.80 > 0.00 | |
| Ry zoom flag, night | 22.48 > 16.95 | 22.67 > 17.01 | 22.40 > 16.87 | 2.00 > 1.56 | 7.31 > 5.61 | 0 | 9.58 > 6.19 |

**Terrain draw ranges.** The four ground / two overlay range limits resubmitted what the frustum had
culled: Ry play submitted 3.16 M indices for 1.41 M visible (mirror 0.94 M for 0.44 M; pond mirror
8.98 M for 6.07 M). Draws stay in index order (overlay order is untouched; no spatial sort). Index cost
measured at about 0.5 ns, a draw call at about 3 us, so two ranges less than 6,144 indices apart are
one draw, under a cap of 256 / 64 for a pathological view (`render_mesh_chunks.h`, `terrain_renderer.cpp`).
Cap sweep on Ry (play TER GPU, ranges total, draws): 4/2 limits 2.30 ms, 6 ranges, 615 draws; 16 ms 1.75,
24, 651; 64 1.49, 72, 732; 256 1.37, 148, 808; the gap rule 1.46 ms, 86 ranges, 717 draws: within 0.1 ms
of the best with 62 fewer ranges. Submitted indices after: main 1.60 M (visible 1.41 M), mirror 0.52 M
(0.44 M), pond mirror 6.63 M (6.07 M).

**Shadow cache.** The sun's map is a cached image, drawn again when the casters or the fit change,
the sun turns more than `map_sun_degrees` (0.02: a 40 m caster's 460 m shadow under a sun 5 degrees
up ends 0.16 m, a quarter of a texel, off), the camera leaves its texel, or a crown or moving caster has
moved more than `map_pose_texels` (0.25) of a map texel from where the image drew it. Sway is bounded
analytically (`tree_pose_drift.h`: reach x (wind change + wind x 0.52 x seconds); a test replays
tree_pose.glsl against the bound); moving casters are compared against where they were drawn, so a flag's
flutter costs nothing and a slow walk is caught. The floodlights' maps use the same rule. Redraw share of
frames at Ry: 1.1% standing, 9.0% sweeping, 25.6% walking in 10 m/s wind (mean SMAP 0.44 ms against 1.80 ms
each frame); hub night lamps in 10 m/s wind: lamp maps 0.02 > 0.00 ms (the hub has few trees); Ry zoom at night 0.13 > 0.00 ms.
A gentler rule would cache more; a lone 42 m tree
makes the bound conservative for the whole course.

**Trees depth first.** In the zoomed pass the trees were fragment-bound by leaf overdraw: replacing the
lighting by a flat colour cut Ry zoom trees from 6.96 to 1.77 ms. Crowns whose near models would cover
the picture more than `depth_first_coverage` (1.5; 0.05 with night lights on) times are drawn with the
casters' shader first and lit only where the depth is equal (`invariant gl_Position` in both vertex shaders).
At Ry's play views the coverage is 0.23 and the draw is unchanged; zoomed 9.5. Ry zoom trees 6.96 > 3.89 ms
(zoom pass 9.58 > 6.19), hub night lamps trees 2.49 > 0.75 ms (an always-on depth pass cost day views +0.2 ms,
hence the threshold). Picture check: a grove drawn both ways differs in 24 of 142,120 pixels, ties between
cards in one plane.

**Lighting cost.** At night most of the per-fragment cost is the air's scattered light: with `faded` in place
of `hazed` the hub's lamps view fell from 8.1 to 5.5 ms (trees 2.5 > 0.6, terrain 2.2 > 1.5). Not changed (it
is the light the picture shows; a cheaper form would alter the look). By day the terrain's fragment shading
is most of its 2.36 ms at Ry play (a flat colour left 0.62 ms, which also drops the vertex outputs); taking
out one part at a time moved little: the haze, the night lights and the fibre noise none (2.28-2.29 ms),
the sun's horizon and shadow taps 0.36 ms. Skipping faded fibre octaves, the rough's patches outside the
rough, the wet shine when dry and the shadow taps with no map is exact and saved 0.06 ms at Ry play.

**Crowd.** Forty figures and carts at Ry cost 11.5 ms of CPU: the blob shadows' ground queries (2.5-3.6 ms,
now remembered per caster while it stands still: `blob_ground_memo`), prop and marker building (3.1 ms),
hashing 14.6 MB of vertices to find what changed (2.3 ms; four lanes, -2.0 ms) and a 11 MB join of the
opaque and blended lists (skipped when nothing is blended, -1.4 ms). Ry play_40 CPU 16.8 > 10.8 ms. What is
left is rebuilding every figure and cart on the CPU each frame; drawing canonical meshes as instances with a
matrix each (the shadow and mirror views reading the same instances) was not done: it touches the one prop path,
the world-marker runs and their tests. Mollerup play_40 stays at 13.2 ms CPU.

**Derived low-end estimate (not measured).** The shipped picture fixes the TV pipeline at about 0.6 ms of the
Ry play frame at 1080p (camcorder 0.04, tape 0.16, TV 0.40); it scales with the window's pixels, not the
scene's. A GPU four times slower (an assumption, as in the tree section) would run Ry play, now 5.4 ms, in
about 22 ms, 1.3 times the 16.67 ms interval; Ry zoom at night (17.0 ms) in about 68 ms, the pond (10.0 ms)
in about 40 ms. The phases that remain largest on that estimate: rough grass (1.5 ms), terrain (1.4-3.0 ms)
and, at night, the air's scattered light.

## Full-app run after section 10 (2026-11-20)

Same protocol as the 2026-11-19 baseline (all 148 views, three repeats, 1920 x 1080, 640 x 360 scene, swap
interval zero, release, AC, NVIDIA 610.47), on the tree with sections 4-10. Raw frames, reports and the
frozen executable are in `build/release/benchmarks/after-section-10/`. **Gaming-reference budgets still FAIL on every
course**: 444 view/repeats measured, 444 with a failed budget, none incomplete. Failures by budget (views of 444,
baseline in brackets): CPU p95 440 (444), GPU p95 436 (444), GPU p99 420 (444), frame p95 381 (444),
frame p99 261 (420), solo dynamic upload 226 (330), steady working set 16 (213), load peak 16 (120); the grass GPU,
grass RAM, logical GPU and uniform-miss gates, which failed before, now pass. Worst view per place, worst
steady p95 / p99 across its repeats (budgets 5 / 8 ms frame, 2.5 ms CPU, 3 ms GPU p95):

| Place | Worst view | Frame p95/p99 ms, 2026-11-19 > now | CPU p95 | GPU p95 | Steady / GPU MiB | Budgets |
|---|---|---|---:|---:|---|---|
| blommenslyst_9_holes | play_40 | 33.4/35.6 > 26.0/33.2 | 33.3 > 23.8 | 16.9 > 9.6 | 433 / 83 | FAIL |
| blommenslyst_golfklub | play_40 | 41.4/42.7 > 22.3/26.1 | 41.2 > 22.0 | 33.4 > 14.9 | 760 / 175 | FAIL |
| hedensted_golf_klub | play_40 | 83.7/88.4 > 16.8/17.9 | 81.4 > 16.6 | 48.4 > 14.5 | 956 / 225 | FAIL |
| hedensted_pay_play | zoom_flag_night | 56.5/61.1 > 14.6/16.1 | 56.3 > 14.5 | 22.2 > 14.7 | 514 / 108 | FAIL |
| himmerland_new_course | zoom_flag_night | 41.9/43.8 > 16.6/55.6 | 41.7 > 16.5 | 37.1 > 16.7 | 1007 / 245 | FAIL |
| himmerland_par_3 | zoom_flag_night | 51.3/54.4 > 15.4/16.9 | 51.2 > 15.3 | 38.5 > 15.2 | 721 / 165 | FAIL |
| horsens_golfklub | play_40 | 42.7/45.7 > 14.3/15.1 | 42.6 > 14.1 | 30.7 > 12.1 | 1014 / 253 | FAIL |
| horsens_par_3 | play_40 | 35.7/37.9 > 25.8/33.8 | 35.5 > 25.5 | 15.9 > 18.7 | 450 / 86 | FAIL |
| jelling_golfklub | play_40 | 48.2/50.0 > 17.3/18.4 | 48.1 > 17.1 | 30.3 > 14.9 | 906 / 210 | FAIL |
| jelling_par_3 | play_40 | 39.5/43.7 > 16.0/17.8 | 39.3 > 15.9 | 24.9 > 14.3 | 453 / 86 | FAIL |
| kalo_golf_club | play_40 | 49.6/51.9 > 17.8/23.8 | 49.5 > 17.6 | 29.9 > 13.4 | 851 / 201 | FAIL |
| kalo_par_3 | play_40 | 43.1/51.0 > 15.7/16.6 | 43.0 > 15.5 | 18.4 > 10.1 | 401 / 78 | FAIL |
| marienlyst_golfklub | play_40 | 38.1/45.7 > 16.5/17.2 | 38.0 > 16.3 | 18.9 > 11.8 | 354 / 72 | FAIL |
| mollerup_golf_club | play_40 | 86.0/104.3 > 20.8/22.6 | 85.3 > 20.6 | 59.0 > 12.1 | 736 / 168 | FAIL |
| mollerup_par_3 | shot_rain | 34.7/39.7 > 33.4/34.1 | 34.6 > 33.2 | 18.3 > 9.9 | 386 / 76 | FAIL |
| odense_golfklub | play | 45.6/48.9 > 45.8/47.0 | 44.4 > 45.7 | 24.7 > 12.5 | 732 / 191 | FAIL |
| odense_pilebanen | play_40 | 54.2/61.6 > 12.5/13.7 | 53.7 > 12.3 | 21.2 > 10.8 | 438 / 104 | FAIL |
| ry_golf_klub | zoom_flag_night | 64.9/69.5 > 19.2/20.8 | 64.8 > 19.1 | 65.0 > 19.0 | 833 / 218 | FAIL |
| practice_ground | plaza_40 | 40.4/44.3 > 12.7/14.7 | 40.1 > 12.6 | 21.6 > 8.9 | 385 / 92 | FAIL |

Frame p95 fell 2-6x at most places (Hedensted 83.7 to 16.8 ms, Mollerup 86.0 to 20.8, Ry 64.9 to 19.2); the
worst views are now the 40-nearby crowd and the zoomed night flag. Outliers to read with care: Mollerup par 3
(shot_rain, repeat 1: p50 4.3 ms, p95 33.4 ms) and Odense golfklub (play, repeat 1: p50 30 ms against GPU 11 ms)
had one repeat of CPU stalls the other repeats do not show, Himmerland new course has a p99 of 55.6 ms; the cause was not
investigated. The estimated 60 Hz multipliers are in `places.csv`. Low-end performance remains estimated, not measured.

## Remaining cost investigation (2026-11-20)

Read-only investigation (plan section 1). Release, 1920 x 1080, 640 x 360 scene, swap interval 0, AC, one
session; a temporary probe (cycle-counter timers around every GL call and the renderer's and `make_frame`'s
stages, plus switches that replaced state read-backs by tracked values, a `glMultiDrawElements` terrain path
and shader variants) was built, used and removed again. Runs: Ry, Odense golfklub and Mollerup (2 s a view),
Ry A/B in alternating pairs; medians unless stated. Frame and GPU times drift with the GPU's temperature, so
only same-session pairs are compared.

**What the CPU failures are.** Solo play CPU is mostly waiting, not work. `glGetIntegerv(GL_VIEWPORT)` in
`grass_renderer.cpp`, `renderer_scene.cpp` (before the trees), `reflection_pass.cpp` (framebuffer binding, viewport,
front face, `glIsEnabled(GL_SCISSOR_TEST)`) and `water_renderer.cpp` read state the NVIDIA driver thread owns, so
the app thread blocks until the driver has caught up with the GPU queue. Time in those calls per frame: Ry play 1.2
ms, Odense 1.5, Mollerup 1.7, Ry pond 3.7, Ry zoom_flag_night 6.3-6.9 (of 8.0 ms submission). Replacing them by
values the renderer already knows (three alternating pairs at Ry, steady `REN` ms before > after): play
2.7-3.0 > 1.0-1.2, pond 6.5 > 1.3, sweep 2.8-3.0 > 1.0-1.4, walk at dusk 3.1-3.7 > 1.0-1.4, zoom at night 9.5-9.7 >
1.2. Frame and GPU times did not change (play 3.4-3.7 against 3.6-3.7, pond 7.3 both, zoom 12.8 both): the wait
moved into `SwapWindow` (`SWP` 0.03 > 1.6-1.9 ms), which the CPU gate excludes. The same stalls turn any
GPU contention into CPU stalls. Solo `play` after the change (CPU p50 / p95, ms): Ry 1.71 / 2.28, Mollerup 2.03 /
2.47, Odense 2.21 / 2.80 (before 3.63 / 4.36, 3.84 / 4.58, 4.42 / 5.06). Its parts at Ry: update 0.04, render
data (`MRD`) 0.65, submission 1.0.

**Solo remainder.** `MRD` 0.5 of its 0.55 ms is `drawn_cart` for the golfer's own cart (`cart_on_ground` samples the
ground 4 x 9 times for the wheels and again at the tyres, about 70 `terrain_height` calls, every frame, 0.49 ms
a cart). The crowd benchmark injects its carts after `make_render_data`, so it never pays this: online each nearby
cart costs it. Submission after the change (Odense play): mirror tree selection 0.36, the cart's 13.5 k prop
vertices rebuilt 0.21, trees 0.16, markers 0.08.

**Draw submission.** On this driver a draw is 17 ns on the app thread (Ry play: 694 draws, 118 + 114 + 462 of
them array, element and instanced, 2,020 binds, 991 uniform sets: 0.07 ms in all; Odense 1,800 draws, Mollerup
1,378). `glMultiDrawElements` for the terrain's 114 ranges cost 2.0 ms on the app thread (18 us a range), took
submission from 1.0 to 3.0 ms and left terrain GPU time unchanged (0.58-0.67 ms): the driver queues plain draws
but not multi-draws. Rejected. Trees are instanced draws with per-bucket attribute pointers and uniforms, the
markers change stencil, depth and offset per run, and props are 29 draws: nothing else can be multi-drawn.
Per-draw cost on other drivers is unmeasured (only the RTX).

**Crowd (`play_40`, `plaza_40`).** Submission 8.0-11.8 ms against 1.0-1.6 solo; GPU +0.1-0.3. At Ry (nosync): carts and
bags 3.95 ms (41 carts, 232 k prop vertices of 36 bytes, about 5,500 a cart, 17-20 ns a vertex), figure and ball
markers 1.8 (124 k vertices of 28 bytes, about 3,000 a figure), change detection over the 8.4 MB of props 1.0, marker
upload 0.3, the sun shadow map's CPU 0.8 (moving casters redraw it every frame). The hub's plaza_40 is the same
(3.7, 1.75, 0.93, 0.34, 0.74). The injected crowd stands still (50-66 KB changed a frame), which is why the blob
shadows' ground queries (2.5-3.6 ms before the memo) do not appear: a walking crowd would repay them and upload the
full 12 MB. Instancing estimate (derived, not built): about 8 matrices a cart and 14 a figure is 60 KB instead
of 12 MB, so building drops from about 7 ms to 0.5-0.8; the prop path, marker runs, shadow and mirror views
read the same instance lists (the fix list says what it touches). Dynamic-upload failures are cold frames:
steady `play` frames upload 16-66 KB (Ry walk at dusk 260 KB), the first frame of a view uploads 0.4-2.4 MB and
`play_40` 13.8 MB, and the gate counts every phase.

**Outliers.** Odense golfklub play (after-section-10 repeat 1: p50 30 ms with GPU 11) ran on battery (SDL power
state 1; the other repeats 3-4); with AC it is p95 4.0-4.7 ms, and Mollerup par 3 `shot_rain` p95 2.8 (was 33.4:
a 6 s burst of 28-39 ms submission with 2 ms GPU, which the state read-backs amplify), Horsens par 3 p99 9.7-10.8
(in the earlier run its repeat 3 was slow in every view, p95 about 10 ms), Himmerland new course p99 12.9 in
repeat 2 and 17.6 in repeat 1, where `SwapWindow` stalled about a second twice and submission once (pond, sweep); the
earlier 55.6 ms p99 was 50-60 ms stalls in pond and play_5. None reproduces in a second repeat of the same binary: environment
(battery, another process or the GPU's scheduling), not renderer cost. Runs: `build/release/benchmarks/s1-outliers-*`.

**Zoomed night flag (Ry, GPU 12.8 ms).** Trees 7.0 (main 4.14, zoom 2.86; 1,564 + 1,427 instances, 1.41 M + 1.26 M
triangles, each drawn twice by depth-first), terrain 1.8, grass 1.7, shafts 0.8, TV, tape and camcorder 0.4. All 16 light
slots are in use. Scattered light off: 12.82 > 10.55 (terrain 1.14 > 0.35, trees 4.13 > 3.49); surface night light
off: 12.82 > 12.32. Flat tree fragment colour (the alpha test kept): 4.14 > 3.96 and 2.86 > 2.64, so the trees are
vertex and raster bound (unindexed lists), not lighting bound; depth-first off was worse (15.3 > 16.2 in a
throttled pair). The eyepiece throws away what the main pass drew in its circle (radius 0.9 of half the height: 36% of
the scene's pixels) and the zoom pass is scissored to the circle's square (46%). Hub `night_lamps`: scattering off
4.42 > 3.10 (terrain 1.37 > 0.42); in a throttled pair 5.17 against 3.10 again.

**VRAM (Ry, ledger 227 MB).** Buffers 188.6 MB: terrain vertices 48.1 (632 k x 76 bytes) and indices 14.9, material
overlay vertices 87.1 (1.146 M x 76) and indices 5.9, so the static meshes are 155.9 MB (83% of buffers); 12 bytes of each vertex
(`map_color`, for the printed course map) are uploaded and never read by a shader. Textures 37.4 MB (two 8.3 MB and one 2.1 MB, probably the far-tree
images; grass tiles 7.3), renderbuffers 2.5. Observed WDDM dedicated peak across the 12 places whose sample began
before the course loaded: 55 MiB (the process before any course) + logical + 31-37 MiB (a constant 35 whatever the
course: driver-side context, pipeline and query memory), so staging and in-flight frames are not course-sized. Not
measured: old-course resources across a course change (the benchmark starts a fresh process per course and has
no scripted travel; the static meshes are respecified in place by `glBufferData`, which orphans the old storage
until the driver retires it).

## State read-backs removed (2026-11-20)

Plan section 2, first fix. The viewport, framebuffer, front face and scissor reads in `grass_renderer::draw`,
`renderer::render_scene` (trees), `reflection_pass::draw` and `water_renderer::copy_beneath` are replaced by the
view's `draw_target` (`renderer/draw_target.h`); `state_readbacks` counts what remains (the once-per-bake saves in
`tree_impostor_renderer::begin_bake`) and the benchmark and `golfpp-picture-check` profile check gate it at zero in
warm stationary views (the grass tile store no longer reads its unpack state: it sets the pixel store and puts it back to
GL's defaults). Same session,
quick runs (2 s a view, 2 repeats, release, steady phase, mean of repeats), CPU p50 / p95 ms before > after:

| Place, view | CPU p50 | CPU p95 |
|---|---|---|
| Ry play | 3.88 > 1.80 | 5.79 > 2.02 |
| Odense golfklub play | 5.07 > 2.70 | 5.71 > 3.35 |
| Mollerup play | 5.47 > 2.51 | 7.57 > 2.98 |
| Ry pond | 6.54 > 2.13 | 7.28 > 2.36 |
| Ry zoom_flag_night | 11.50 > 4.45 | 12.24 > 5.39 |

Frame and GPU time did not improve (Ry play frame p50 3.98 > 3.88, GPU 3.06 > 3.82 with the GPU warmer in the second
run; pond frame 6.63 > 6.95, zoom 11.61 > 12.10): the wait moved into `SwapWindow`, as predicted. Runs:
`build/release/benchmarks/s2a-before` and `s2a-after`.

## Cart ground fit remembered (2026-11-20)

Plan section 2, second fix. `drawn_cart` keeps each cart's fit (`cart_on_ground` or `cart_rolled`) in the app-owned
`cart_fit_memo` (`core/render_gear.h`), keyed on owner, position, yaw, condition, fall angle, model name and the area's
revision (static anchors and terrain render revision, as `blob_ground_memo`); `find_drawn_cart` for a strapped bag
hits the same entries, and carts no longer in the world are dropped. Same session, quick runs (2 s a view, 2 repeats,
release, steady phase; the before build only forced a miss), CPU p95 ms per repeat, and `MRD` p50:

| Place, view | CPU p95 | `MRD` p50 |
|---|---|---|
| Odense golfklub play | 2.01, 2.65 > 1.63, 2.23 | 0.52-0.62 > 0.04 |
| Odense golfklub play_5 | 3.09, 3.77 > 2.95, 3.44 | 0.53-0.64 > 0.04-0.05 |
| Ry play | 2.00, 2.31 > 1.20, 1.23 | 0.63-0.75 > 0.04 |
| Ry play_5 | 3.11, 3.72 > 2.29, 2.56 | 0.69-0.82 > 0.04-0.05 |

`play_40` was unchanged (10.3-12.3 ms) because the benchmark then added its carts after `make_render_data`; they now
go through the real path ([walking crowd benchmark](#walking-crowd-benchmark-2026-11-20): `MRD` 0.04-0.07 ms parked,
4.0-4.2 for 5 moving carts, 15-17 for 40). The unit test
covers a parked cart returning its kept fit, a moved or turned one fitted afresh and equal to a fresh frame's. Runs:
`build/release/benchmarks/cart-before-*` and `cart-after-*` (snapshots deleted).

## Walking crowd benchmark (2026-11-20)

Plan section 2, "make the crowd benchmark honest". The standing views stay as they were (their carts now go
through the real cart path, so their parked fits are remembered: Ry `play_40` uploads 18 KB a frame, not 50-66);
`play_40_moving` and `plaza_40_moving` are new. Quick runs, release, 1920 x 1080, AC, 2 s a view, steady phase,
median of each repeat (ms): `build/release/benchmarks/walk-crowd` (Ry, Odense) and `walk-crowd-hub` (practice
ground; snapshots deleted).

| View | Frame | CPU | `MRD` | `REN` | `UPL` | GPU | Uploaded a frame |
|---|---|---|---|---|---|---|---|
| Ry `play_40` | 8.3, 10.8 | 8.2, 10.7 | 0.06 | 8.1, 10.5 | 0.01 | 2.5 | 18 KB |
| Ry `play_40_moving` | 25.1, 30.7 | 24.9, 30.6 | 15.4, 19.1 | 9.5, 11.4 | 0.6, 0.7 | 5.6 | 12.9 MB |
| Odense `play_40` | 10.1, 10.5 | 10.0, 10.4 | 0.07 | 9.9, 10.2 | 0.01 | 2.9 | 51 KB |
| Odense `play_40_moving` | 28.2, 30.6 | 28.1, 30.4 | 17.3, 18.6 | 10.8, 11.6 | 0.65 | 6.9 | 12.9 MB |
| Practice ground `plaza_40` | 9.4, 10.2 | 9.3, 10.1 | 0.05 | 9.2, 9.9 | 0.00 | 2.1 | 13 KB |
| Practice ground `plaza_40_moving` | 14.4, 15.2 | 14.3, 15.1 | 3.3, 3.7 | 10.8, 11.2 | 1.6, 0.75 | 4.1 | 12.3 MB |

Per-stage CPU (mean ms a frame, steady phase, one repeat, a temporary timer probe since removed; `.` = standing,
`m` = moving):

| Stage | Ry . / m | Odense . / m | Practice ground . / m |
|---|---|---|---|
| carts' ground fit, `add_carts_and_bags` (`MRD`) | 0.02 / 14.1 | 0.02 / 15.4 | 0.02 / 3.9 |
| blob shadows' ground queries (`MRD`) | 0.01 / 2.5 | 0.01 / 2.7 | 0.01 / 0.6 |
| prop batch, carts and bags (`append_carts_and_bags`, renderer) | 4.2 / 4.4 | 4.9 / 4.9 | 5.9 / 6.0 |
| prop change hashing and upload (`prop_renderer::stage`) | 1.3 / 1.7 | 1.4 / 2.8 | 1.7 / 2.6 |
| avatar and ball markers, build (`build_world_marker_batch`) | 1.9 / 2.0 | 2.2 / 2.2 | 2.8 / 2.8 |
| marker upload (`world_marker_renderer::stage`) | 0.36 / 0.46 | 0.40 / 0.47 | 0.51 / 0.69 |

Five moving carts cost 3.4-3.6 ms of ground fit and 0.56-0.59 of blob shadows (`MRD` 4.0-4.2); 40 cost 14-15 and
2.5-2.7. The fit is the cart memo's miss: 0.35-0.7 ms a cart on a real course (the nearer five cost more each),
about 0.1 on the practice ground's flat scripted terrain. Standing crowds through the memo cost `MRD` 0.04
(5 carts) and 0.07 (40); a moving crowd never hits it. The sun shadow map and mirror see moving casters, and GPU time
doubles (2.5 > 5.6 at Ry), partly shadow-map redraws.

What it means: a walking crowd of 40 is 25-28 ms of CPU on a real course, 14 on the practice ground, against 8-10
standing. Instancing the props and markers (about 8 ms: 4.4-6.0 build, 1.3-2.8 hash and upload, 2.0-2.8 markers,
0.5 upload) is the half of it that stands still too; the other 3.9-17.5 ms is
`MRD`'s ground queries (cart fits, blob shadows), which instancing does not touch. Skipping unchanged entities
saves nothing here: every figure and cart moves every frame, so it would pay only for the share of an online crowd
that stands (unmeasured; a standing crowd already uploads 13-51 KB).

## Ground queries indexed (2026-11-20)

Plan section 2, "make the ground query cheap". A `sample_area` call cost 5.0-7.4 us on real courses (0.4-0.5 us on
the practice ground), so a cart's ~70-sample fit was 0.35-0.5 ms. Profiled in a test over every shipped course
(points along the holes), the cost was three scans, not one: the ground mesh (2.4-3.9 us: its spatial index was
capped at 256 cells a side, 20-40 triangles a cell on 1-1.7 M triangles, and `scan_triangle` copied two
candidate samples per triangle), the holes (1.3-1.4: every hole's index looked up and its state built per query), and the
zones (1.1-2.5: a box test per zone, 100-180 of them). Fixed, all exact (same winner, same bits):

- Ground mesh index cap 256 > 1024 cells a side (about one triangle a cell), `scan_triangle` updates the scan
  state in place and works out the section distance only for a candidate.
- `sample_terrain_inside` returns early outside a hole's padded box, before building any state.
- `winning_zone_at` computes a zone's edge distance only to break a tie of priority; `zone_hit::depth`, read by
  nobody, is gone.
- `surface_index` (`play_area::surface`, built with the ground): a grid over the hole and zone boxes, each cell
  listing its candidates ascending; `sample_area` and `water_level_at` look through it. The scans stay as the
  fallback for an area built without one, and are the reference of the test.

`sample_area` 5.0-7.4 > 1.8-2.1 us (Ry 5.4 > 1.8, Odense 5.0 > 2.0; ground 2.4 > 0.35, zones 1.4 > 0.1, holes 1.3 >
0.5). Test: `the surface and ground indexes answer as a scan ...` (`course_tests.cpp`) compares the indexed and the
scanned answers, bit for bit, at random points in and around every shipped course. Quick runs, release, 2 s a view,
steady phase, two repeats, back to back in one session (`build/release/benchmarks/ground-index-before`, `-after`;
snapshots deleted):

| Course, view | `MRD` p50 before | after | CPU p50 / p95 before | after |
|---|---|---|---|---|
| Ry `play_40_moving` | 14.4, 18.4 | 2.1, 2.5 | 23.3-29.8 / 24.3-33.3 | 12.9-15.3 / 13.8-16.4 |
| Odense `play_40_moving` | 16.4, 18.4 | 3.4, 3.8 | 27.3-30.7 / 28.2-31.6 | 15.0-16.6 / 15.9-17.9 |
| Ry `play` | 0.034-0.046 | 0.024-0.025 | 1.0-1.2 / 1.3-1.4 | 0.95-0.98 / 1.15-1.16 |
| Ry `play_5` | 0.041-0.050 | 0.031-0.035 | 1.8-2.0 / 2.1-2.2 | 1.8-2.3 / 2.0-3.0 |
| Odense `play` | 0.035 | 0.027-0.029 | 1.4 / 1.6 | 1.4-1.6 / 1.6-1.7 |
| Odense `play_5` | 0.042-0.047 | 0.036-0.041 | 2.3-2.6 / 2.6-3.2 | 2.6-2.9 / 2.9-3.6 |

The walking crowd of 40 loses 11-15 ms of `MRD` (cart fits and blob shadows); what remains is the instancing work.
`play_5` CPU is within the session's noise (its `MRD` is 0.04 ms either way).

## Crowd drawn as instances (2026-11-20)

Plan section 2, "draw the crowd as instances". Every piece of a cart, bag, club, held item and figure, and every
ball, is a `mesh_instance` (a 68-byte affine matrix, colour, alpha and glow floor) of one of three canonical meshes
uploaded once (`instance_meshes.h`); `prop.vert`, `world_marker.vert` (`INSTANCED`) and the sun shadow map's
`shadow_map.vert` (`MODEL_CASTER`) place them, one `glDrawArraysInstanced` per mesh and run with the attribute
pointers moved to the first instance (no base instance). The per-frame vertex rebuild (357 k vertices, 12 MB hashed
and uploaded) is gone; the marker batch keeps world-space triangles only for what is not a placed mesh or is uploaded
once (cups, flags, poles, collectibles, shoes at address, tee boxes). A frame's instance buffer is hashed whole and
sent only when it changed.

Quick runs, release, 1920 x 1080, 2 s a view, steady phase, mean over four repeats of CPU p50 / p95 and GPU p50 (ms)
and the bytes uploaded a frame, before > after. Order BEFORE, AFTER, AFTER, BEFORE would have been the ideal; the
runs went A B B A (`build/release/benchmarks/abba-*`, snapshots deleted) because the GPU throttles: an earlier
non-interleaved pair had the second run at 1073 MHz against 1282 MHz and is not used. Mean P0 clock of the four
runs: before 1139 and 1155 MHz, after 1141 and 1065.

| View | CPU p50 / p95 | GPU p50 | Uploaded a frame |
|---|---|---|---|
| Ry `play` | 0.98 / 1.17 > 0.66 / 0.83 | 4.74 > 4.82 | 9 > 9 KB |
| Ry `play_5` | 2.33 / 3.02 > 0.80 / 1.09 | 4.25 > 4.77 | 10 > 10 KB |
| Ry `play_40` | 11.62 / 12.98 > 1.28 / 1.66 | 3.53 > 4.90 | 18 > 18 KB |
| Ry `play_40_moving` | 15.31 / 16.44 > 4.61 / 5.17 | 5.91 > 4.64 | 12,610 > 270 KB |
| Odense `play` | 1.63 / 1.97 > 1.21 / 1.42 | 4.82 > 4.81 | 16 > 16 KB |
| Odense `play_5` | 3.13 / 4.17 > 1.38 / 1.84 | 4.74 > 4.79 | 20 > 20 KB |
| Odense `play_40` | 12.61 / 13.83 > 2.11 / 2.62 | 4.02 > 4.89 | 50 > 50 KB |
| Odense `play_40_moving` | 17.52 / 19.18 > 7.41 / 7.87 | 7.44 > 4.12 | 12,642 > 302 KB |
| Practice ground `plaza` | 0.57 / 0.68 > 0.36 / 0.56 | 3.44 > 3.70 | 8 > 8 KB |
| Practice ground `plaza_5` | 2.11 / 2.82 > 0.44 / 0.57 | 3.31 > 3.69 | 9 > 9 KB |
| Practice ground `plaza_40` | 10.91 / 11.89 > 1.02 / 1.29 | 2.75 > 3.85 | 13 > 13 KB |
| Practice ground `plaza_40_moving` | 14.99 / 16.43 > 4.08 / 5.11 | 4.85 > 3.53 | 12,049 > 260 KB |

`REN` (submission) 8-13 ms of a crowd of 40 fell to 0.9-2.7 (Ry `play_40` 11.49 > 1.19, `play_40_moving` 12.62 >
1.71; the stage's `UPL` 0.8 > 0.02). Draw calls rose by 9 (Ry `play`) to 61 (Odense `play_40`): one instanced draw for each mesh and run instead of one for each run. What the walking
crowd still costs is `MRD`, which instancing never touched: cart fits and blob shadows, 2.6-2.9 ms at Ry and the
practice ground, 4.7 at Odense. The standing crowd of 40 is now 1.3-2.1 ms of CPU against the 2.5 budget (Odense's
p95 2.6 is the only standing view left over it: its `REN` is 2.0 against 1.1 solo, the crowd's instance building).

GPU: the standing 40 and the solo views now have the same GPU time (Ry 4.82 `play`, 4.90 `play_40`: the crowd costs
about 0.1 ms), so the rows above that rose were CPU bound before: their GPU idled between 11 ms frames at a higher
clock, and now it is the frame, at the throttled clock. The passes that changed read the same or lower in the
per-pass timers (`PRP` 0.11 > 0.09, `SMR` 0.04 > 0.05, `SMAP` 0.16 > 0.18 at Ry `play_40_moving`, so the shadow map
is redrawn as often as before); the terrain's and trees' times moved with the clock alone. `golfpp-picture-check`
passes on all 18 courses; no gate other than the CPU and upload ones changed (of the 50 view-repeats of the first
before and after runs, failing the solo upload gate 26 > 4 and the CPU gate 19 > 8).

Pictures: `golfpp-picture-check --crowd=<label>` (`crowd_review_check.h`) draws forty golfers with carts, bags, clubs
and balls, five close ones and, at a pond, fifteen on its bank, at noon, low sun and night, and compares the scene
before picture processing with another run exactly. Captured on the vertex path, then compared after (`ry_golf_klub`,
`odense_golfklub`): 0-17 of 230,400 pixels differ in the standing views at Ry, 62-233 at Odense, 73-557 in the pond
views (largest 0.77 of full scale on a high-contrast edge). Every difference is a single pixel or a handful along a
silhouette (in the views examined, three a course, the largest connected group is 21 pixels, 9 elsewhere), and Odense's pixel counts are the same at low sun
and at night, so it is geometry, not lighting. The cause is how the matrix times the vertex is rounded: the old build
multiplied on the CPU (`-ffp-contract=off`), the shader now multiplies on the GPU with fused multiply-adds, one unit
in the last place, which tips an edge pixel. GLSL 330 has no `precise`, so it cannot be matched; the CPU-side
placement used by tests is the old expression bit for bit (`an instance places its canonical mesh exactly ...`).
The shadow map tracks a moving caster by the corners of each instance's mesh box (`placed_corners`) instead of its
vertices: never fewer movements, so never a stale shadow, at the price of a redraw a little earlier.

## Eyepiece throwaway not shaded (2026-11-20)

Plan section 2, "do not shade what the eyepiece throws away". With the rangefinder up, a mask pass writes the nearest
depth over the circle of the main picture (radius 0.9 of half the height, less `eyepiece_mask_margin_pixels` = 4) and
over the zoom square's corners (beyond the same radius plus 4), so the depth test rejects everything there; terrain
chunks, tree cells and grass blocks whose bounds project wholly inside the circle (2 pixels further in) are not
submitted in the main pass only (`hidden_disc` on the frustum; shadow maps and mirrors never get one). The margin is
not the plan's one pixel: the sun shafts' composite reads depth two pixels away, so a pixel the eyepiece keeps would
otherwise read a masked one. The trees' depth-first decision still counts hidden cells, so a tree is drawn the same
way as before. With a pond that may show in either view, or the eye under water, nothing is masked (`water.frag`
reads a bent pixel up to `refraction_pixels / ripple_slope` away, more than the margin can bound).

Quick runs, release, 1920 x 1080, Ry, 2 s a view, two repeats each, ordered before, after, after, before
(`build/release/benchmarks/eye-*`, snapshots deleted); only the two runs at the matching clocks (1271 and 1293 MHz
mean; the first before ran at 1559, so it flatters before) are a fair pair, and the other four are shown for range.
`zoom_flag_night`, ms, before > after:

| | Frame p50 / p95 | GPU p50 / p95 | CPU p95 |
|---|---|---|---|
| Matched pair (before-2, after-2) | 12.64 / 13.89 > 9.69 / 10.73 | 12.51 / 13.44 > 9.94 / 9.99 | 2.75 > 1.82 |
| All four runs, mean | 11.59 / 12.58 > 9.69 / 10.74 | 11.49 / 12.16 > 9.94 / 9.99 | 2.26 > 1.83 |

The other views of the same runs (rangefinder down, masking never runs) read 0.1-0.4 ms higher in the after runs
at a lower clock (play GPU p50 3.72 > 3.85, pond 6.93 > 7.21, play_40 3.83 > 4.06): clock drift, not the change.

Pictures: `golfpp-picture-check --eyepiece` (also in the default run) draws the eyepiece's picture through the game's
renderer with the mask on and off at the start of every course in four directions and, with water, from four banks of
its largest pond, at noon and night, with trees and wind: every pixel is bit-identical on all courses (Ry and Odense
also before the check was extended). Setting the margin to -6 pixels makes the same run report about 12,200 pixels
differing in every view, so the check can fail.

## Terrain levels for mirrors (2026-11-20)

Plan section 2, "terrain level of detail for mirrors". Each ground chunk gets three coarser index lists on the
same vertices, built at course load (`render_mesh_lod.cpp`, data in `lighting.json`'s `terrain_lod`: 0.2, 0.5 and
1.25 m of error, 0.5 pixel for a mirror); a mirror draws a chunk as the coarsest level whose error spans at most
0.5 pixel of its own picture at the chunk's nearest point. The picture, zoom and shadow maps keep the full ground
(a main-view threshold was not tried: the mirror alone is the plan's target). Chunk borders never change (no
cracks); turf and blade kind never move; colour, normal and sky values move by at most 0.1. A first one-sided
version (never above the full ground) kept 37-51% of the triangles at 0.1-1.5 m; two-sided with the overlay
lifted over the coarse ground by the accepted error (`u_lift_per_depth`) keeps 43-46%, 31% and 24-26% at the three
levels (Ry, Horsens golf klub). The thinning costs 0.9-1.3 s of the course load at Ry and Horsens golf klub
(1.0-1.2 million triangles; 0.6 s were the ground mesh itself).

Quick runs, release, 1920 x 1080, Ry, 2 s a view, two repeats each, ordered before, after, after, before
(`build/release/benchmarks/lod3-*`, snapshots deleted); before is `mirror_max_error_pixels` 0 in `lighting.json`
(the levels are built and uploaded in both, so memory is the same). Pond view, ms:

| | Frame p50 / p95 | GPU p50 / p95 | `reflection_TER` p95 |
|---|---|---|---|
| before (runs 1, 4) | 7.23-7.59 / 7.67-9.66 | 7.06-7.58 / 7.10-9.00 | 1.78, 2.14 |
| after (runs 2, 3) | 6.61-7.34 / 7.64-7.69 | 6.91-7.18 / 6.93-7.23 | 1.19, 1.24 |

GPU p50 of the pond 7.5 > 6.9 ms (-0.6) and the mirror's terrain 1.8-2.1 > 1.2 (-0.6 to -0.9); the other views
(play, walk, zoom: no mirror) read the same in the four runs within clock drift. The plan estimated 7.0 > 5.5;
the mirror's overlay (draped zones, cart paths) stays at full detail (it must lie on the full ground) and so does
every chunk within 31 m of the eye, so about a third of the saving remains.

Memory (logical GPU ledger, MiB, the 256 cap): the coarse lists are 11.5 MiB at Horsens golf klub (215.8 > 227.3)
and 14.2 at Ry (185.0 > 199.2). To make room, `map_color` (12 bytes of every ground and overlay vertex, the
printed map's colour, uploaded and never read by a shader) moved out of the uploaded vertex into
`render_static_mesh::map_colors`: the vertex is 76 > 64 bytes. Per course (after section 10, this tree):
Horsens golf klub 253.4 > 227.3, Ry 217.9 > 199.2, Himmerland new course 244.5 > 220.9, Hedensted golf klub
225.0 > 209.0, the rest 13-24 MiB lower (the instanced crowd and caches also moved the ledger).

Pictures: `golfpp-picture-check --water` (every course with a pond) draws the largest pond from four banks at 25
and 70 m through the whole renderer with the levels and with the full ground: at most 0.05-0.89% of the pixels
differ by more than 0.02 (the far shore's reflection, one-pixel silhouette flips), against 5-32% at 8 pixels of
error, which the check requires to fail. Unit tests (`render_lod_tests.cpp`) check on a hill that every level
stays within its error of the full mesh, keeps its chunk's border edges, keeps a material edge, and that a pass
picks a coarser level only far enough away.

## Tree meshes indexed (2026-11-21)

Plan section 2, "index the tree meshes". A grown tree's triangle list is turned into unique vertices and indices
(`index_vertices` in `tree_model.cpp`): vertices are shared only when all ten floats are bit for bit equal, and
the triangles keep their order, bark first, so depth-first crowns and alpha draw as before. The model's buffers
are a vertex and an element buffer in one VAO, and the main pass, the depth-first prepass, mirrors, zoom, the
sun and lamp shadow maps and the far-tree image bake all draw it with `glDrawElementsInstanced` (GL 3.3; no
base-instance or indirect draw); there is no unindexed path. Both buffers are in the GPU ledger.

Pictures, indexed against the same models unindexed (identity indices, a temporary build): `--tree-images` writes
a bit-identical `tree_images.csv` (15,120 rows of coverage, luminance and depth), the `--shadow-receivers`
pictures of Ry and Mollerup golf club are byte-identical files, and the crowd review's nine scene views (noon,
low sun, night; near, far and pond-bank) of both courses differ in 0 of 230,400 pixels.

Quick runs, release, 1920 x 1080, Ry, 2 s a view, two repeats each, ordered before, after, after, before
(`build/release/benchmarks/tidx-*`, snapshots deleted). The GPU throttled between runs (play GPU p50 3.5 to
5.1 ms from the first to the last repeat), so only the tree stage is compared; ms, mean of the four repeats:

| View | Tree stage p50 | GPU p50 / p95 | Frame p50 / p95 |
|---|---|---|---|
| zoom flag, night | 2.65 > 1.86 (-30%) | 10.0 / 10.4 > 8.5 / 8.9 | 9.9 / 11.0 > 8.6 / 9.4 |
| play | 0.93 > 0.78 (-16%) | 4.1 / 4.3 > 4.3 / 4.3 (drift) | 4.5 / 5.3 > 4.0 / 5.0 |
| pond | 0.24 > 0.25 | 6.9 / 7.0 > 7.4 / 7.5 (drift) | 6.8 / 7.6 > 7.3 / 8.0 |

The plan estimated -25-35% of the trees' time: the zoomed flag meets it, play gains 16% and the pond's trees,
which are few, nothing measurable. Ledger at Ry: 207.98 > 206.90 MB
(-1.09 MB over 7 species of 3 variants).

## Night lights culled per view, not adopted (2026-11-21)

Plan section 2, "cull night lights per view". Tried and removed: no measurable gain. The nearest 16 lights were
still chosen first (so lamp shadow slots and the trees' night flag did not change), then each slot whose reach sphere
missed the view's four side planes (the pyramid from the eye, with no near or far plane: air and sky out there still
scatter into the picture) was emptied in place for that view, so main, zoom and each mirror had their own block.

Lights per view (the nearest 16 of 137 at Ry, 25 at the practice ground, all in the 16 slots): Ry at night main 9
(8-12 as the view turns), zoom 8, mirrors 9-12; hub lamps main 9. A third to a half of the slots were culled.

Pictures were within the capture's own noise: Ry zoom night and hub lamps differ from the uncull run in 57,194 and
41,687 of 2,073,600 pixels by at most 2 levels, against 105,858 and 188,865 pixels by at most 2 between two
runs of the same binary (tape noise). Ry play at day was bit-identical. Vertex-lit pieces (`tee_box.vert`,
`world_marker.vert`, `floodlight.vert`) could in principle differ at a screen edge, since a culled light can still
reach a vertex off screen.

Quick runs, release, 1920 x 1080, Ry and the practice ground, 2 s a view, two repeats, ordered before, after,
after, before (the GPU drifts up about 0.6 ms across the four runs, so the ABBA means cancel it); GPU p50 ms:

| View | Before | After |
|---|---|---|
| Ry zoom flag, night | 7.80 | 7.77 |
| Ry zoom pass alone | 3.24 | 3.21 |
| Hub lamps | 4.99 | 4.98 |

The estimate was 0.3-1.0 ms. A culled light was already cheap: `scattered_from` returns at once when the ray misses
the light's reach (`beam_run`), and `local_light` is a few instructions when `reach` is zero, so the work a cull
removes is the near-empty iterations. The cost is in the 8-9 lights that do reach the view, which no per-view
cull touches. Scattering per pixel from the depth in `sun_shaft_pass` was not started (the plan estimated it nets
0.4-1.0 ms).

## Terrain vertex packed (2026-11-22)

Plan section 2, "pack the terrain vertex". `render_terrain_vertex` went from 64 to 32 bytes (`terrain_vertex.h`): the
normal is `GL_INT_2_10_10_10_REV`, colour and rough, paved, sky are normalized bytes, turf and blades count in steps of
1/127 (0, 1, 2 exact), the eight horizon sines stay bytes. Setters round the way the GPU reads back (normal 0.1
degrees, colour 0.002 at most, fixed steps of the value), so the coarse levels' attribute error
(`chunk_thinner.cpp`, split from `render_mesh_lod.cpp`), the grass field and the cart paths work on the vertex as
drawn. The flight path shares the struct and halves with it. A previous, cut-off agent had written the struct, the
attribute layout and the shader; the rest (users, tests, tooling, split, measurements) was finished here.

Memory (logical GPU ledger, MiB, 300 ms quick run of every place, release, before > after): Horsens golf klub
226.5 > 159.5, Ry 198.4 > 144.1, Himmerland new course 220.1 > 155.0, Hedensted golf klub 208.2 > 149.6, Jelling golf
klub 191.2 > 137.7, Kalo golf club 180.7 > 131.2, Odense golfklub 167.0 > 121.4, Blommenslyst golfklub 152.2 > 108.6,
Mollerup golf club 149.6 > 110.1, Himmerland par 3 144.2 > 110.5, Hedensted pay and play 92.0 > 72.5, Odense
Pilebanen 89.9 > 72.1, Horsens par 3 71.1 > 59.4, Jelling par 3 69.9 > 57.7, practice ground 68.0 > 55.4, Blommenslyst
9 holes 67.6 > 57.4, Kalo par 3 63.4 > 54.2, Mollerup par 3 61.6 > 53.9, Marienlyst 57.1 > 49.6. Ry's process
working set fell 728 > 632 MB and its peak 886 > 706 MB. The plan estimated -57 MB at Ry.

Pictures: the turf review (Marienlyst hole 1, 36 views at noon, low sun, overcast, wet, night and wet night, scene
and TV) drawn by an unpacked build of the same tree (same accessors, 64-byte vertex, old layout and shader) and
by the packed one, in the picture rig's frozen clock: largest channel difference 4 of 255 (3 in scene pictures),
mean 0.07-0.30, no pixel over 3 in the scenes (0.016% of one TV picture). Below visible, and no banding in the
low-sun and night greens. `golfpp-picture-check` on all 18 courses passes, ground checks included. App screenshots
(`GOLFPP_SCREENSHOT`) cannot serve as the before/after: two runs of the same build differ by a mean 2-19 of 255
(clouds and the clock run in real time).

Timing (Ry quick runs, 2 s a view, two repeats, ordered before, after, after, before; GPU p50 ms, mean of the four
repeats, steady): play 3.79 > 3.84 (unchanged, drift 3.2-4.5), pond 7.57 > 6.27 (-17%, lower in every before/after
pairing); CPU p50 unchanged (0.8-1.0 play, 1.5-1.7 pond). The vertex fetch matters where the
mirror draws the ground again.

## Second recovery round result (2026-11-22)

Plan section 2 closed with a full run, `build/release/benchmarks/after-section-2` (release, the tree after the
terrain vertex was packed; same protocol as `after-section-10`: three repeats, 1920 x 1080, 640 x 360 scene, swap
interval zero, AC). It has 501 view/repeats, not 444: the walking-crowd views `play_40_moving` and `plaza_40_moving`
were added. **Budgets still FAIL on every course.** Gate changes made for it: the solo upload gate reads the steady
phase only, a view's first-frame upload is reported apart (`first_frame_dynamic_bytes_max` in `views.csv`), and `SWP`
(`SwapWindow` and every wait that lands in it) is reported beside `CPU` (`cpu_SWP_ms_*` in `views.csv`, `swp_p95`
and `swp_p99` in `places.csv`). Budget numbers unchanged.

Failing view-repeats per gate (after-section-10 in brackets): frame p95 483 of 501 (381 of 444), frame p99 370
(261), GPU p95 501 (436), GPU p99 500 (420), CPU p95 106 (440), solo upload 0 (226 counted every phase; 15 when read
steady, none in this run: the largest steady solo frame uploads 203 KB, the Ry walk at dusk that was 260 KB in
`after-section-10` is 259,848 bytes, under 256 KiB = 262,144 and not over; no trim was needed), steady working set 0
(16), load peak 0 (16), logical GPU 0, grass 0, uniform misses and state read-backs 0. 168 views have a first frame
over 256 KiB (0.3-0.5 MB; the crowd's about 8 MB), reported, not gated.

**CPU is the section's result.** Solo views (no crowd): worst CPU p95 per view 45.7 > 10.8 ms (Mollerup par 3 zoomed
night flag; the next worst 8.2 at Horsens golf klub), median of views 7.2 > 1.0 ms, 295 of 333 pass 2.5 ms (4 did).
`play`: median 6.4 > 0.87, worst 2.13 ms. The 106 CPU failures are `play_40_moving` 54 (4.4-18 ms, the walking crowd;
Jelling par 3 at 18.1 is an outlier), `zoom_flag_night` 34, standing `play_40` 10, `plaza_40_moving` 3,
`shot_rain` 2, `walk_windy_dusk` 2, `play_5` 1. The old standing 40 is 1.3-5 ms of CPU now (25.5 before). Waits now
show as `SWP`: worst p95 7-36 ms in the same views whose GPU is slow, which is the driver blocking in `SwapWindow` on
the GPU, no longer in submission.

**GPU and frame do not compare with `after-section-10`.** This session ran thermally throttled: `gpu-telemetry.csv`
shows the software thermal slowdown active in 95% of samples in both runs and a median graphics clock of 667 MHz
against 1050 MHz in `after-section-10` (temperature to 96 C). `play` GPU p50 is therefore 4.9 ms against 4.2 and the
worst-view GPU p95 is higher at many courses (Hedensted pay and play, Himmerland par 3 zoomed flag 31-39 ms in repeat 1
against 11-15) although no GPU pass got more work (the changes since are the packed vertex, indexed trees, eyepiece
mask and mirror levels, each measured same-session as a saving). Repeats differ by up to 3x in the same view (Ry pond
GPU p95 22.7, 20.2, 9.1). Take the frame and GPU rows as environmental until a cool, same-session pair exists; the
plan's rule (compare only back-to-back runs) stands. Repeating the matrix after the laptop has cooled is the
cheapest way to a clean GPU table.

Worst view per place, worst p95 across repeats (ms; `after-section-10` > now; RAM and logical GPU MiB, now):

| Place | Worst view now | Frame p95/p99 now | CPU p95 (worst view) | Steady / GPU MiB (before) |
|---|---|---|---|---|
| blommenslyst_9_holes | zoom_flag_night | 9.7/10.9 (26.0/33.2) | 23.8 > 6.1 | 264 / 57 (433 / 83) |
| blommenslyst_golfklub | walk_windy_dusk | 14.0/35.5 (22.3/26.1) | 22.0 > 6.3 | 532 / 109 (760 / 175) |
| hedensted_golf_klub | pond | 25.3/33.6 (16.8/17.9) | 16.6 > 6.8 | 734 / 150 (956 / 225) |
| hedensted_pay_play | zoom_flag_night | 31.9/33.9 (14.6/16.1) | 14.5 > 9.4 | 375 / 73 (514 / 108) |
| himmerland_new_course | zoom_flag_night | 18.8/32.2 (16.6/55.6) | 16.5 > 6.4 | 741 / 155 (1007 / 245) |
| himmerland_par_3 | zoom_flag_night | 38.9/44.4 (15.4/16.9) | 15.3 > 4.4 | 483 / 111 (721 / 165) |
| horsens_golfklub | pond | 23.8/34.3 (14.3/15.1) | 14.1 > 8.2 | 737 / 160 (1014 / 253) |
| horsens_par_3 | zoom_flag_night | 27.6/29.9 (25.8/33.8) | 25.5 > 6.0 | 292 / 59 (450 / 86) |
| jelling_golfklub | shot_rain | 22.6/29.3 (17.3/18.4) | 17.1 > 6.4 | 661 / 138 (906 / 210) |
| jelling_par_3 | zoom_flag_night | 24.2/25.6 (16.0/17.8) | 15.9 > 18.1 | 278 / 58 (453 / 87) |
| kalo_golf_club | zoom_flag_night | 24.9/26.3 (17.8/23.8) | 17.6 > 7.1 | 614 / 131 (851 / 201) |
| kalo_par_3 | play_40 | 13.6/17.0 (15.7/16.6) | 15.5 > 5.2 | 280 / 54 (401 / 78) |
| marienlyst_golfklub | zoom_flag_night | 16.1/19.5 (16.5/17.2) | 16.3 > 5.5 | 240 / 50 (354 / 72) |
| mollerup_golf_club | zoom_flag_night | 19.1/21.1 (20.8/22.6) | 20.6 > 7.3 | 529 / 110 (736 / 168) |
| mollerup_par_3 | zoom_flag_night | 11.0/12.9 (33.4/34.1) | 33.2 > 10.8 | 242 / 54 (386 / 76) |
| odense_golfklub | zoom_flag_night | 23.6/26.2 (45.8/47.0) | 45.7 > 8.6 | 609 / 121 (732 / 191) |
| odense_pilebanen | shot_rain | 18.0/21.0 (12.5/13.7) | 12.3 > 5.0 | 384 / 72 (438 / 104) |
| ry_golf_klub | pond | 22.8/29.9 (19.2/20.8) | 19.1 > 6.3 | 671 / 144 (833 / 218) |
| practice_ground | night_lamps | 16.0/16.5 (12.7/14.7) | 12.6 > 4.3 | 275 / 55 (385 / 92) |

Memory fell as the packed vertex predicted: steady working set at most 740 MB (was 1,014), logical GPU at most 160 MiB
(253). The CPU column is the worst view's CPU p95 (`play_40_moving` at most places).

Still failing: GPU p95 and p99 (everywhere, throttled session included), frame p95 and p99 (GPU-bound), and CPU p95 in
the walking 40-figure crowd (4.4-18 ms), the zoomed night flag (CPU up to 10.8 ms on Mollerup par 3, a noisy repeat)
and a few `shot_rain`/`walk` repeats. Budgets are the owner's decision: see the plan's proposal.

## Bare trees: grown branches, twig cards, falling leaves (2026-11-22)

The bare-trees plan, built. Models: space-colonization skeletons by the pipe model (`tree_skeleton.h`), bark
in three orders (6 / 4 / 3 sides). Bark triangles per model, seeds 1-3, against the old trunk, limbs and spokes:
beech 640-686 (672), oak 512-534 (588), birch 498-512 (504), willow 566-606 (672), spruce 510-532 (504),
pine 296-328 (324), scrub 316-342 (300); the new beech hedge 318-386. Cards are unchanged in number (six
triangles a clump); `tree_model_tests` holds every species within 1.2 times the old bark.

Images: each view is drawn 8 times finer and averaged (`tree_bake_resolve`), the leaf code split into leaf share
and crown depth (RG8): 22.5 MiB for 8 looks x 3 variants, baked in about 320 ms. Gate (`--trees`): worst
silhouette 0.069 of the crown radius (shipped 0.110), mean coverage within 1.3%, luminance within 5%, every
species. Getting there: the haze of finest twigs had to keep its coverage inside a widened stroke (it was
resolution dependent: -3 to -7%), clumps had to turn at random about their branch (a level first card made
crowns denser from above than the grid's views: -8% at 60 degrees), and 4 times finer bakes still lost 3%
on willow. A bare crown walked past at 30 and 70 m changes 1.06-1.11 times as much as twice the lines does
(bound 1.3), with the hashed threshold in two-pixel cells (one-pixel cells: up to 1.35).

Cost, release, 1920 x 1080, 2 s a view, one repeat each in the order winter, summer, summer, winter
(`build/release/benchmarks/trees4-*`), GPU tree stage p50 ms, winter (Jan 15) against summer (Jul 1):

| View | Summer | Winter |
|---|---:|---:|
| Ry play | 0.91-0.95 | 0.92-0.96 |
| Ry walk, windy dusk | 1.16-1.27 | 1.22-1.24 |
| Ry zoom flag, night | 2.01-2.15 | 2.44-2.47 |
| Mollerup play | 0.35 | 0.39-0.43 |
| Mollerup shot, rain | 0.38-0.40 | 0.45-0.48 |
| Mollerup zoom flag, night | 0.85-0.88 | 1.02-1.05 |
| Hub plaza / range (always midsummer) | 0.40 / 0.56 | - |

Summer stays at the indexed trees' cost (Ry play 0.78 there, another session). Winter is not yet free: every
card texel of a bare crown evaluates the twig spray (with it switched off, Mollerup winter play is 0.28 ms), so
bare crowns cost up to 5% more in play at Ry, 10-20% at Mollerup and 15-20% in the zoomed night flag. Computing each card's spray in the vertex
shaders instead was tried and dropped: the trees are vertex bound, and summer rose to 1.19 ms at Ry. The plan's
"a winter crown costs no more than a summer one" is open. Sun and lamp maps (SMAP, LMAP) stay cached in calm
views; with figures moving, SMAP 0.13-0.16 ms in both seasons. Falling leaves draw in the tree stage; at their
real rate (a few in the air per tree at peak fall) they are not measurable. The hub always stands at midsummer.

## Tuning the look while measuring

`lighting.json` and `picture.json` reload when saved, and the look panel (F2
in the visual tour, with `GOLFPP_GOD=1`) steps the shafts', beams' and
mist's numbers live, so a stop's look and its GPU timings can be judged
together. Watching the files costs two write-time checks every half second;
a reload rebuilds only what its changed sections feed (a change of
`shafts.steps` or `shafts.lamps` recompiles the shaft programs and redraws
the floodlights' maps, `LMAP`), so expect a hitch on that frame only.

## Measured on the largest course

`himmerland_new_course` (2026-10-26, the owner's machine): building the
course takes about 1.1 s (`golfpp-shot-lab --course himmerland_new_course
--hole 1 --club driver`); a shot simulates in about 7 ms (the shot lab's 42
shots of the whole bag at three backswings in 0.3 s more); walking costs
about 0.03 ms of `update_game` a frame (the test preset, 3000 frames walking
and turning across the course).

## Current renderer benchmark

Measured 2026-11-18: 18 courses, 51 views, release, Ryzen 7 5800HS,
16 GB RAM, RTX 3050 Ti Laptop GPU (driver 610.47), AC power, 1920 x 1080
window and shipped 640 x 360 scene. The competing game was closed before
this run; a second golf++ instance started after it finished. GPU telemetry
recorded 79-90 C and thermal limiting in 241 of 436 one-second samples.
These are sustained, thermally variable laptop observations, not a certified
unthrottled hardware comparison.

Each case warms 60 frames and measures 240. A measured frame rebuilds render
data/lights/weather, renders, swaps with vsync off and calls `glFinish`.
It measures **serialized rendering cost**, including driver/query waits;
it excludes simulation, networking, audio and loading. It is not measured
whole-game FPS. The sweep is a full first rotation after the short warmup,
so it can include newly visited-view resource growth. The
[scripted full-app benchmark](#scripted-full-app-benchmark) measures the pipelined app separately.

32 of 51 p95 results exceeded 16.67 ms. This gaming machine needs substantial
headroom beyond a 60 Hz interval to support older integrated devices.
No earlier executable plus matching assets was benchmarked, so there is no
honest old/new percentage to quote.

All values below are p95 milliseconds; grass is CPU sample payload only.
A similar logical GPU texture payload is requested; driver storage may differ.

| Course | Play p95 | Pond p95 | Sweep p95 | Grass MiB |
|---|---:|---:|---:|---:|
| `blommenslyst_9_holes` | 9.88 | 14.31 | 10.08 | 39.7 |
| `blommenslyst_golfklub` | 17.80 | 26.30 | 18.69 | 138.3 |
| `hedensted_golf_klub` | 18.57 | 22.97 | 53.82 | 470.7 |
| `hedensted_pay_play` | 16.13 | 15.32 | 21.31 | 104.3 |
| `himmerland_new_course` | 17.44 | 29.80 | 17.46 | 296.8 |
| `himmerland_par_3` | 18.87 | 17.43 | 17.54 | 77.2 |
| `horsens_golfklub` | 17.77 | 31.58 | 37.59 | 139.6 |
| `horsens_par_3` | 10.49 | 13.20 | 10.80 | 42.0 |
| `jelling_golfklub` | 19.24 | 26.51 | 33.42 | 337.5 |
| `jelling_par_3` | 11.60 | 9.76 | 9.71 | 19.1 |
| `kalo_golf_club` | 17.54 | 27.92 | 28.18 | 303.2 |
| `kalo_par_3` | 10.31 | - | 9.78 | 43.9 |
| `marienlyst_golfklub` | 11.81 | - | 11.55 | 20.1 |
| `mollerup_golf_club` | 46.82 | 47.51 | 28.59 | 202.7 |
| `mollerup_par_3` | 10.27 | 12.87 | 9.29 | 19.0 |
| `odense_golfklub` | 24.37 | 31.63 | 24.29 | 190.8 |
| `odense_pilebanen` | 14.04 | - | 22.19 | 175.3 |
| `ry_golf_klub` | 20.71 | 17.73 | 21.00 | 223.8 |

Hedensted's sweep averaged 26.70 ms but reached p95 53.82 / p99 57.49 ms.
Mollerup's pond averaged 42.78 ms, p95 47.51 / p99 49.38 ms. Those cases
show why one small course's stage averages are insufficient. Warm solo
views stream roughly 1.7-4.8 MiB/frame in these cases; Hedensted's field is
470.7 MiB before its GPU texture. Peak observed process working set reached
about 1.55 GiB. These are measured costs, not proof that one particular
component accounts for every slow frame.

The [summary CSV](benchmarks/2026-11-18_renderer.csv) retains mean, median,
p95, p99, maximum, preparation/submission time and draw/upload counters.
Raw frames, GPU stages and telemetry from the completed run are in
`build/release/benchmarks/rtx3050ti/`. The later component-isolation batch
was interrupted when a separate golf++ instance began; its partial results
are exploratory and are not used as controlled subtraction estimates.
A temporary per-executable power-saving preference still selected NVIDIA
and was restored; actual integrated-GPU performance remains unmeasured.

Reproduce the probe (close competing GPU apps and avoid simultaneous builds):

```powershell
cmake --preset release
cmake --build build/release --target golfpp-picture-check --parallel 4
./build/release/golfpp-picture-check --profile
./build/release/golfpp-picture-check horsens_golfklub --profile --isolate
./build/release/golfpp-picture-check horsens_golfklub --profile --night
```

`--isolate` removes water, trees or grass only in diagnostic render data;
no production setting changes. Do not add those deltas: removed trees affect
both main and reflected views, and driver/clock effects are not additive.
The scene and memory changes proposed next are tracked in
`docs/todo/rendering_performance.md`.

## Earlier GPU stage measurements

Measured 2026-11-18 in release on the owner's NVIDIA GeForce RTX 3050 Ti
Laptop GPU (driver 610.47), 1920 x 1080 window, shipped 640 x 360 scene,
no vsync. `golfpp-picture-check COURSE --profile` uses the ordinary renderer,
including signs, props and the local figure. This earlier run used noon light,
60 warmup frames and 120 measured frames per view. It wrote `profile.csv` and `profile_info.txt`
in `build/release/picture_check/COURSE`.

| GPU stage (ms) | Marienlyst play | Horsens play | Horsens pond |
|---|---:|---:|---:|
| Full trees and signs (`TRE`) | 0.831 | 1.185 | 1.967 |
| Full-model sun shadows (`SMAP`) | 0.755 | 1.149 | 0.139 |
| Fixed grass (`GRS`) | 0.481 | 0.636 | 0.583 |
| Real mirrored scenes (`REF`) | 0 | 2.116 | 7.992 |
| Water composition (`WAT`) | 0 | 0.048 | 0.167 |

These stage-only fixed-view averages do not establish low-end headroom.
The broader frame-time results above supersede that conclusion. The sky,
sun shadows, shafts and camcorder stages (`SKY + SMAP + SHF
+ LMAP + BLM + SMR + CAM`) total 0.975 ms. The sun map is 256 square;
`map_softness` is 0.375 texels, preserving the world-space PCF tap offsets
of the previous 1024-square, 1.5-texel filter. Tree geometry, leaf cutouts
and wind pose are identical in picture, sun map and lamp maps.

Mirrors retain the full scene at quarter width and height (160 x 90 in this
run). Current-frame depth queries skip entirely hidden water planes. Before
those queries, the full-resolution Horsens pond view spent 118.96 ms in
mirrors; with queries at half resolution it spent 10.60 ms, and the final
quarter-resolution pass costs 7.99 ms. These compare the same view; the
first measurement also used the larger sun map. Marienlyst has no ponds,
so Horsens supplies the water measurement. More visible water levels mean
more real scene passes; `REF` includes their geometry, shading and visibility
queries rather than charging their work to the main scene's counters.

The one-time grass field bake takes 0.20 s for Marienlyst's 1418 x 1238
nodes and 2.35 s for Horsens's 4102 x 2974 nodes. It is not part of frame
cost; no field is redrawn or uploaded while walking.

## Online: server energy and traffic

Maincloud bills a database in energy: the work its reducers and subscriptions
do, what it stores, and the data it sends. A local server (`spacetime start`)
costs nothing, so check usage on Maincloud.

### Reading it

- **The Maincloud dashboard** (spacetimedb.com, the `golfpp` database): energy
  used over time. To measure a session, note the figure, play, and read it
  again. The bar to meet: an hour of two-player play uses well under 1% of the
  free tier's monthly energy.
- **`spacetime logs golfpp --server maincloud -f`**: what the module reports
  (refused logins, errors, panics). A reducer that fails over and over still
  costs energy each call, so a flood of the same refusal here is worth fixing.
  `--server local` reads a local server the same way.

### What keeps traffic down

The rules are in `AGENTS.md` ("Offline and online part in one place"); in short:

- Subscriptions are only my own rows and my room's (`src/net/net_client.cpp`),
  never a whole table. Avatar and cart motion use `my_avatar_motion` and
  `my_golf_carts`: the caller's 100-metre cell and its eight neighbours,
  self, group and occupied seats. Cold room rows and events stay room-wide.
- Motion is sparse: sent on a change of intent, as a heartbeat while moving,
  or when others' extrapolation would drift too far, and never more often than
  `net.motion_min_interval_seconds` (`assets/play/tuning/network.json`).
  Others carry each player on between updates (`extrapolate_motion`).
- A shot is one event (its inputs and where it rests); every client simulates
  the flight itself (`src/game/remote_players.h`), so no flight is streamed.
- `hole_score` keeps every hole an account ever scored and is not subscribed:
  nothing in the game shows it yet.

Raising the motion rate, adding a subscribed table or a column that changes
often, or subscribing to history all show up on the dashboard: measure a
session before and after.

### Motion interest delivery benchmark

Native `"motion interest:"` tests (2026-11-03) exercise the same indexed
selection used by both server views. One update from every player is
selected for every observer, with players spread round 18 non-neighbouring
cells, no groups or passengers. Counts include each player's own row.

| Players | Whole-room row deliveries | Interest row deliveries |
|---|---:|---:|
| 2 | 4 | 2 |
| 8 | 64 | 8 |
| 40 | 1600 | 92 |

Each view seeks nine cells and retained account ids; population does not
add index seeks. A crowded cell still delivers everyone. Interest adds two
cell coordinates and a room/cell key to each avatar/cart row: counts alone
are not a byte measurement. The benchmark excludes view execution,
transaction envelopes, compression, subscriptions' initial snapshots and
cold tables. It does not measure Maincloud energy or end-to-end bandwidth;
measure 2- and 8-player play on the test database before release, as the
launch checklist requires.

### Movement stepping cost

The server steps every player's walking and driving inputs
(`movement_rules.h` `play_move`, 120 steps a second). Measured natively in the
`test` build (2026-11-03, fixture course, one player's second of steps per
call): about 0.22 ms walking and 0.46 ms driving. The server's WASM is
slower, and each call also builds the room's obstacles. This is new CPU on
every motion where `update_motion` used to only check a position, so energy
per player-hour will rise: the reason is that positions, reach, the shot
snapshot and movement XP are now the server's own. Measure 2- and 8-player
play on the test database before release; if it costs too much, walking
standing still is already skipped, and walking could step at half the rate.


Screenshot runs accept `--screenshot-view x,z,yaw,pitch` (radians), placing the eye on the sampled ground at that location. It applies only with `GOLFPP_SCREENSHOT`. Hub path lamps share the nearest 16 scene lights and four 128-square lamp shadow maps (cached while the casters' pose holds); adding posts does not increase those GPU budgets.
