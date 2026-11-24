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

**One OpenGL version (owner decision, 2026-11-20).** Every target runs the
same OpenGL 3.3 core / GLSL 330 path, which macOS supports. No faster path
for newer GL on some platforms: anything not in 3.3 (base-instance or
indirect draws, compute, persistent mapping, bindless textures) is out.

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
(`src/renderer/prop_renderer.h`, `prop.vert`, `prop.frag`): lit where it is in the world by
the sun, the shadow map and the night lights on the side facing them; a new
model file needs no shader or drawing code. Everything standing in the world
casts the sun's shadow: a model casts from the prop path's own uploads
(`prop_frame.h`), anything drawn outside it (flagsticks, signs, fence posts,
figures) from the world markers' upload or the static caster list
(`src/renderer/shadow_casters.h`). What never moves is uploaded once per course
(placed models, sign and fence casters, tee boxes, cups) and carries its glow
floors as slots (`prop_glow.h`) the frame fills, so night glow is never baked
into it; what moves is built and uploaded once a frame (`stage_frame_geometry`),
only when it changed, and every view and the shadow map draw the parts they
show from that upload.

**A knocked-out golfer's body is the golfer's figure lying on its back**
(`src/renderer/lying_body.h`, instances in the markers' avatars part), head
at its yaw's end, every part inside the body's solid box and resting on the
ground under it; its bag lies beside it (`lying_bag_frame`). Presentation only:
`core/render_bodies.h` reads `bodies_in_world`.

**The crowd is instances, never rebuilt vertices.** Every piece of a model,
and every part of a figure and its ball, is a `mesh_instance`
(`src/renderer/mesh_instance.h`): the matrix that places one of three canonical
meshes (a unit cylinder, sphere or quad, uploaded once by `instance_meshes.h`),
its colour, alpha and glow floor, 68 bytes. The vertex shaders (`prop.vert`,
`world_marker.vert` with `INSTANCED`, `shadow_map.vert` with `MODEL_CASTER`,
all through `mesh_instance.glsl`) place the mesh, so no placed vertex is built
on the CPU or uploaded: forty walking players cost about 300 KB a frame, not
12 MB. A view draws each part as one instanced draw per mesh, by
`glDrawArraysInstanced` with the attribute pointers moved to the first instance
(OpenGL 3.3: no base instance). A new moving thing is drawn by appending
instances to the prop batch or the marker batch, never by transforming vertices;
the marker batch's world-space triangles (cups, flags, poles, collectibles,
feet, tee boxes) are the exception, kept as vertices because they are not a
placed canonical mesh or are uploaded once. A draw is by mesh, not in the order
appended (blended pieces only ever mix spheres, which stay in order). The
shadow map's tracking of moving casters follows the corners of each instance's
mesh box (`placed_corners`), never fewer movements than its vertices make.
Tests and the one geometry query that needs placed vertices (a rolled cart's
lowest point) use `opaque_vertices()` and `instanced_vertices()`, which place the
canonical mesh by the same matrix; a placement test projects those, so a wrong
sign fails.

The sun shadow map is centred on the camera's position, never offset by its
look direction: turning the camera must not move the coverage or its fade
through the world. It is a cached image, drawn again only when it can look
different (`shadow_map_pass.h`): a caster, the sun's fit (the camera leaving its
texel, the sun turning `map_sun_degrees`) or sway moved a leaf more than
`map_pose_texels` of a map texel from where the image drew it
(`tree_pose_drift.h`, `caster_motion.h`). The wind changes the cached image,
never the static buffers. Every shader that includes `shadow_map_light.glsl` is
told its texture unit by `shadow_map_pass::bind` (the one place);
`golfpp-picture-check` reads it back from every linked program.

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
lamp, the casters or their wind pose change by `map_pose_texels`). A thin mist lies all day, so the view fades out
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
model's `board_center`/`board_size`/`board_normal` points. Way-sign lettering is one
fixed scale (the `way_sign_number` style) at one texel density: the board's length
grows with its label (`way_board_length`, `way_sign` tuning), never the text shrinks.

**Maps are drawn from game surfaces, never render meshes.** The paper course map
and the hole signs' faces classify the ground by `surface_at`
(`src/game/area_surface.h`, the one rule `sample_area` plays by: zone, else the
hole's ribbon, else rough) and print it in the maps' own inks (`lighting.json`
`map`, `src/renderer/map_ink.h`), never the 3D ground's colours, as flat ink. A
hole sign's map prints a zone's or a hole's ground and leaves the rest paper;
the course map prints every texel with ground in the surface's ink, the rough
included, and shows paper only where there is no ground. Trees on the course
map are canopy discs of the crown's real radius (at least `least_tree_radius`
texels) with a shadow to the lower right and a one-texel outline round the wood
they form; a flag marks every pin (`src/renderer/map_texels.h` is the printing
they share). The course map's turn is the angle (a degree, refined to a tenth)
at which the course fits its paper largest; the importer's `map_turn` chooses
the same, to size the ground. The course map's
sheet (`src/renderer/course_map_sheet.h`) is built from the `play_area` alone once per
course load and painted with `overlay_raster.h`; it is static, never picking out
the hole played or drawing lines of play (only the player, ball and bag icons
move); everything it prints (cart paths, fences, trees, pins) comes from
the area's data. No map reads a render mesh, vertex colour, material overlay, turf,
grass, pond-bed mud, chunk, cull or GPU buffer, so changing how the 3D ground is
drawn cannot change a map. Tests print every shipped course's map and check its pin
on green, its tee, its greens' area and that the paper's ground has no gaps.

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
blends the whole tape look between `picture.json`'s fresh and worn. The line count is art direction (`picture.json`), not a free
number: the player picks one of its `scene_lines` presets (360, 720; the setting
"picture detail", 360 the default and the performance reference). **Owner decision
(2026-11-22):** these presets replace "never a setting"; 480 and 540 were dropped
because the HUD's whole-pixel blocks need whole multiples of `ui_lines` (360) and
because 480/720 resolved identically on a 1080 window; 1080 was dropped as too costly. Each scene line is a whole
number of window rows (`picture_size.h`); square pixels, even scaling and the whole
pipeline hold at every preset, and the two presets resolve differently at every
window height (the scene never exceeds one scene pixel per window pixel). Effect sizes keep their size on screen: tape effects (grain, blur, wobble,
dropouts, tracking, head switch), bloom, smear and halation are fractions of the
picture height; the HUD, text, datestamp, the camcorder's gain grain and the TV's
horizontal beam blur are in "UI pixels", blocks of `unit` scene pixels
(`picture_size::unit`, about `scene lines / ui_lines`), so they look the same at every
preset; scanlines fade with window rows per scene line (none at one). Deliberately
fixed in scene pixels instead: anti-aliasing floors (`blade_min_pixels`, flagpole
width, the eyepiece mask margin), so a finer scene shows finer detail. Layout
tests cover every preset's UI grid. Nothing flashes the whole screen during play
(never at address or during a shot; lightning only while walking). The
renderer's clock never reaches gameplay. For every rendering decision, ask
whether it looks more like a consumer camcorder tape from 1989 or less.

**The eyepiece's throwaway is not shaded.** With the rangefinder up, the circle
of the main picture and the corners of the zoom's square are replaced, so the
renderer writes the nearest depth there first (`eyepiece_mask.frag`,
`eyepiece_pass::mask_scene`, `begin_zoom`) and the depth test rejects
everything, and a terrain chunk, tree cell or grass block whose bounds project
wholly inside the circle is not submitted (`frustum_hides_aabb`; shadow maps
and mirrors never carry the disc). The mask stops `eyepiece_mask_margin_pixels`
short of the edge, past the sun shafts' two-pixel depth reads, so every pixel
the eyepiece keeps is bit-identical (`golfpp-picture-check --eyepiece`). With a
pond in either view, or the eye under water, nothing is masked: water reads
what lies beside its pixel (`water.frag`). The trees' depth-first decision
still counts the hidden ones, so the draw mode never changes.

**Water reflects the real scene.** Each visible water level gets a mirrored
scene pass, at quarter scene resolution, clipped at its surface (`reflection_pass.h`,
`water_reflection.h`). Current-frame depth queries reject occluded water
planes before drawing; no previous frame decides visibility. The pass is cropped
to the picture rectangle the water's lookups reach (`water_crop.h`): a crop matrix
on its projection, at the same quarter-scale texel density, so terrain, trees,
grass and props are selected by the crop's tighter frustum. The rectangle is the
water's clipped footprint widened by the most `water.frag` can move a lookup
(its tilt, smear and slopes, from the style and weather: `water_reflection_reach`
mirrors the shader, and a test reads its constants from `water.frag`); where it
cannot be bounded the whole picture is captured. Change that lookup and the
reach together. The whole-picture capture is the test oracle, not a path. It draws the ordinary backdrop, ground, grass, full
trees, props, figures (mine too, with my gear) and flags through the same lighting path. The reflected
low mist follows the two physical legs above the surface. Water samples that
image, distorted by its world-fixed waves; there is no screen march or sky
fallback. The sky disc is integrated by the filtered glint, so it is not also
painted into the reflected backdrop. Ripples too small to resolve pass their slope to reflection blur,
projected-area Fresnel filtering and the broadened sun glint in `water.frag`
(`water_light.h` mirrors Fresnel for tests). Fading normals must never turn a
windy pond into a flat mirror. Wave faces pointing into the next wave see the
water's own light, as the share of the undrawn slopes that tilt the ray down
(`water_masked_share`), never a per-pixel noise pick, which speckles far water. Shallow angles reflect more; never fix a pale pond with
exposure, a distance fade or a reflection cap. `GPU REF` measures the extra
scene passes separately. Lower their resolution if needed, never substitute
a different scene. The rendered water check includes an off-screen tree and
compares every cropped mirror with the whole-picture one.

**The ground has coarser levels for mirrors.** At course load each terrain chunk gets `terrain_lod.levels`
index lists on the same vertices (`render_mesh_lod.h`), thinned by collapsing vertices that touch no other
chunk and no mesh edge, so a chunk's border never changes and chunks at different levels meet without a
crack. Turf and blade kind never move (a vertex that differs from a neighbour is kept) and colour, normal and
sky values move by at most `attribute_error`. Level k is within `finest_error_meters * error_ratio^(k-1)` of
the full mesh, which stays the oracle and the only ground in the picture and zoom.
A mirror draws a chunk as the coarsest level whose error, at the chunk's nearest point to the mirrored eye,
spans at most `mirror_max_error_pixels` of the mirror's own picture, as the trees' `max_error_pixels` does.
The material overlay and cart paths stay full detail and are lifted over a coarse ground by that error
(`u_lift_per_depth`, `terrain.vert`), so a coarse ground that rises over the full one never buries a green or
bunker; the grass field, tee boxes and props are placed on the full ground and sit at most that far
(sub-pixel) off the coarse one. The levels are built only when a course loads, and cost index memory
(the ledger counts it): keep it inside the 256 MiB logical cap. `golfpp-picture-check --water` compares
pond mirrors through the whole renderer with the full ground, and draws them at 8 pixels, which must fail.

**The ground's vertex is 32 bytes** (`render_terrain_vertex`, `terrain_vertex.h`): position as floats, the normal as
`GL_INT_2_10_10_10_REV`, colour, rough, paved and sky as normalized bytes, turf and blade kind in steps of 1/127
(0, 1, 2 exact) and the eight horizon sines as bytes. Every setter rounds the way the GPU reads the value back, so the
coarse levels' attribute error, the grass field and the cart paths see the vertex as drawn; the rounding is fixed steps of
the value (0.1 degrees of normal, 0.002 of colour), never of the view. Add an attribute through the setters, not a field.

**The rough grows only on the rough.** The zones' overlay alone draws their
shapes: the ground under it is drawn in what `sample_area` finds there with
no zone (`make_ground_render_mesh`), so no zone colour leaks past its outline.
At course load, `bake_grass_field` samples the actual ground and the exact bare
triangles of zones (ponds included), paths and tee footprints on one
world-fixed 0.5 m field, one 32 m tile at a time (`grass_baker.h`): the dense
field never exists, a tile's nodes are what one dense raster gives (later
triangles decide height ties) and no tile is rerasterized when the eye crosses
a cache window. A pond bed may keep the bank's grass colour without
growing blades.
A spatial index of the exact bare edges keeps the bake local. It keeps
height, grass kind and distance to bare ground (capped at 32 m, beyond the largest accepted blade reach).
Tiles are kept packed (`grass_pack.h`): height a whole number of 1 mm steps on
one global step from a per-tile integer base (so a shared border node decodes
identically; float heights where relief exceeds 65 m), clearance and kind in
their GPU encodings, five bytes a node at most; a tile no accepted blade can
grow in is not kept. The packed tiles (164 MiB at Hedensted) live in a
disposable cache file (`grass_cache.h`, `<pref>/cache/grass`, keyed by a hash of
every input of the bake and the format, rebuilt when stale or damaged, no
migration), so the next load skips the bake; if no file can be made the field
stays in memory (over the RAM budget, reported as `grass_storage=memory`). A
tile that cannot be read is not drawn and is retried after a wait.
A root grows only where interpolated distance clears its maximum blade reach
plus half the field cell's diagonal (0.354 m). The same clearance holds in every
ring. No blade grows over a cell with a node that has no ground.
The GPU holds the field as fixed 32 m tiles (64 cells and a shared closing node)
on that one global lattice: height R32F, clearance R16 rounded down (never up),
kind R8_SNORM, one array layer per resident tile, a tile table, and no
filtering across tiles. Clearance is interpolated in the shader from its texels,
so rounding down is the only error beyond the 0.354 m (its margin is 4.5e-4 m;
a step is 4.9e-4 m, and rounding down only removes blades). Residency (an LRU
cache of at most 7 MiB of layers, `grass_tile_store.h`) chooses which immutable
samples are stored, never which roots exist: a draw first makes the tiles its
visible blocks stand in resident, so no frame shows missing grass; each frame a few tiles round the eye are
fetched ahead (all of them when the field or the eye jumped, before the views),
only into layers no draw used recently, so turning finds them resident; tuning
asking for more tiles than fit is drawn in batches, each tuft in the batch that
holds its tile. Tile bounds select blocks; nothing scans the terrain per view. Thin bare slivers are included even between field nodes. Each tuft's
root, yaw, height and lean come from its finest world cell; coarser aligned
blocks retain their lowest-ranked member. A retained tuft is drawn once,
widens continuously with distance, and thins by width; its drawn width never
falls below `blade_min_pixels` (the blade fades in proportion, alpha blended),
so blades finer than a pixel keep their coverage as the eye moves. Nothing remaps or
reseeds as the eye walks. `tooling/picture_check` checks bare-ground exclusion,
retained root identity while walking, ground-level fairway views and the
rendered rough's motion (`--motion`: scene against a twice-the-lines
reference, by distance band); the
`GRASS_DEBUG_RINGS` shader define colours the density levels.

**Messages say how serious they are.** A message is a status or an error
(`game/message_severity.h`), named by its producer and carried to the picture:
`game_notice::severity`, `render_data::notice_severity`, `render_startup_menu::severity`
(set with `set_message`, which replaces text and severity together, so a retry
never inherits an earlier error). Never infer it from the wording, a text key's
prefix or where the message came from. Only an error is red, and fully: the `error`
text style, border and background of the HUD notice; a status uses the neutral
style, border and panel. Refused actions, an instruction explaining a refusal and
failed requests are errors; progress (waiting on the server, ball preparation,
checking bookings), a group wait and a rules consequence (a stroke penalty) are
statuses. Travel is shown by the travel screen alone, with no HUD notice; the HUD
shows one notice, an error before progress.

**The effect shelf** is two rows of small squares (12 UI pixels, buff-icon style) left of
the club label spanning its height, one square per running effect source (an item with
three effects is one square), newest leftmost in the top row, a full row continuing in
the one below, its seconds under it (`45`, then `2M` past 99 s; none while equipped). Built from
`current_effects` (`core/effect_shelf_frame.h`, words from en.json), laid out once in UI
pixels for drawing and hit tests (`renderer/effect_shelf_layout.h`: square on screen, a
`+N` square when the rows would reach the mode label) and drawn by
`renderer/effect_shelf_overlay.h` with code-drawn icons (`effect_icons.h`; a shipped
effect naming an icon it cannot draw fails a test). Holding the right button while
walking on foot (`wants_effect_cursor`, `core/effect_cursor.h`) frees the mouse; the
square under it shows a tooltip. The mouse's counts never turn the look while it is
free or for two frames after it is captured again.

**Screen vignettes** (the dazed red edge) are overlay quads (`renderer/vignette_overlay.h`),
drawn first in the HUD so the tape and TV take them: the event's colour at the screen
edge, clear a fixed way in, its strength falling evenly from 1 to 0 over the vignette's
seconds from the effect's start (`game/vignette_strength.h`, from `current_effects` and
the shared clock). Presentation only.

**A real lightning strike is drawn on the struck golfer, in the world.** Random lightning
is decoration in the sky (`backdrop.frag`, `render_lightning::bolt`), held back while
aiming or a shot is in the air. A strike on a golfer (the server's `lightning_strike_event`
in the room, or mine offline; `core/lightning_frame.h` `strike_at`) is never held back and
is not drawn in the sky pass: `renderer/world_bolt.h` builds a jagged camera-facing ribbon
channel with a few branches from `world_bolt_height_meters` over the golfer down to their
feet (shape from the strike's seed), and `world_bolt_renderer` adds it in the scene pass
(depth-tested, never written, so ground and trees hide it), flickering with the flash; the
sky still lights the clouds over the golfer's bearing. Its thunder is heard after the sound
has come the distance from the camera, the close clap or the far rumble as for any strike.
Presentation only.

**XP drops** are an icon and `+N XP` text with no panel, right-aligned under the club
label (`renderer/xp_drop_layout.h`), one per award (no stacking; gains below
`min_visible_xp` pool until visible). Each falls a short way and fades over
`xp_drops.lifetime_seconds`; simultaneous drops are spaced a line apart, newest on top,
and only the newest few show.

**One model at every distance.** Trees have one full model per species variant, indexed with bit-identical vertices shared and the triangle order kept.
Its leaf clumps' cards are drawn in every season and every pass (picture, depth first, bake, sun and lamp
shadow maps, mirrors) and cut by one function (`tree_leaf.glsl`): leaves, withered leaves and a spray of fine
twigs, so a bare crown's shadow is its twigs'. Twigs finer than `twig_min_pixels` are drawn that wide and
thinned in proportion by a hash fixed on the card (hashed alpha testing), never blended, so a bare crown
neither shimmers nor darkens with distance (`golfpp-picture-check --trees` walks past them against twice the
lines); the bake draws each view `bake_supersample` times finer and averages it, so an image's coverage is the
twigs' real share of a texel. Branching follows space colonization
([Runions et al. 2007](https://www.algorithmicbotany.org/papers/colonization.egwnp2007.pdf)) with the pipe
model's exponent near 2 ([Eloy 2011](https://ar5iv.labs.arxiv.org/html/1105.2591)); twig colours follow
winter twig guides ([Forestry Commission](https://cdn.forestresearch.gov.uk/1985/03/fcbk020_2ed.pdf),
[Woodland Trust](https://www.woodlandtrust.org.uk/blog/2026/02/winter-tree-identification/)). Falling leaves'
numbers are estimates: leaves per square metre of crown from a leaf area index of about 6 over a 25 cm² leaf,
about 1 m/s falling.
Shadow maps always draw it; the picture draws it near the eye and, once the
image of that same model baked for the course's season (a view grid, unlit, lit
at run time by the same `tree_light.glsl`) is within `max_error_pixels` of the
pass's own picture, draws that image. There is no sparse tree and no fixed
distance threshold: the error bound, measured in `--tree-images`, decides, per
pass. Every tree's instance, tint, variant and wind-proof bounds are
built once per course revision into one static buffer (`tree_index`); a view
only selects runs of it, and the clock and wind are uniforms. Whole crowns that
would cover the picture `depth_first_coverage` times over are drawn twice, depth
first with the casters' shader (`invariant gl_Position` in both vertex shaders)
and then lit where the depth is equal, so leaves overlapping many deep are lit
once. Flagsticks are fixed cylinders at their real radius in every
pass; they never turn toward the eye or widen with distance. Falling leaves (`falling_leaf.h`, presentation
only) are a pure function of a near tree's instance, the leaf's slot and the clock, drawn from the near runs in
the picture, zoom and mirror passes, never in a shadow map or bake, and only where a leaf covers at least
`trees.falling_leaves.min_pixels` of the pass's picture (smaller is below what it resolves); the rendered check
replays `falling_leaf.vert` against the function.

The representation audit lives with these checks: frustum/chunk culling and
shadow-map coverage select geometry, without replacing it (`render_culling_tests`,
`shadow_map_tests`); grass selects nested tufts (`grass_field_tests` and the
rendered walking check); water filters unresolved slopes with their energy
preserved (`water_light_tests`); ground textures use mipmaps and short turf's
fibre fades at the pixel footprint with its slope variance kept in the wet shine
(`turf_fibre`, `wet_gloss`), fixed in the world. Weather
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
a uniform. A pass never reads GL state back (`glGet*`, `glIsEnabled`): each
blocks the app thread until the driver has drained its queue. The renderer
passes the view's `draw_target` (`renderer/draw_target.h`: framebuffer, size,
scissor) down instead; `state_readbacks` in the frame accounting counts the
rest and is zero in a warm stationary view. Lighting, sky, camcorder, shadows,
sun shafts and night lights together cost at most 1.0 ms of GPU at
1080p on the owner's machine (release, `GOLFPP_VSYNC=0`,
`marienlyst_golfklub`).
