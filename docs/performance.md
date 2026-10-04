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
whole microseconds to keep the lines short.

| Counter | Meaning |
|---|---|
| `UPD` | CPU time in `update_game` (physics, movement, terrain sampling) |
| `MESH` | Refreshing static anchors and render meshes. Should be ~0 except on the frame an area loads |
| `MRD` | Building the frame's render data. Should stay well under 1 ms |
| `SWP` | `SDL_GL_SwapWindow`. With vsync on this is the vblank wait, so large = headroom |
| `REN` / `SCN` / `OVL` / `CRT` | Whole render call / 3D scene / 2D overlay / CRT pass |
| `DRAW` | Draw calls, excluding the overlay's own (`DBGUI`) |
| `UNI` / `ULOC` | Uniform sets / uniform location lookups. `ULOC` must be 0 after warmup |
| `BUF` | Streaming buffer writes / reallocations / bytes. Reallocations must settle to 0 |
| `TSAMP` / `TTRI` | Terrain sample calls / triangles tested across them |
| `CHUNK v/c R IDX` | Terrain chunks visible / culled, merged draw ranges, indices drawn |
| `TREE` | `ON` when the instanced tree batch passed frustum culling |
| `GPU ...` | GPU pass timings from timer queries, reported 2 frames late |

The overlay text comes from `format_profile_overlay_lines` in
`src/profiling/profiling.cpp`.

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
  never a whole table. Every row in a room goes to everyone in it, so a full
  room of 40 multiplies whatever one player's rows cost.
- Motion is sparse: sent on a change of intent, as a heartbeat while moving,
  or when others' extrapolation would drift too far, and never more often than
  `net.motion_min_interval_seconds` (`assets/tuning/game_tuning.json`).
  Others carry each player on between updates (`extrapolate_motion`).
- A shot is one event (its inputs and where it rests); every client simulates
  the flight itself (`src/game/remote_players.h`), so no flight is streamed.
- `hole_score` keeps every hole an account ever scored and is not subscribed:
  nothing in the game shows it yet.

Raising the motion rate, adding a subscribed table or a column that changes
often, or subscribing to history all show up on the dashboard: measure a
session before and after.
