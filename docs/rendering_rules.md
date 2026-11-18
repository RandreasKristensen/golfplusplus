# Rendering, UI and sound rules

Read before changing the corresponding systems, including callers and content.
Paths are relative to the repository root.

**One world to draw (owner decision, 2026-11-17).** Everything drawn is
fixed by the world (course data, clock, weather), built at course load. The
eye only chooses the projection. A distance switch may draw the same thing
more cheaply; it never draws something different, and a test shows the look
matches across the switch. Water reflections are always real. Static geometry
and the grass field are built with the course; the clock and weather animate
them without regrowing them.

**One lighting path.** All scene light and haze come from
`assets/picture/shaders/lighting.glsl` and its `frame_lighting` block, uploaded
once per scene view; no shader has a light or haze uniform of its own, and the
clock moving changes uniforms only, never a mesh. Nothing glows at night
unless the palette gives it a glow floor, it is a lit lamp's face or a night
light reaches it. Light after dark comes from something visible holding it
(the ball, headlights, a floodlight, a lit window's `window` glow floor), never a point in the air. A new light is
a candidate in `src/core/light_frame.h` with its numbers in `lighting.json`.
Everything drawn in the world (ground, trees, props, markers, the ball, tee
boxes) fades into the same haze, `hazed` or `faded` per vertex with
`scattered_light` per pixel, so nothing shows through the haze before the
ground round it; `tests/shader_tests.cpp` lists every scene program. The
haze takes the low mist with it (`haze_over`): thickest on the area's
lowest ground and thinning with height by the palette key's
`mist_height_meters`, integrated in closed form along the ray, so no shader
hazes by distance alone.
Terrain-dependent, time-independent light (sky visibility, horizons) is baked
with the area's render meshes, never per frame. Every model (a cart, a club,
what is held, mine pinned to the camera too) is drawn by the one lit prop path
(`src/renderer/prop_renderer.h`, `prop.frag`): lit where it is in the world by
the sun, the shadow map and the night lights on the side facing them; a new
model file needs no shader or drawing code. Everything standing in the world
casts the sun's shadow: anything drawn outside the prop path (flagsticks,
signs, fence posts, figures) is added to `src/renderer/shadow_casters.h`.

The sun shadow map is centred on the camera's position, never offset by its
look direction: turning the camera must not move the coverage or its fade
through the world.

**Light in the air is real, never boosted.** The air's light all lives in
`lighting.glsl` (on the CPU for tests: `src/renderer/scattered_light.h`):
the haze glows toward the sun (Henyey-Greenstein, `sun_glow_at_sun`,
`sun_glow_spread`), thick as real visibility says (Koschmieder: 3.912 /
visibility); the night lights' beams scatter by the real inverse square,
softened over the lamp's housing (`beam_core_meters`), as bright as a surface
at `beam_near_share` of the reach; the low mist is exponential height fog
integrated in closed form. Every shader hazes as if the air in front were in
sun; the sun shaft pass (`sun_shaft_pass.h`) then takes out the glow of the
air in the shadow map's shadow, and the trees' and floodlights' shadows out of
the nearest floodlights' beams (`lamp_shadow_pass.h`, maps drawn when a
lamp, the casters or their wind pose change). A thin mist lies all day, so the view fades out
within a kilometre and shafts are faint in clear noon air, strong in mist and
fog, and a beam is bright at the lamp and faint by the ground. Not
done: a 3D grid of light cells (froxels) needs compute shaders, which macOS's
GL 4.1 lacks, and costs too much as fragment shaders; screen-space god rays
work only with the sun on screen and read as a lens trick. Tune the air by eye
in the visual tour with the F2 panel (`docs/development.md`).

**Words in the world are sign faces.** Hole signs, way signs and prop boards (the
bus's destination, the tee book's title) are painted with the pixel font into
small textures (`renderer/hole_sign_batch.h`), rebuilt only when what they say or
where they stand changes (the hub's clocks, once a game minute); never letters made of geometry. Board placement is the
model's `board_center`/`board_size`/`board_normal` points.

**The ball, cup and flag stay readable at every time of day.** Contrast tests
in `tests/content_tests.cpp` check them at every palette key and at night:
when one fails, fix the palette, not the check.

**Sound comes from where it happens.** Every sound in the world (a strike, a
landing, a cart, a bump, a use, mine and others') is pushed with its
position (`src/game/world_sounds.h`) and heard from the camera as drawn,
quieter with distance by inverse distance and panned by direction
(`src/audio/sound_placement.h`), carrying as far as its manifest entry says.
Menus, the HUD, thunder and the ambience say `placed: false`. Birds and the
terrace's chatter are placed loops (`hub.json` `ambience`, category `world`); the PA
call, the bus's horn and its idling engine come from the hub's model points
(`play_area::hub_sounds`). Range strikes of other players reuse the club's hit
sound, which carries as far as its `max_distance`. The hub bus's door leaf (part tag
`door`) swings about the model's `door_hinge` while it boards, over the model's
`numbers` `door_swing_seconds` to `door_open_degrees`. The open state is the game's
(`bus_doors_open`); the renderer only advances the swing with frame time and draws
the leaf each frame (`append_bus_doors`), so the static props never rebuild for it.
A new course, or a clock not yet known online, stands the doors at the state.

**Text always goes in a box.** Draw with `draw_label` / `layout_text`
(`src/renderer/pixel_font.h`) into a `ui_rect` cut from its panel, never at a
free point or a hand-picked size; the layout picks a whole-number scale, wraps
or shrinks, then ellipsizes. A test draws every screen at 4:3, 16:9 and 21:9
and fails on any cut-off label: make the box bigger or the string shorter.

**The camcorder, tape and TV passes are never optional.** Scene (low-res HDR)
→ camcorder (`camcorder.frag`: iris, white balance, tone curve, bloom, smear,
gain noise; the rangefinder's eyepiece is part of the scene) → tape
(`tape.frag`) → overlay (HUD, menus, datestamp, never bloomed or noisy) → TV
(`crt.frag`, window resolution, last; nearest vertically, soft horizontally).
The camcorder doesn't repeat what the tape owns. Paused, the scene and
camcorder hold their last frame while the tape plays on. The tape wear setting
blends the whole tape look between `picture.json`'s fresh and worn. The line count is art direction (`target_lines`), never a setting;
each scene line is a whole number of window rows. Tape effects are sized as
fractions of the picture height. Nothing flashes the whole screen during play
(never at address or during a shot; lightning only while walking). The
renderer's clock never reaches gameplay. For every rendering decision, ask
whether it looks more like a consumer camcorder tape from 1989 or less.

**Water reflects the real scene.** Each visible water level gets a mirrored
scene pass, at quarter scene resolution, clipped at its surface (`reflection_pass.h`,
`water_reflection.h`). Current-frame depth queries reject occluded water
planes before drawing; no previous frame decides visibility. It draws the ordinary backdrop, ground, grass, full
trees, props, figures (mine too, with my gear) and flags through the same lighting path. The reflected
low mist follows the two physical legs above the surface. Water samples that
image, distorted by its world-fixed waves; there is no screen march or sky
fallback. The sky disc is integrated by the filtered glint, so it is not also
painted into the reflected backdrop. Ripples too small to resolve pass their slope to reflection blur,
projected-area Fresnel filtering and the broadened sun glint in `water.frag`
(`water_light.h` mirrors Fresnel for tests). Fading normals must never turn a
windy pond into a flat mirror. Wave faces pointing into the next wave see the
water's own light. Shallow angles reflect more; never fix a pale pond with
exposure, a distance fade or a reflection cap. `GPU REF` measures the extra
scene passes separately. Lower their resolution if needed, never substitute
a different scene. The rendered water check includes an off-screen tree.

**The rough grows only on the rough.** The zones' overlay alone draws their
shapes: the ground under it is drawn in what `sample_area` finds there with
no zone (`make_ground_render_mesh`), so no zone colour leaks past its outline.
At course load, `bake_grass_field` samples the actual ground and the exact bare
triangles of zones (ponds included), paths and tee footprints on one
world-fixed 0.5 m field. A pond bed may keep the bank's grass colour without
growing blades.
A spatial index of the exact bare edges keeps the bake local. It keeps
height, grass kind and distance to bare ground (capped at 32 m, beyond the largest accepted blade reach).
A root grows only where interpolated distance clears its maximum blade reach
plus half the field cell's diagonal (0.354 m). The same clearance holds in every
ring. Thin bare slivers are included even between field nodes. Each tuft's
root, yaw, height and lean come from its finest world cell; coarser aligned
blocks retain their lowest-ranked member. A retained tuft is drawn once,
widens continuously with distance, and thins by width. Nothing remaps or
reseeds as the eye walks. `tooling/picture_check` checks bare-ground exclusion,
retained root identity while walking, and ground-level fairway views; the
`GRASS_DEBUG_RINGS` shader define colours the density levels.

**One model at every distance.** Trees have one full model per species variant
in the picture and both kinds of shadow map. There is no sparse tree or tree
LOD threshold. The rendered check compares every species immediately either
side of 70 m. Flagsticks are fixed cylinders at their real radius in every
pass; they never turn toward the eye or widen with distance.

The representation audit lives with these checks: frustum/chunk culling and
shadow-map coverage select geometry, without replacing it (`render_culling_tests`,
`shadow_map_tests`); grass selects nested tufts (`grass_field_tests` and the
rendered walking check); water filters unresolved slopes with their energy
preserved (`water_light_tests`); ground textures use mipmaps and mowing uses
view-dependent surface response, without changing stripe positions. Weather
streaks select periodic world-space copies: moving the selection box does not
move an interior streak (`weather_frame_tests`). Overlay and viewmodel camera
rigs are presentation, outside the world batches. Any added representation
switch needs its own projected comparison before it ships.

**Drawing stays cheap.** Every effect runs at scene resolution or smaller;
only the TV pass runs at window resolution. Static geometry is uploaded when
`terrain_render_revision` changes, which only a course load does: a hole
starting or ending never rebuilds the area or its meshes. A saved
`lighting.json` or `picture.json` (`src/game/look_reload.h`) rebuilds only
the passes its changed sections feed (`renderer::set_look`); ground colours,
baked into the meshes, show from the next course load (the tour loads its
course again). Every pass has its
own `gpu_profile_stage` (`docs/performance.md`). Pick shader paths with
separate programs or `#define`s (`load_from_files`' defines), not a branch on
a uniform. Lighting, sky, camcorder, shadows, sun shafts and night lights
together cost at most 1.0 ms of GPU at
1080p on the owner's machine (release, `GOLFPP_VSYNC=0`,
`marienlyst_golfklub`).
