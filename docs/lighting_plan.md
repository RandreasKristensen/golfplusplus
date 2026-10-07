# Lighting plan: day/night cycle and camcorder light

Goal: every course has a sun that moves across its real sky, nights the player can
still play in, and light that looks the way a 1989 consumer camcorder saw it:
blown-out highlights, vertical smear from bright lights, warm tungsten and blue
dusk, grainy gain-up at night. Do it cheaply. The scene stays at roughly 640×360,
and every new effect runs at that resolution except the CRT pass that already exists.

**Status: draft, not owner-approved.** The open decisions below need answers before
Phase 1 starts. Once it is approved, add it to the "Planned work" section of
`AGENTS.md`, and keep the docs in
sync under the same rules (delete finished phases, renumber, move rules into
`AGENTS.md`).

No new dependencies. Everything is GLSL 330 plus plain C++.

---

## Where it stands today

| Piece | Now |
|---|---|
| Sun | One constant `light_direction` in `src/renderer/renderer.cpp` |
| Sky | Each course's sky panorama, then its land panorama fading into the course's `haze_color` (`backdrop_pass`, a full-screen pass drawn first); `sky_color` only clears |
| Terrain | `terrain.frag`: vertex colour × `(0.70 + 0.30·N·L)`, then faded into the backdrop's haze with distance (`scene_haze.h`, also on trees). No ambient model |
| Trees, markers | Unlit flat colour (`world_marker.frag`). Tree normals exist in the primitive VBO but are not used |
| Ball | `ball.frag`: `0.45 + 0.55·N·L` |
| Colours | `sky_color`, `backdrop_ground_color`, `ball_color`, `trunk_color`, `leaf_color` and `terrain_palette.cpp` are all C++ constants |
| Target | `framebuffer`: `GL_RGB` 8-bit, nearest filtered |
| Post | `crt.frag`: scanlines and vignette at window resolution |
| Location | Course worlds carry `projection.origin_lat/lon`, read only by tooling |

The game is already cheap to draw (a handful of draw calls, chunk-culled terrain,
one instanced tree batch), and this plan has to keep it that way.

## Art direction: what "pretty" means here

We're not after realistic lighting. We want light that looks recorded by a cheap
CCD camcorder and then played on a small TV. Each effect below copies something
a real 1989 camcorder did:

- **Narrow dynamic range.** A sky behind a sunlit fairway clips to white, and
  shadows crush toward a muddy blue-black. A soft shoulder tone curve, not a filmic one.
- **Auto white balance and auto iris that lag behind.** Turn toward the sun and the
  picture darkens over about a second. Dusk goes blue, tungsten-lit areas go orange.
- **CCD vertical smear.** A very bright point (the sun near the horizon, a cart
  headlight) leaves a faint vertical streak through the whole frame column. It's
  the most recognisable camcorder artifact, and it's cheap to do.
- **Bloom and highlight bleed.** Bright things spill into their neighbours,
  mostly horizontally, as tape does.
- **Chroma bleed.** Colour is lower resolution than brightness and smears to the
  right. Done in YIQ at low res.
- **Gain-up noise at night.** As light drops, the "camera" raises gain: more
  grain, less saturation, a slightly green-grey cast.
- **Datestamp.** An optional `PM 7:42 OCT 2 1989` burn-in in the corner, which shows
  the time of day as part of the fiction. Text from `en.json`, drawn in a box with the
  bitmap font like any other HUD text.

Ask the existing question for every one of these: does this look more like a 1989
camcorder tape, or less?

## Open decisions (owner)

| Topic | Options | Recommendation |
|---|---|---|
| Clock | (a) Real time: the sun is where it really is over that course right now. (b) Accelerated cycle, e.g. one day = 48 minutes. (c) Fixed time per round | **(b)**, with the cycle length in tuning. Real time sounds charming, but Danish winter evenings would mean always playing in the dark |
| Sun path | Generic arc, or the real solar position from the course's latitude and a game date | **Real latitude.** It's one pure function and gives each course its own character (long Nordic dusk) |
| Online sync | Pure function of UTC time with the cycle baked into client content, or an authoritative server row | **A server row per room** (`room_sky`, see "Online: the sky is server state"). It holds a timeline anchor, not a ticking clock, so it costs no reducer calls. The operator can change the cycle without a client release, and later weather and time-dependent rules have one authority |
| Sky per room or global | One sky per room, or one for the whole server | **Per room**, so subscriptions stay room-scoped. New rooms copy the server default, so all rooms share one sky unless an event changes a room |
| Night play | Night is just darker, or night needs lights (cart headlights, glow ball, lit greens) | **Lights.** Night golf with a glow ball is a real thing and a good look. The ball is never unreadable |
| Datestamp | Always on, a setting, or a camcorder collectible later | A setting, off by default until settings exist; on in the hub only |
| Gameplay effect | None, or hooks (night XP bonus, nocturnal collectibles) | **None in this plan.** Time of day is visual only. Gameplay hooks can be added later as `progress_rules` |

## Design

### Time → sky state (pure, GL-free)

`src/game/sky_clock.h`: pure functions, unit tested. The game reads a
**timeline** from the active mode, in the same way it reads progress through
`active_progress()`, and turns it into a sky state:

```cpp
// Where the sky's clock stands. It changes only when someone changes the
// sky (server default, an event, an admin), never because time passed.
struct sky_timeline {
    std::int64_t anchor_unix_ms = 0;   // real time at which...
    double anchor_game_day = 0.0;      // ...the game clock read this (whole part = date, fraction = time of day)
    double game_days_per_real_day = 0.0;  // 30 = one game day per 48 real minutes; 0 = frozen
};

// The game day and time at `now_unix_ms`. Pure arithmetic in doubles, so it
// gives the same result native and in WASM: any server rule that ever depends on
// time of day uses this, never the trigonometry below.
double game_day_at(const sky_timeline& timeline, std::int64_t now_unix_ms);

struct sky_state {
    glm::vec3 sun_direction;   // world space, towards the sun
    glm::vec3 moon_direction;
    float sun_elevation;       // radians; what the palette is keyed on
    float moon_phase;          // 0 new .. 1 full
    float day_fraction;        // 0..1 local solar time, for the datestamp
};
// Visual only; client side.
sky_state compute_sky_state(double game_day, double latitude_deg, double longitude_deg);
```

- A low-precision solar position (declination + hour angle) is plenty. The
  moon is the anti-sun direction offset by the phase. No ephemeris library.
- The whole part of the game day is a calendar date, so seasons move along with
  the game clock (summer dusk at Kalø is long, winter days are short).
- The course world loader starts reading `projection.origin_lat/lon`. Courses
  without it fail loudly, as missing content always does.
- `game_state::sky` holds the active `sky_timeline`:
  - **offline:** built from `assets/tuning/game_tuning.json` (`sky_clock`: anchor
    date, start time, rate) and the local clock. No save field, no migration.
  - **online:** mirrored from the room's `room_sky` row. It is never computed
    locally and never written to the local save.
- `render_frame` evaluates `game_day_at` and `compute_sky_state`, then fills a new
  `render_lighting` in `render_data`. The renderer never sees the clock.

### Online: the sky is server state

Online, the server owns the sky, like everything else other players see.
`AGENTS.md` already makes the server the authority on everything
shared and keeps clients to room-scoped subscriptions. The sky follows the same
rules:

- **What is stored is the timeline, not the time.** One `room_sky` row per room
  holds a `sky_timeline`. Clients extrapolate it with `game_day_at` and their
  estimate of server time. There is **no scheduled reducer ticking the clock**:
  a ticking row would fan an update out to 40 subscribers every tick and bill
  energy for nothing. The row changes only when the sky actually changes.
- **Server time on the client.** `net_client` already needs a client-to-server
  clock offset to extrapolate `avatar_motion` from `server_time`. The sky uses that
  same estimate (one home). A wrong offset of a few hundred ms is invisible in the sky.
- **Defaults.** `server_config` gets a default `sky_timeline`. `join_course` copies
  it into `room_sky` when it creates a room, so every room shares one sky by
  default. An owner-only `admin_set_room_sky(room_id, timeline)` reducer covers
  events (a "night golf" weekend, a frozen golden hour). Changing the default
  re-anchors open rooms at their current game day, so their skies don't jump.
- **Re-anchoring keeps continuity.** Any timeline change first evaluates the old
  timeline at `now` and anchors the new one there. A rate change never makes
  the sun jump, unless an admin sets the day explicitly.
- **Self-hosting.** The default timeline is ordinary server config, so a
  self-hosted server picks its own cycle. Offline uses the tuning default and
  never asks a server.
- **Gameplay authority.** Time of day is visual only in this plan. If a rule ever
  depends on it (night-only collectibles, a dusk XP bonus), the server evaluates it
  with `game_day_at` on the `room_sky` row inside the reducer, and the offline path
  runs the same function through `progress_rules`. That's why `game_day_at` is
  plain arithmetic and lives in `src/game/` with no GL. It's added to the module's
  shared sources (`server/golfpp_module/CMakeLists.txt`) only once a rule needs it.
- **Weather later.** Wind is still sampled from `shot_input.wind_time`
  (`src/game/shot_simulation.h`). If weather ever becomes world state (the `ideas.md` wind and rain),
  it belongs in this same per-room row, and `shot_input` would carry the weather
  it was simulated with, so the server can replay the shot. Not in this plan, but
  don't design `room_sky` in a way that blocks it.

#### Server and client changes

| Where | Change |
|---|---|
| Server tables (`server/golfpp_module/src/lib.cpp`) | `room_sky`, public, PK `room_id`: `anchor_unix_ms`, `anchor_game_day`, `game_days_per_real_day`. `server_config` gets the same three columns as `default_sky_*` |
| Server reducers (`lib.cpp`) | `join_course` creates `room_sky` with a new room (copying the default, re-anchored to now). The room cleanup that deletes empty rooms also deletes their `room_sky`. New owner-only `admin_set_room_sky` and `admin_set_default_sky` |
| Room subscription (`src/net/net_client.cpp` subscribe_room) | `room_sky where room_id = <mine>` |
| Network seam (`src/game/net_types.h`) | `online_view` gets `sky_timeline sky`. An accessor, `active_sky(const game_state&)`, returns the offline or online timeline, like `active_progress` |
| `net_client` | When the room changes, the sky blends to the new room's state over about 2 s rather than snapping. With no connection, keep extrapolating the last timeline |
| Traffic | No added calls. One row per room, written only on events |
| Protocol (`src/game/net_types.h`) | `protocol_version` goes up: a new table and reducers |

Online play exists, so `active_sky` has both paths from the start. Nothing in the
lighting phases waits on the server: without the room's sky (a local server
without it yet, or no connection), the online path keeps the last timeline.

### Palette keyed on sun elevation (data)

`assets/visuals/lighting.json` with a loader `src/game/lighting_loader.cpp`
(`parse_lighting_from_text`). Its keys are **sun elevations**, not clock times, so
dusk lasts as long as it really would at that latitude:

```json
{ "keys": [
  { "sun_elevation_deg": -18, "sun_color": [...], "sky_zenith": [...], "sky_horizon": [...],
    "ambient_sky": [...], "ambient_ground": [...], "fog_color": [...], "fog_density": 0.004,
    "exposure": 2.2, "white_balance": [...], "gain_noise": 0.35, "saturation": 0.6 },
  { "sun_elevation_deg": -4,  ... },
  { "sun_elevation_deg": 2,   ... },
  { "sun_elevation_deg": 10,  ... },
  { "sun_elevation_deg": 35,  ... }
]}
```

The surface and prop colours (`terrain_palette.cpp`, trees, backdrop ground,
ball, emote props) move to data in the same file, which clears up an existing
"content is data" gap. The noon key has to reproduce today's look, so Phase 1
changes nothing visually.

### One lighting model, one home

- A **std140 uniform block** `frame_lighting`, uploaded once per frame and bound to
  every scene shader: sun direction and colour, hemisphere sky and ground colour,
  fog, moon, point lights. This takes the per-shader `u_light_dir` sets out of the
  `UNI` count.
- A **shared `assets/shaders/lighting.glsl`**, pulled in by a small `#include`
  step in `shader.cpp`. It is the only place that computes scene light, so terrain,
  trees, markers and ball all agree.
- Model: `albedo × (hemisphere(N) × ao + sun_color × wrap(N·L) × shadow) + emissive`,
  then exponential fog toward the horizon colour along the view ray. Fog is computed
  per vertex. Its main jobs are depth cues and a soft horizon, and it lets the far
  plane come in, which also gives back terrain triangles.
- Trees become lit using the normals already in the primitive VBO: the instanced
  vertex shader passes them through. Markers stay unlit but are **tinted by
  ambient light** so they don't glow at night. The ball, pin and flag are an
  exception (see readability).

### Baked, never per frame

Anything that depends on the terrain but not on time is baked when the area mesh
is built (`make_terrain_render_mesh`), in a pure, tested function:

- **Sky visibility / AO per vertex** from the ground grid: bunkers, valleys and
  tree bases go darker.
- **Horizon angles in 8 directions per vertex**, packed into two attributes. In the
  shader, `shadow = smoothstep` of the sun's elevation against the horizon angle
  in the sun's direction. Hills then cast real shadows across fairways at sunrise
  and sunset, at any time of day, without a shadow map and without a rebake.

Time-of-day changes are uniforms only. No mesh, buffer or bake ever changes because
the clock moved.

### Sky

A full-screen sky pass at low res, drawn **after opaque geometry** with the depth
test at the far plane, so it only shades pixels that show sky:

- Zenith-to-horizon gradient, plus a horizon glow towards the sun at low elevations.
- Sun disc (HDR-bright, so it drives bloom and smear) and a moon disc with the phase.
- Hash-based stars that fade in below −6°, with slight twinkle. No texture.
- One band of procedural low clouds lit by the sun colour. Optional, last in the phase.

### Camcorder pass (before overlay, before CRT)

The pass order becomes:

```
scene (RGBA16F, low res)
  → camcorder pass (low res): exposure, white balance, tone curve, bloom,
    CCD smear, chroma bleed, gain noise, saturation  →  RGB8 low-res target
  → overlay (HUD, menus, datestamp) on that target
  → CRT pass (window res, mandatory, unchanged order)
```

- The HUD is drawn **after** the camcorder pass, so text is never bloomed, smeared
  or noisy.
- **Bloom:** threshold + downsample to 1/4 res + two blur taps, then add back.
- **CCD smear:** a `W×1` pass takes each column's over-threshold maximum
  (W×H reads, ~230k at 640×360), and the camcorder pass adds it as a faint
  vertical streak.
- **Auto iris:** average luminance from the mip chain of the HDR target goes into
  a 1×1 ping-pong texture that eases toward the target. No CPU readback, no stall.
- The CRT pass stays last and always on. These effects come before it and never
  replace it.

### Night lights

- Up to 8 point or spot lights in the UBO, chosen per frame by the CPU (closest to
  the camera). Evaluated per pixel. At 640×360 that's cheap.
- Sources: cart headlights (spot), the glow ball (small radius, emissive), pin
  lights on the active green, and later clubhouse or path lamps from course data
  (via the planned interaction system, not a one-off list).
- The cigarette ember and other emissives finally show up after dark through bloom.

### Shadows

- **Blob shadows** under the ball, player, cart and trees: dark ellipses projected
  on the ground along the sun direction, faded by sun elevation, batched like
  world markers. The ball's shadow matters for golf because it shows ball height
  in flight. Do these first.
- **Terrain shadowing** comes from the horizon bake above.
- **A real shadow map** (trees on fairways) is optional and comes last. Only do it
  if the GPU timers show it fits the budget, at a small, chunky resolution
  (512–1024) that suits the look.

### Readability (non-negotiable)

Golf simulation comes first. At every palette key:

- The ball, cup, flag and aim markers keep a minimum emissive floor, so they
  always contrast with the fairway and green.
- A content test computes ball-vs-fairway and flag-vs-sky luminance contrast at
  every key in `lighting.json` and fails below a threshold. When it fails, fix the
  palette, not the check.

## Performance rules

- Nothing time-dependent touches the CPU beyond one UBO write per frame.
- Every new pass gets a `gpu_profile_stage` (`sky`, `camcorder`, `bloom`, `smear`) so
  it shows up in the `Ctrl` overlay.
- All post passes run at scene resolution or smaller. Only the CRT pass runs at
  window resolution.
- Budget: Phases 1–5 together add **at most 1.0 ms GPU** at 1080p on the owner's
  machine (release build, `GOLFPP_VSYNC=0`, `marienlyst_golfklub`), measured
  before and after each phase and recorded in `docs/performance.md`.
- No branching on uniforms to choose a shader path. Use separate programs or
  `#define` variants (the current `u_use_vertex_color` branch is fine to leave
  until the terrain shader is split).
- `renderer.cpp` is at its size limit. The new passes go in their own files:
  `src/renderer/lighting_uniforms.*`, `sky_pass.*`, `camcorder_pass.*`,
  `blob_shadow_batch.*`.

## Phases

Each phase leaves the build warning-free and all tests passing.

| Phase | What | Visible result |
|---|---|---|
| 1 | Plumbing: `sky_timeline` + `sky_clock` (pure, tested), `game_state::sky` and `active_sky()` (offline source only), course lat/lon loading, `lighting.json` + loader with every hard-coded scene colour moved into it, `render_lighting`, UBO, `lighting.glsl` include, developer time-scrub key | Unchanged at noon. Scrubbing moves the sun |
| 2 | Lighting model: hemisphere + wrapped sun, fog, lit trees, ambient-tinted markers, baked AO, full palette keys, readability test | A real day/night cycle |
| 3 | Sky pass: gradient, sun/moon discs, stars, horizon glow | Sunsets |
| 4 | Camcorder pass: RGBA16F target, auto iris, white balance, tone curve, bloom, CCD smear, chroma bleed, gain noise. Datestamp text | The camcorder look |
| 5 | Shadows and night lights: blob shadows, horizon-angle terrain shadows, point and spot lights, glow ball, headlights | Night golf |
| 6 | Optional: shadow map for trees, procedural clouds. Only if the budget allows | |

## Tests

- `sky_clock`: solar noon elevation at known latitudes and dates, sunrise
  azimuth roughly east, the same timeline + time + course always giving the same state.
- `game_day_at`: frozen rate stays at the anchor. Re-anchoring at `now` keeps the
  game day continuous across a rate change. Large `unix_ms` values keep millisecond
  precision.
- Offline never reads the online timeline, and online never builds one from
  tuning (in the same shape as `tests/mode_dispatch_tests.cpp`).
- Palette: keys sorted, covering −90° to 90° after clamping, every field present,
  interpolation continuous.
- Readability contrast at every key (shipped content, `tests/content_tests.cpp`).
- Bakes: AO is 1 on flat open ground and below 1 in a bunker fixture. Horizon
  angles are 0 on a flat fixture.
- The existing every-screen label test still passes with the datestamp shown.

## Rule changes to write into AGENTS.md

- Time of day is visual only. It never changes physics, scoring or rewards (until a
  `progress_rules` change explicitly says otherwise).
- The sky is a `sky_timeline` plus the current time. Online, the timeline is the
  room's `room_sky` row (server-authoritative, never ticked by a scheduled
  reducer). Offline, it comes from tuning and the local clock (no save field). The
  two never mix. Game code reads it only through `active_sky()`.
- Any gameplay rule that depends on time of day uses `game_day_at` (plain
  arithmetic, replayable on the server), never the sun trigonometry.
- All scene light comes from `assets/shaders/lighting.glsl` and the
  `frame_lighting` block. Scene colours live in `assets/visuals/lighting.json`.
- Time-dependent lighting is uniforms only. Terrain-dependent lighting is baked when
  the mesh is built. Neither ever rebuilds meshes per frame.
- Post effects run at scene resolution before the overlay. The CRT pass stays last
  and mandatory.
- The ball, cup and flag stay readable at every time of day, and a content test
  enforces it.
