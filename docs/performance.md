# Profiling

Profile the **release** build; debug is not representative.

## Startup switches

They are environment variables, read once at startup in `src/core/main.cpp` and
parsed by `src/core/startup_options.cpp`. Nothing in game, physics or renderer
code reads them.

| Variable | Effect |
|---|---|
| `GOLFPP_COURSE=<course id>` | Skip the menu and boot straight into that course (the `id` in `assets/courses/*.json`). Unknown ids fall back to the menu. |
| `GOLFPP_VSYNC=0` | Present without vsync so FPS shows real headroom. Also accepts `off`, `no`, `false`, `disable(d)`; anything else keeps vsync on. |
| `GOLFPP_WINDOW=<W>x<H>` | Open the window at this size in pixels (e.g. `3440x1440`) instead of 1280x720. Anything else keeps the default. The fullscreen setting still wins. |
| `GOLFPP_SCREENSHOT=<path.bmp>` | 120 frames after loading (into `GOLFPP_COURSE`, or the menu), save the window's picture after the TV pass as a 24-bit BMP and quit. Runs with default settings and never writes the save. For reviewing the picture. |
| `GOLFPP_GAME_TIME=<hour>` or `<YYYY-MM-DD>T<hour>` | Pin the sky at this hour on the course's clock (`0` to `24`, e.g. `19.5`; the course file's `sky.utc_offset_hours`) on the course's date (its `sky.date`), or on the given date (`1989-12-21T23`: the Danish courses' midsummer night never gets darker than about −10.5°, so a winter date shows a true night), so screenshots show the same light every time. Anything else leaves the clock running. |
| `GOLFPP_WEATHER=<kind>[,<wind>[,<wetness>]]` | Hold the weather offline from the start (the god menu's hold, `src/core/god_menu.h`): `clear`, `cloudy`, `light_fog`, `fog`, `light_rain`, `rain`, `heavy_rain`, `thunderstorm` or `sun_shower`, then optionally the wind in m/s (at 10 m, blowing towards +Z) and the ground's wetness from `0` to `1`, e.g. `heavy_rain,8,1`. Anything else holds nothing. Online play ignores it. |
| `GOLFPP_GOD=1` | The god menu can be opened with F1 (as `--god` does): hold the time of day, the weather, the wind and the wetness to try them. Arrows change a row, Backspace lets it run live again. |

```pwrshl
$env:GOLFPP_VSYNC = "0"; $env:GOLFPP_COURSE = "marienlyst_golfklub"
.\build\release\golf++.exe
Remove-Item Env:GOLFPP_VSYNC, Env:GOLFPP_COURSE

$env:GOLFPP_COURSE = "marienlyst_golfklub"; $env:GOLFPP_WINDOW = "3440x1440"; $env:GOLFPP_SCREENSHOT = "wide.bmp"
.\build\release\golf++.exe
Remove-Item Env:GOLFPP_COURSE, Env:GOLFPP_WINDOW, Env:GOLFPP_SCREENSHOT
```

## Overlay

Press `Ctrl` in game to toggle FPS + the profiling overlay. The profiler records
nothing while the overlay is off. While it shows, hold `T` to run the sky
forward (two game hours a second); the offset lasts until the game quits and
only this player sees it. Values are averaged over 0.25 s; timings are
whole microseconds to keep the lines short.

| Counter | Meaning |
|---|---|
| `UPD` | CPU time in `update_game` (physics, movement, terrain sampling) |
| `MESH` | Refreshing static anchors and render meshes. Should be ~0 except on the frame an area loads |
| `MRD` | Building the frame's render data. Should stay well under 1 ms |
| `SWP` | `SDL_GL_SwapWindow`. With vsync on this is the vblank wait, so large = headroom |
| `REN` / `SCN` / `OVL` / `CRT` | Whole render call / 3D scene / 2D overlay / TV pass |
| `DRAW` | Draw calls, excluding the overlay's own (`DBGUI`) |
| `UNI` / `ULOC` | Uniform sets / uniform location lookups. `ULOC` must be 0 after warmup |
| `BUF` | Streaming buffer writes / reallocations / bytes. Reallocations must settle to 0 |
| `TSAMP` / `TTRI` | Terrain sample calls / triangles tested across them |
| `CHUNK v/c R IDX` | Terrain chunks visible / culled, merged draw ranges, indices drawn |
| `TREE` | `ON` when the instanced tree batch passed frustum culling |
| `GPU SMAP` / `TER` / `SHD` / `TRE` / `SKY` / `WTR` / `BLM` / `SMR` / `CAM` / `EYE` / `TAPE` / `OVL` / `CRT` / `ZOOM` | GPU timings from timer queries, reported 2 frames late: the shadow map of trees, floodlights, players and carts (`shadow_map_pass`, 0 with the sun down) / terrain / blob shadows (`blob_shadow_renderer`) / trees, signs, fence posts and floodlights / the sky and land panorama behind them, its rainbow too (`backdrop_pass`) / rain streaks and the wind's specks (`weather_renderer`, two line draws, 0 with neither) / the camcorder's quarter-size copy, metering and bloom / its column smear / its recording of the picture (`camcorder_pass`) / the rangefinder's eyepiece (`eyepiece_pass`, 0 when not held up) / tape pass / overlay / TV pass / the eyepiece's zoomed view, the scene drawn again within the eyepiece at its zoom (0 when not held up). While paused only the tape, overlay and TV run |

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
