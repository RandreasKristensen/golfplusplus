# Profiling

Profile the **release** build; debug is not representative.

## Startup switches

Both are environment variables, read once at startup in `src/core/main.cpp` and
parsed by `src/core/startup_options.cpp`. Nothing in game, physics or renderer
code reads them.

| Variable | Effect |
|---|---|
| `GOLFPP_COURSE=<course id>` | Skip the menu and boot straight into that course (the `id` in `assets/courses/*.json`). Unknown ids fall back to the menu. |
| `GOLFPP_VSYNC=0` | Present without vsync so FPS shows real headroom. Also accepts `off`, `no`, `false`, `disable(d)`; anything else keeps vsync on. |

```pwrshl
$env:GOLFPP_VSYNC = "0"; $env:GOLFPP_COURSE = "marienlyst_golfklub"
.\build\release\golf++.exe
Remove-Item Env:GOLFPP_VSYNC, Env:GOLFPP_COURSE
```

## Overlay

Press `Ctrl` in game to toggle FPS + the profiling overlay. The profiler records
nothing while the overlay is off. Values are averaged over 0.25 s; timings are
whole microseconds (the pixel font has no `.`).

| Counter | Meaning |
|---|---|
| `UPD` | CPU time in `update_game` (physics, movement, terrain sampling) |
| `MESH` | Rebuilding cached render meshes. Should be ~0 except on the frame a hole loads |
| `MRD` | Building the frame's render data. Should stay well under 1 ms |
| `SWP` | `SDL_GL_SwapWindow`. With vsync on this is the vblank wait, so large = headroom |
| `REN` / `SCN` / `OVL` / `CRT` | Whole render call / 3D scene / 2D overlay / CRT pass |
| `DRAW` | Draw calls, excluding the overlay's own (`DBGUI`) |
| `UNI` / `ULOC` | Uniform sets / uniform location lookups. `ULOC` must be 0 after warmup |
| `BUF` | Streaming buffer writes / reallocations / bytes. Reallocations must settle to 0 |
| `TSAMP` / `TTRI` | Terrain sample calls / triangles tested across them |
| `CHUNK v/c R IDX` | Terrain chunks visible / culled, merged draw ranges, indices drawn |
| `TREE` | `ON` when the instanced tree batch passed frustum culling |
| `GPU ...` | GPU pass timings, only when timer queries exist; reported 2 frames late |

The overlay text comes from `format_profile_overlay_lines` in
`src/profiling/profiling.cpp`.
