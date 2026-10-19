#pragma once

// Gameplay feel constants, loaded from assets/tuning/game_tuning.json by
// game/tuning_loader.h. Every field is required in the file; the zero
// defaults here only exist so the structs are plain values. Distances are in
// meters, times in seconds, angles in radians unless named otherwise.

#include "physics/physics_tuning.h"
#include "physics/land_relief.h"
#include "physics/terrain.h"

#include <array>
#include <cstddef>
#include <cstdint>

#include <glm/vec3.hpp>

struct world_scale_tuning {
    float meters_per_world_unit = 0.0f;
    float ball_physics_radius_meters = 0.0f;
    float ball_visual_radius_meters = 0.0f;
    // Drawn and holed at this radius: a ball holes when its centre is inside.
    // Sized to the drawn ball as a real cup is to a real ball.
    float cup_radius_meters = 0.0f;
    float pin_visual_height_meters = 0.0f;
};

struct terrain_build_tuning {
    int min_sections = 0;           // terrain_spline::sample_count
    float ground_cell_size = 0.0f;  // spacing of the ground grid, the one surface of a play area
    // How far from a hole's edge the ground takes to ease into the course's land.
    float ground_blend_distance = 0.0f;
    terrain_zone_tuning zones;      // bunker and water carve depths
    land_relief relief;             // the land's and greens' relief, before the ground is built on it
    float fairway_cell_size = 0.0f;  // the ground's spacing over fairways and greens, on land
    float zone_cell_size = 0.0f;     // the ground's spacing over bunkers and ponds
    // Longest side of a drawn zone shape's outline (build_material_overlay_mesh).
    float material_overlay_spacing = 0.0f;
};

struct ball_tuning {
    float stop_speed = 0.0f;  // below this (and grounded) the ball is at rest
    // A ball lost in water lies on the pond's floor this long before it goes
    // back to where it was hit from.
    float water_linger_seconds = 0.0f;
    float ground_restitution = 0.0f;
    float ground_friction = 0.0f;  // per 1/60 s of contact, see physics/collision.h
    float water_restitution = 0.0f;
    float water_friction = 0.0f;
    // Sand catches a ball: it plugs instead of bouncing and stops within a
    // hand's width, whatever the club's roll_friction_scale.
    float bunker_restitution = 0.0f;
    float bunker_friction = 0.0f;
    float bunker_roll_deceleration = 0.0f;  // m/s^2
    float tree_restitution = 0.0f;
    float tree_friction = 0.0f;
    // m/s^2 while rolling. Only on the green does the club's
    // roll_friction_scale apply: a putt that leaves it is caught by the
    // fairway or rough. A ball holds on a slope while g * sin(slope) is
    // below its surface's deceleration.
    float green_roll_deceleration = 0.0f;
    float fairway_roll_deceleration = 0.0f;
    float rough_roll_deceleration = 0.0f;
    float settle_speed = 0.0f;       // normal speed below which a grounded ball rolls instead of bouncing
    // A ball whose centre passes straight over the cup's centre drops in only
    // below this speed; faster, it rolls over. Off centre it has less of the
    // cup to fall into (a shorter chord), down to cup_lip_capture_scale of it
    // on the lip.
    float cup_capture_speed = 0.0f;
    float cup_lip_capture_scale = 0.0f;
    // A ball landing in the cup from the air may come in faster by this many
    // m/s of horizontal speed per m/s it falls at: a steep dunk drops, a fast
    // low ball skips over.
    float cup_dunk_scale = 0.0f;
};

struct swing_tuning {
    float meter_cycle_seconds = 0.0f;  // one full 0 -> 1 -> 0 swing meter cycle
    float min_power = 0.0f;
    // Launch speed is the club's power times meter power to this exponent:
    // below 1 it flattens the curve, so half power carries about half as far.
    float power_curve_exponent = 0.0f;
    float side_spin_scale = 0.0f;      // multiplies club_stats::side_spin
};

struct player_tuning {
    float walk_speed = 0.0f;
    float turn_rate = 0.0f;
    float aim_turn_rate = 0.0f;
    // Looking around, first person: the arrow keys look up and down at
    // look_pitch_rate (rad/s) as far as max_look_pitch (rad) either way; the
    // mouse turns mouse_look_radians per count at 100% mouse sensitivity.
    float look_pitch_rate = 0.0f;
    float max_look_pitch = 0.0f;
    float mouse_look_radians = 0.0f;
    float ball_interact_radius = 0.0f;
    // How far the player stands from the ball: behind it when teeing up, to
    // its side (and address_back_distance behind) when addressing it.
    float ball_stand_off_distance = 0.0f;
    float address_back_distance = 0.0f;
    float emote_seconds = 0.0f;
    // A hop on foot (game/player_jump.h): up at jump_speed (world units/s),
    // pulled down at jump_gravity (world units/s^2).
    float jump_speed = 0.0f;
    float jump_gravity = 0.0f;
};

// The golf cart (game/golf_cart.h), in world units and seconds.
struct cart_tuning {
    float speed = 0.0f;
    float turn_rate = 0.0f;
    float damping = 0.0f;  // how quickly the cart takes up its speed (1/s)
    float reverse_speed_scale = 0.0f;  // of speed, backing up
    float full_steer_speed = 0.0f;     // steering turns it fully from this speed, less below
    float steering_wheel_rate = 0.0f;  // how quickly its steering wheel follows the steering (1/s), looks only
    // Its two seats, each this far to the side of its middle: the driver's
    // on the left, the passenger's on the right.
    float seat_offset = 0.0f;
    float board_reach = 0.0f;        // from a seat, to step into it
    float step_out_distance = 0.0f;  // from a seat, out to the side, where stepping out stands
    float step_seconds = 0.0f;       // stepping in or out
    // Where a course's carts are parked: beside hole 1's start, this far down
    // the hole and to its right, side by side park_spacing apart.
    float park_ahead_distance = 0.0f;
    float park_side_distance = 0.0f;
    float park_spacing = 0.0f;
    // A road counts as under the cart within half its width plus this margin
    // (and never less than min_road_reach).
    float road_reach_margin = 0.0f;
    float min_road_reach = 0.0f;
    // Hub only: multipliers on cart speed and control on and off roads.
    float road_speed_scale = 0.0f;
    float road_control_scale = 0.0f;
    float off_road_speed_scale = 0.0f;
    float off_road_control_scale = 0.0f;
};

// Camera rigs. "back" is against the aim direction, "side" is to the
// player's left (the address stance side). The field of view is the
// player's setting (game/settings.h), not tuning.
struct camera_tuning {
    float walking_eye_height = 0.0f;
    float walking_look_distance = 0.0f;
    float aiming_back_distance = 0.0f;
    float aiming_eye_height = 0.0f;
    float aiming_look_distance = 0.0f;
    float aiming_look_height = 0.0f;
    float address_side_distance = 0.0f;
    float address_back_distance = 0.0f;
    float address_eye_height = 0.0f;
    float address_look_distance = 0.0f;
    float address_look_height = 0.0f;
    float follow_look_height = 0.0f;
    float transition_seconds = 0.0f;        // blend time on rig changes and teleports
    float transition_jump_distance = 0.0f;  // eye movement per frame that counts as a teleport
    // What is held before my eyes (my emote props) is drawn at this field
    // of view, the one it was posed for, whatever the player's setting.
    float viewmodel_fov_degrees = 0.0f;
};

// The dotted arc shown while aiming.
struct aim_preview_tuning {
    float step_seconds = 0.0f;
    int max_points = 0;
};

// The trail drawn behind a ball in flight.
struct flight_path_tuning {
    glm::vec3 color{0.0f};
    float alpha = 0.0f;
    float min_point_spacing = 0.0f;
    int max_points = 0;
    float line_width = 0.0f;
};

// The "+25 XP" popups.
struct xp_drop_tuning {
    float lifetime_seconds = 0.0f;
    int min_visible_xp = 0;  // smaller gains are pooled until they reach this
};

// Limits on online motion updates (game/motion_sync.h).
struct net_tuning {
    float motion_heartbeat_seconds = 0.0f;     // resend at least this often while moving
    float motion_min_interval_seconds = 0.0f;  // never send motion more often than this
    float motion_correction_distance = 0.0f;   // resend when others' extrapolation is this far off (world units)
    float motion_correction_yaw = 0.0f;        // ...or turned this far off (radians)
    // Others are carried on along their last motion for at most this long,
    // then stand still; a new motion blends in over remote_correction_seconds.
    float remote_extrapolation_seconds = 0.0f;
    float remote_correction_seconds = 0.0f;
    // A shot the server says rests further than this from where this client
    // played it (world units) blends there: by the end of playback, or over
    // shot_correction_seconds once it has ended.
    float shot_correction_distance = 0.0f;
    float shot_correction_seconds = 0.0f;
    float remote_trail_fade_seconds = 0.0f;  // another player's shot trail, after the ball stops
    float group_join_distance = 0.0f;        // G joins the group of a player this close (world units)
    float notice_seconds = 0.0f;             // how long a refusal from the server shows
};

// What the online server allows (server/golfpp_module). Slack covers a
// client's honest rounding and lag, not cheating.
struct server_tuning {
    int room_capacity = 0;
    int group_capacity = 0;
    int name_min_length = 0;  // code points
    int name_max_length = 0;
    float link_code_minutes = 0.0f;  // how long a link code works
    int link_codes_per_hour = 0;     // per account
    int link_failures_per_hour = 0;  // wrong codes per login
    // Between motion updates a player may move at their mode's speed (walking
    // or the fastest cart) times motion_speed_scale, over at most
    // max_motion_gap_seconds, plus a slack of motion_distance_slack that
    // refills at that many world units per second (play_rules.h check_motion).
    float motion_speed_scale = 0.0f;
    float motion_distance_slack = 0.0f;  // world units
    float max_motion_gap_seconds = 0.0f;
    float interact_slack = 0.0f;      // world units past an interaction radius
    float wind_time_slack_seconds = 0.0f;
    // How much earlier than the server's clock the client may restart an
    // emote, or later still use its cigarette (it times them by frames).
    float timing_slack_seconds = 0.0f;
};

// The sign at every hole's tee (game/hole_sign.h), in world units. It stands
// on the tee box at its right edge looking down the hole, its face looking
// across the tee, and its posts stop shots like tree trunks.
struct hole_sign_tuning {
    float edge_inset = 0.0f;      // the board's middle, inside the tee box's right edge
    float forward_offset = 0.0f;  // along the hole from the tee (negative: behind it)
    float look_ahead = 0.0f;  // the hole's direction at the tee: towards its line this far down
    float board_width = 0.0f;   // along the hole
    float board_height = 0.0f;
    float board_thickness = 0.0f;
    float board_lift = 0.0f;    // the board's bottom edge over the higher post's foot
    float post_radius = 0.0f;
};

// The tee box at every hole's tee (game/tee_box.h), in world units.
struct tee_box_tuning {
    float width = 0.0f;   // across the hole
    float length = 0.0f;  // down the hole
    float padding = 0.0f;  // its top over the highest ground it covers
    float sample_spacing = 0.0f;  // between the ground samples that find that height
};

// The fastest a cart goes: on a road.
inline float fastest_cart_speed(const cart_tuning& cart) {
    return cart.speed * (cart.road_speed_scale > 1.0f ? cart.road_speed_scale : 1.0f);
}

// Fences (course worlds): their poles stop shots like tree trunks, their nets
// catch them, giving back little of the speed.
struct fence_tuning {
    float pole_radius = 0.0f;
    float net_restitution = 0.0f;
    float net_friction = 0.0f;
};

// The floodlight poles along every hole (game/floodlights.h), in world
// units: alternating sides down the fairway, one beside the tee, one each
// side of the green. Their poles stop shots like tree trunks.
struct floodlight_tuning {
    float spacing = 0.0f;           // the most between fairway poles, along the hole
    float tee_clearance = 0.0f;     // no fairway pole nearer the tee than this, along the hole
    float green_clearance = 0.0f;   // nor nearer the pin
    float fairway_offset = 0.0f;    // a fairway pole's foot, out past the fairway's edge
    float tee_offset = 0.0f;        // the tee pole's, out past the tee box's left edge
    float green_offset = 0.0f;      // a green pole's, out past the green's edge
    float pole_height = 0.0f;       // its lamp head's middle over its foot
    float pole_radius = 0.0f;
    float arm_length = 0.0f;        // the lamp head, out from the pole towards what it lights
    float obstacle_clearance = 0.0f;  // from a trunk, post or another pole, past its radius
    float nudge_step = 0.0f;        // a blocked spot is tried again this much further out or along
    int nudge_tries = 0;
};

// Where the sky's clock stands (game/sky_clock.h): the same for every
// player, so the sky is the same for everyone at the same moment. The clock
// counts game days, its fraction the time of day in UTC; the date is each
// course's own (course_sky), in `year`. Visual only.
struct sky_timeline {
    std::int64_t anchor_unix_ms = 0;  // the real time at which...
    double anchor_game_day = 0.0;     // ...the game clock read this
    double game_days_per_real_day = 0.0;  // 0: frozen at the anchor
    int year = 0;  // every course's date is in it
};

// What the ball lies on when it is hit: a tee box, else the material under it.
// Underlying values index game_tuning::lies; keep `ball_lie_count` in sync.
enum class ball_lie : std::uint8_t {
    tee,
    fairway,
    rough,
    green,
    bunker,
    water
};
inline constexpr std::size_t ball_lie_count = 6;

// How a lie changes a shot played from it: multipliers on the club's power
// and backspin. From a bunker the club's own bunker_power applies as well.
struct lie_tuning {
    float power = 0.0f;
    float spin = 0.0f;
};

struct game_tuning {
    world_scale_tuning scale;
    terrain_build_tuning terrain;
    physics_tuning physics;
    wind_tuning wind;
    ball_tuning ball;
    swing_tuning swing;
    player_tuning player;
    cart_tuning cart;
    camera_tuning camera;
    aim_preview_tuning aim_preview;
    flight_path_tuning flight_path;
    xp_drop_tuning xp_drops;
    net_tuning net;
    server_tuning server;
    hole_sign_tuning hole_sign;
    tee_box_tuning tee_box;
    fence_tuning fence;
    floodlight_tuning floodlights;
    sky_timeline sky_clock;
    std::array<lie_tuning, ball_lie_count> lies{};  // by ball_lie
};
