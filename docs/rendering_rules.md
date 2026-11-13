# Rendering, UI and sound rules

Read before changing the corresponding systems, including callers and content.
Paths are relative to the repository root.

**One lighting path.** All scene light and haze come from
`assets/picture/shaders/lighting.glsl` and its `frame_lighting` block, uploaded
once per frame; no shader has a light or haze uniform of its own, and the
clock moving changes uniforms only, never a mesh. Nothing glows at night
unless the palette gives it a glow floor, it is a lit lamp's face or a night
light reaches it. Light after dark comes from something visible holding it
(the ball, headlights, a floodlight), never a point in the air. A new light is
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
the nearest floodlights' beams (`lamp_shadow_pass.h`, maps drawn only when a
lamp or the casters change). A thin mist lies all day, so the view fades out
within a kilometre and shafts are faint in clear noon air, strong in mist and
fog, and a beam is bright at the lamp and faint by the ground. Not
done: a 3D grid of light cells (froxels) needs compute shaders, which macOS's
GL 4.1 lacks, and costs too much as fragment shaders; screen-space god rays
work only with the sun on screen and read as a lens trick. Tune the air by eye
in the visual tour with the F2 panel (`docs/development.md`).

**Words in the world are sign faces.** Hole signs, way signs and prop boards (the
bus's destination, the tee book's title) are painted with the pixel font into
small textures (`renderer/hole_sign_batch.h`), rebuilt only when what they say or
where they stand changes; never letters made of geometry. Board placement is the
model's `board_center`/`board_size`/`board_normal` points.

**The ball, cup and flag stay readable at every time of day.** Contrast tests
in `tests/content_tests.cpp` check them at every palette key and at night:
when one fails, fix the palette, not the check.

**Sound comes from where it happens.** Every sound in the world (a strike, a
landing, a cart, a bump, a use, mine and others') is pushed with its
position (`src/game/world_sounds.h`) and heard from the camera as drawn,
quieter with distance by inverse distance and panned by direction
(`src/audio/sound_placement.h`), carrying as far as its manifest entry says.
Menus, the HUD, thunder and the ambience say `placed: false`.

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
