# Code map

Entry points for finding the code relevant to a task. Paths are relative to the repository root.

| To change… | Start at |
|---|---|
| Frame loop, saving, audio dispatch | `src/core/app.cpp` |
| Sound in the world: where each is heard from, how far it carries | `src/audio/sound_placement.h`, `src/game/world_sounds.h`; each sound's `reference_distance` and `max_distance` (or `placed: false`) in `assets/sound/sounds.json` |
| Keys → game actions | `src/core/key_bindings.cpp` |
| Menus (new screens go here) | `src/core/startup_flow.h`, `src/core/online_menus.h`; drawn by `src/renderer/menu_overlay.h` |
| Taking a cart or carrying my bag for a round (the transport picker) | `pick_transport` in `src/core/startup_flow.h`, `game_state::takes_cart`, `own_cart` in `src/game/golf_cart.h`; `join_course`'s `takes_cart` in the server |
| What the renderer gets each frame, camera rigs | `src/core/render_frame.h`, `src/renderer/render_data.h` |
| Per-frame gameplay | `src/game/game_state.h` |
| Standing over my ball: the setup's dials and limits, my hand, the target and down views and R, the swing (swipe back, swipe down), a mistimed strike | `src/game/shot_setup.h`, `src/game/address_play.h`, `src/game/address_view.h`, `src/game/swing.h`, `src/game/shot_ability.h`; the tuning's `setup`, `swing`, `golfer` and the camera's `address_*` |
| The OSD at address: the shot preview (a fading start line), the setup word, the swing clock, where my bag is | `preview_shot` in `src/game/address_play.h`, `src/renderer/osd_overlay.h`; the tuning's `shot_preview` |
| My shot's number as it is struck (its neutral carry, yellow on a crit) | `src/game/shot_number.h`, `src/renderer/shot_number_overlay.h`; the tuning's `shot_numbers` |
| A golfer's ability and a shot's dice: shape weights, the setup word, spreads, the natural shot, seeds | `src/game/golfer.h`, `src/game/shot_ability.h`, `src/game/shot_dice.h`; `assets/play/tuning/golfer.json`; the natural shot is a golfer setting (`assets/interface/settings.json`) and the picker in `src/core/startup_flow.h` |
| My bag and the club in hand: B, F at the bag, the picker; a bag on anyone's cart, off a crashed one | `src/game/golf_bag.h`, `src/game/bag_place.h` (`cart_owner`); the bag item in `assets/play/items/bag/`, its model; drawn by `src/renderer/bag_batch.h`, `src/core/render_gear.h` |
| Handling a bag or swapping a club, mine and others' (the moves, the figure reaching) | `src/game/bag_handling.h`, `follow_bag` in `src/game/remote_players.cpp`, `add_handling` in `src/core/render_gear.h`; the tuning's `bag_handle_seconds`, `club_swap_seconds` |
| Whose colour a bag and cart wear (the favourite colour setting) | `src/game/gear_paint.h`; `paint` in a model's prop file; `bag_paints` and `cart_paints` in `lighting.json`'s `figures`; `set_golfer` in the server |
| What moves that a shot meets (people, carts, balls at rest), the shooter's grace, the 2-stroke ball penalty | `src/game/shot_snapshot.h`, `simulate_struck_shot` in `src/game/shot_simulation.h`; the tuning's `solids` |
| Courses, holes, the area played on; my ball's life on them (teed, played, holed, given up) | `src/game/course_session.h`, `src/game/play_area.h`, `src/game/course_loader.h`; content in `assets/world/courses/` |
| The practice ground: range bays and stations by a green, no scores, XP or carts | `practice` and `carts` in `src/game/course_definition.h`, `tee_up` in `src/game/course_session.h`; made by `tooling/practice_ground/make_practice_ground.py` |
| Greens, bunkers, water and fairways: a zone's outline (its edge the curve through its mapped points, `rounded_outline`; inside, metres from its edge, its triangles), bunkers and ponds carved into the ground, a pond's level | `src/physics/material_zone.h`, `carved_height` in `src/physics/terrain.h`, `build_ground` in `src/physics/ground_mesh.h`; the tuning's `terrain` (`bunker_lip_meters`, `water_bank_meters`, `water_shore_meters`) |
| What F acts on in reach (my ball, a tee, a collectible, my bag, a tipped cart) | `src/game/interactables.h` |
| A shot's flight and playback | `src/game/shot_simulation.h`, `play_shot` in `src/game/game_state.h`; `src/physics/` |
| How a ball leaves the club and flies: launch, lift and drag, the air | `src/physics/launch.h`, `src/physics/flight_model.h` (`air_at`), `src/physics/ball_physics.h`; the ball's fit in `assets/play/tuning/ball.json` (`flight`, `air`), each course's `air` in its `course.json`; the reference in `tests/fixtures/reference/` |
| The clubs and what a swing does to the ball: delivery, impact, the D-plane | the club items in `assets/play/items/club/` (their `spec`; `src/game/club_definition.h`), `src/game/delivery.h`, `src/physics/impact.h`; `club_strike` in `src/game/shot_simulation.h`; speeds fitted with `tooling/shot_lab/` (`--fit`) to `tests/fixtures/reference/bag_targets.json` |
| Lies: the stance's slope, grass, water and sand between face and ball, tee or ground | `lie_delivery` and `lie_contact_for` in `src/game/delivery.h`, `stance_at` in `src/game/shot_simulation.h`, the sand in `src/physics/impact.h`; the tuning's `lies`; `tests/fixtures/reference/lies.json` |
| The ball on the ground: bounce, roll, stimp; cups and flags; the hole as drawn, cut into the green | `src/physics/turf_contact.h`, `src/physics/cup.h`; `append_cup_holes` in `src/renderer/world_marker_batch.h`, `drawn_ball_center` in `src/core/render_frame.h`; `assets/play/tuning/surfaces.json`, the tuning's `cup` (`cup_leniency`), each course's `green_stimp_feet`; `flagstick_solids` in `src/game/area_solids.h`, P in `flag_in_reach` (`src/game/game_state.h`) |
| Deterministic sine, cosine, atan2, exp, log and pow | `src/physics/det_math.h` |
| Offline vs online: progress, net commands, motion | `src/game/mode_dispatch.h`, `src/game/net_types.h`, `src/game/motion_sync.h` |
| The connection, my room's rows, rejoining | `src/net/net_client.h` over `src/net/stdb_bridge.h` (Rust in `net/client_bridge/`), `src/net/room_rows.h`, `src/core/online_session.h`; `assets/play/online.json` |
| An online round; other players as shown, at their ball from their sent setup; a room's shot replayed (`room_shot_result`) | `src/game/online_play.h`; `src/game/remote_players.h`, `src/game/sent_setup.h`, `src/core/render_online.h` |
| Motion acknowledgements, prediction replay and sync diagnostics | `src/game/sync_reconciliation.cpp`, `src/game/motion_sync.h`, `src/game/cart_prediction.cpp`; fault injection in `tests/sync_harness.h`, `tests/sync_tests.cpp` |
| Walking and driving in fixed steps, and the server stepping a player's inputs | `src/game/movement_step.h` (shared), `src/game/walking.h` (my walking); `server/golfpp_module/src/movement_rules.h` (`play_move`); `tests/movement_tests.cpp` |
| Online content compatibility | `src/game/content_hash.h`, `load_simulation_content_hash` in `src/game/content_files.h`; `server_content_hash` in the module, checked after `server_protocol` by `src/net/net_client.h` |
| The server: tables, views, reducers, its rules | `server/golfpp_module/src/lib.cpp`, `account_rules.h`, `play_rules.h`, `cart_rules.h`, `movement_rules.h`, `server_content.h`, the XP rollup in `progress_rollup.h`; motion interest in `interest_rules.h` and the `my_avatar_motion` / `my_golf_carts` views |
| Models drawn in the world: kinds, `base` variants, mounts | their files in `assets/world/props/` (`src/game/prop_loader.h`, `src/game/model_kinds.h`), `src/renderer/prop_pieces.h` |
| Items: what a player uses and what it does | their files in `assets/play/items/<kind>/` (`src/game/item_catalog.h`); `src/game/effects.h` (`effective_value`); `src/game/loadout.h`, `assets/play/items/default_loadout.json`; `record_item_use` in `src/game/mode_dispatch.h`, `use_item` in the server |
| How every model is lit and drawn | `src/renderer/prop_batch.h`, `src/renderer/prop_renderer.h`, `assets/picture/shaders/prop.vert` / `prop.frag` (`src/renderer/prop_light.h` on the CPU); what is held `src/renderer/held_props.h`, the club at a ball `src/renderer/swing_club.h` |
| The golf cart; walking into a seat's door, scooting across | `src/game/golf_cart.h` (`walk_into_seat`), `src/game/cart_seats.h` (shared with the server, `at_seat_door`), `src/renderer/cart_batch.h`; the cart tuning's `door_*` |
| A cart going over and lying on its side | `src/physics/cart_rollover.h`, `cart_falls` in `src/game/golf_cart.h`, `cart_rolled` in `src/renderer/cart_batch.h` |
| Riding together: the driver's cart sent often to the rider alone | `src/game/ride_sync.h`; the server's `ride_motion` table and `update_ride`; the net tuning's `ride_*` |
| How a cart drives: motor, slopes, grip, tipping, sinking | `src/physics/cart_dynamics.h` (fixed steps); the cart item's `spec` in `assets/play/items/cart/`; the surfaces' `cart` numbers in `assets/play/tuning/surfaces.json` (`cart_ground_at` in `src/game/play_area.h`); `update_motion`, `right_cart` and `tow_cart` in the server; `tests/fixtures/reference/carts.json` |
| Cart paths: laid on the ground, the ball's hard top, carts' path numbers, the lane kept clear beside them, drawn draped on the ground in asphalt | `src/game/cart_path.h`, the `slab` solid in `src/physics/solid.h`, `sample_area` and `cart_ground_at` in `src/game/play_area.h` (trees and fences cleared in `with_cart_paths`, `with_fences`), `src/renderer/cart_path_mesh.h`, `terrain.frag` (`cart_path.bmp` from `tooling/art/make_art.py`); the tuning's `cart_path` and `solids.path`; made by `tooling/osm_import/osm_cart_paths.py` |
| Hole signs, the way signs from each green to the next tee, tee boxes, floodlights, fences | `src/game/hole_sign.h`, `src/game/way_sign.h`, `src/game/tee_box.h` (the ground levelled under it: `src/physics/ground_pad.h`; the tuning's `tee_box`), `src/game/floodlights.h` |
| Anything solid: what the ball, players and carts meet | `src/physics/solid.h`, `src/game/area_solids.h` (`play_area::solids`), `src/game/body_contact.h`, `src/physics/body_collision.h`; restitution and friction by kind in the tuning's `solids` |
| Up and down: the one surface, steps, hops and falls | `sample_area` in `src/game/play_area.h` (the ground or a walkable top), `settle_vertical` in `src/physics/body_collision.h`, `src/game/player_jump.h`; `step_up` in the player's and the cart's tuning |
| The visual tour: a camera flown round a course through every hour and weather (VISUAL TOUR, `GOLFPP_TOUR`) | `src/core/visual_tour.h`; its stops and flight in `assets/picture/visual_tour.json` (`src/game/visual_tour_loader.h`) |
| Tuning the look live: `lighting.json` and `picture.json` reloaded when saved, the F2 panel (in the tour, with the god menu), a JSON number edited in place | `src/game/look_reload.h`, `src/core/look_tuning.h` (`src/core/app_look.cpp`), `renderer::set_look` in `src/renderer/renderer_look.cpp`; the panel's rows in `assets/picture/tuning_panel.json` (`src/game/tuning_panel.h`, `src/game/lighting_numbers.h`); `src/game/json_edit.h` |
| Pacing a putt off (Caps Lock), crouching to read the green (C): the count and the green's dots | `src/game/pacing.h`, `src/game/green_read.h`; `src/core/render_putting.h`, `draw_green_read` in `src/renderer/osd_overlay.h`; `player.json`'s `pacing` and `green_read` |
| Course map (M), rangefinder (Ctrl), god menu (F1, with every solid's outline for checking an import, and the profiler overlay) | `src/renderer/course_map_overlay.h`; `src/game/rangefinder.h`, `src/game/sight_line.h`; `src/core/god_menu.h`, `src/renderer/solid_outline.h` |
| Settings | `src/core/settings_menu.h`, `src/game/settings.h`, `assets/interface/settings.json` |
| XP, collectibles, completion; rewards, skills and their level curves | `src/game/progress_rules.h`, `src/game/skill_catalog.h`, `src/game/reward_rules.h`; `assets/play/progression/`; online, `store_progress` and the golfer tables in the server |
| Feel numbers | `assets/play/tuning/*.json` (`src/game/game_tuning.h`) |
| HUD, scorecards | `src/renderer/hud_overlay.cpp`, `scorecard_overlay.cpp`; GL in `src/renderer/renderer.cpp` |
| Text size and layout | `src/renderer/pixel_font.h`, `assets/interface/text/styles.json` |
| Content formats | the loader next to it (`src/game/*_loader.cpp`, each with `parse_*_from_text`) |
| The weather and what it does to a shot | `src/game/weather.h`, `src/physics/wind.h`, `course_weather_at` in `src/game/mode_dispatch.h` |
| The weather as drawn | `src/core/weather_frame.h`, `src/core/lightning_frame.h`, `src/game/weather_lighting.h`, `src/renderer/weather_renderer.h` |
| Time of day, sun and moon | `src/game/sky_clock.h`, `src/core/sky_frame.h` |
| Scene colours, light, haze | `assets/picture/lighting.json` (`src/game/lighting_loader.h`, `src/game/sun_lighting.h`), `assets/picture/shaders/lighting.glsl`, `src/renderer/lighting_uniforms.h`, `src/renderer/sky_visibility.h` |
| The air: its mist, the low mist lying on the ground, the haze glowing towards the sun, the night lights' beams in fog and rain | `haze_over`, `mist_depth`, `haze_toward` and `scattered_light` in `assets/picture/shaders/lighting.glsl` (on the CPU for tests: `src/renderer/scattered_light.h`), `scattering_under_weather` in `src/game/weather_lighting.h`; each key's `mist_height_meters` and `mist_density` in `lighting.json`, and `mist`, `fog_mist_density`, `rain_mist_density`, `sun_glow`, the `*_visibility_meters` and `*_forwardness` in its `weather`, `beam_near_share` and `beam_core_meters` in its `night_lights` |
| Shadows and night lights | `src/core/light_frame.h`, `src/renderer/shadow_map_pass.h`, `src/renderer/scene_lights.h`, `src/renderer/blob_shadow_renderer.h` |
| Sun shafts: the shadows of trees, people and carts taken out of the haze's sun glow; the trees' and floodlights' shadows taken out of the nearest floodlights' beams | `src/renderer/sun_shaft_pass.h`, `assets/picture/shaders/sun_shafts.frag` and `sun_shafts_composite.frag` (on the CPU for tests: `shaft_shade` and `lamp_shaft_shade` in `src/renderer/scattered_light.h`); the floodlights' maps in `src/renderer/lamp_shadow_pass.h` and `lamp_shadows.h`; `shafts` in `lighting.json` |
| The sky | `src/renderer/backdrop_pass.h`, `assets/picture/shaders/backdrop.frag` |
| The picture: line count, camcorder, tape, TV, datestamp | `assets/picture/picture.json`, `src/renderer/picture_size.h`, `camcorder_pass.h`, `tape_pass.h`, `tv_pass.h` (in `src/renderer/`), `src/core/vcr_frame.h` |
| Static meshes on the GPU | `src/renderer/terrain_renderer.h` |
| Backdrops, ground textures | `tooling/art/make_art.py` |
| Tooling (OSM importer, hole editor, art) | `tooling/README.md` |
| How OSM shapes become zones: simplified outlines, joined bunkers, generated fairways (with their sources) | `tooling/osm_import/osm_outline.py`; `zones` and `generated_fairway` in `osm_golf_convert.py`'s config |
| What OSM leaves out, read from aerial photos and heights: fairways, bunkers, ponds, paths, every tree; one source per area | `tooling/osm_import/osm_aerial.py`, Denmark's `aerial_dk.py`; `aerial` in `osm_golf_convert.py`'s config |
| Shots, driving and walking natively vs in WASM | `tooling/net/check_determinism.ps1` |
| A running server checked by a headless client | `tooling/net/online_check/` |
| What real golf measures, with sources; what each club does | `tests/fixtures/reference/` (the oracle tests check against); `tooling/shot_lab/` |
| What players download | `tooling/release/build_release.ps1` (Windows, the server package), `build_release.sh` and `build_deps.sh` (macOS, Linux; run by `.github/workflows/release.yml`), the `install` rules in `CMakeLists.txt` |

## Shared helpers and coordinates

Reuse instead of writing your own: `src/physics/vector_math.h` (yaw,
horizontal distance, safe normalize, `clamp01`; `glm::radians` and
`glm::pi`, never a local `pi`), `src/game/json_util.h` (every JSON read),
`src/game/content_files.h` (every file read and directory scan: loaders parse
text, so the server parses the same content without a filesystem),
`tests/test_support.h` (fixtures, `started_hole()`, `started_game()`,
`near()`), `src/renderer/camera_local.h` (placing things in a thing's frame).

Every local frame (a prop's, a cart's, a figure's, the camera's) is **x left,
y up, z forward**: a plain rotation, +x on the screen's left seen from behind.
Pose a thing by where its sides point (`posed_model`), never Euler angles.
