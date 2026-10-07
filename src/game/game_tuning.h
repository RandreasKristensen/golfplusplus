#pragma once

// Gameplay feel constants, loaded from assets/tuning/game_tuning.json by
// game/tuning_loader.h. Every field is required in the file; the zero
// defaults here only exist so the structs are plain values. Distances are in
// meters, times in seconds, angles in radians unless named otherwise.

#include "physics/physics_tuning.h"
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
    float zone_cell_size = 0.0f;    // the ground's spacing over bunkers and ponds
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
    float roll_deceleration = 0.0f;  // m/s^2 while rolling (times the club's roll_friction_scale)
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
    float ball_interact_radius = 0.0f;
    // How far the player stands from the ball: behind it when teeing up, to
    // its side (and address_back_distance behind) when addressing it.
    float ball_stand_off_distance = 0.0f;
    float address_back_distance = 0.0f;
    float emote_seconds = 0.0f;
};

struct cart_tuning {
    float speed = 0.0f;
    float turn_rate = 0.0f;
    float drift_turn_rate = 0.0f;
    float normal_damping = 0.0f;
    float drift_damping = 0.0f;
    float drift_duration = 0.0f;
    float drift_speed_boost = 0.0f;
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
    // What is held before my eyes (my cart from the driver's seat, my emote
    // props) is drawn at this field of view, the one it was posed for,
    // whatever the player's setting.
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

// The fastest a cart goes: drifting on a road.
inline float fastest_cart_speed(const cart_tuning& cart) {
    return cart.speed * cart.drift_speed_boost * (cart.road_speed_scale > 1.0f ? cart.road_speed_scale : 1.0f);
}

// Fences (course worlds): their poles stop shots like tree trunks, their nets
// catch them, giving back little of the speed.
struct fence_tuning {
    float pole_radius = 0.0f;
    float net_restitution = 0.0f;
    float net_friction = 0.0f;
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
    std::array<lie_tuning, ball_lie_count> lies{};  // by ball_lie
};
