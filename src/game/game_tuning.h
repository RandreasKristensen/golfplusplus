#pragma once

// Gameplay feel constants, loaded from assets/play/tuning/ by
// game/tuning_loader.h. Every field is required in the file; the zero
// defaults here only exist so the structs are plain values. Distances are in
// meters, times in seconds, angles in radians unless named otherwise.

#include "physics/cup.h"
#include "physics/impact.h"
#include "physics/physics_tuning.h"
#include "physics/turf_contact.h"
#include "physics/land_relief.h"
#include "physics/terrain.h"

#include <cstddef>
#include <cstdint>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

struct world_scale_tuning {
    float meters_per_world_unit = 0.0f;
    float ball_physics_radius_meters = 0.0f;
    // The ball as drawn: bigger than the real one so it reads at the
    // picture's lines, yet small enough to drop into the cup as drawn
    // (cup_tuning: the cup is drawn as big as it plays).
    float ball_visual_radius_meters = 0.0f;
    float pin_visual_height_meters = 0.0f;
    // What a player or cart bumps into at a standing flagstick.
    float flagstick_radius_meters = 0.0f;
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
};

struct ball_tuning {
    float stop_speed = 0.0f;  // below this (and grounded) the ball is at rest
    // A ball lost in water lies on the pond's floor this long before it goes
    // back to where it was hit from.
    float water_linger_seconds = 0.0f;
    float water_restitution = 0.0f;
    float water_friction = 0.0f;  // per 1/60 s of contact, see physics/collision.h
    float settle_speed = 0.0f;    // normal speed below which a grounded ball rolls instead of bouncing
};

// One surface as the ball meets it (physics/turf_contact.h), dry, and the
// share of its stimp, contact angle (soft ground gives more), restitution,
// friction and plugging speed it has soaked (blended by the weather's wetness).
struct surface_tuning {
    turf dry;
    float wet_stimp_scale = 0.0f;
    float wet_contact_angle_scale = 0.0f;
    float wet_restitution_scale = 0.0f;
    float wet_friction_scale = 0.0f;
    float wet_plug_speed_scale = 0.0f;
};

// A surface under a cart's wheels (physics/cart_dynamics.h): its rolling
// resistance and the tyres' grip, dry, and the share of each it has soaked.
struct cart_surface_tuning {
    float rolling_resistance = 0.0f;
    float grip = 0.0f;
    float wet_rolling_resistance_scale = 0.0f;
    float wet_grip_scale = 0.0f;
};

// Under a cart: a cart road (a path), else the ground's material.
struct cart_surfaces_tuning {
    cart_surface_tuning path;
    cart_surface_tuning fairway;
    cart_surface_tuning rough;
    cart_surface_tuning green;
    cart_surface_tuning bunker;
    cart_surface_tuning water;
};

// Every surface but water (which the ball sinks into), from surfaces.json,
// and what each is to a cart.
// A green's stimp is its course's (course_definition), not the surface's.
struct surfaces_tuning {
    surface_tuning fairway;
    surface_tuning rough;
    surface_tuning green;
    surface_tuning bunker;
    cart_surfaces_tuning cart;
};

// The cups, one on every green (physics/cup.h): the real hole's size
// scaled by `leniency` (cup_leniency, the one named departure from the real
// cup), drawn as big as it plays; the flagstick's real thickness, and a
// ball resting in another green's cup dropped wrong_cup_drop_distance from
// its centre.
struct cup_tuning {
    float radius_meters = 0.0f;
    float depth_meters = 0.0f;
    float leniency = 0.0f;
    float rim_radius_meters = 0.0f;
    cup_contact_tuning contact;
    float flagstick_radius_meters = 0.0f;
    float wrong_cup_drop_distance_meters = 0.0f;
};

// A piecewise-linear curve through (x, y) points in rising x, held past
// either end.
struct tuning_curve {
    std::vector<glm::vec2> points;

    float at(const float x) const {
        if (points.empty()) {
            return 0.0f;
        }
        if (x <= points.front().x) {
            return points.front().y;
        }
        for (std::size_t i = 1; i < points.size(); ++i) {
            if (x <= points[i].x) {
                const glm::vec2 a = points[i - 1];
                const glm::vec2 b = points[i];
                return a.y + (b.y - a.y) * (x - a.x) / (b.x - a.x);
            }
        }
        return points.back().y;
    }
};

struct swing_tuning {
    // The swing's tempo (game/swing.h), whatever its length: back to the
    // top, then down to impact. A putter's stroke has its own.
    float backswing_seconds = 0.0f;
    float downswing_seconds = 0.0f;
    float putt_backswing_seconds = 0.0f;
    float putt_downswing_seconds = 0.0f;
    // The club's speed share by backswing (0 none .. 1 full .. past 1
    // overswing): a full swing's clock (tests/fixtures/reference/impact.json),
    // and a putter's pendulum.
    tuning_curve speed_share;
    tuning_curve putting_speed_share;
    // A ball one unit back in the stance (ball_position -1): degrees of loft
    // the shaft's lean takes off, and degrees steeper the club comes down.
    float ball_position_loft_degrees = 0.0f;
    float ball_position_attack_degrees = 0.0f;
    // A strike at least this near the face's middle sounds pure; below it,
    // the club's mishit sound.
    float pure_strike_centredness = 0.0f;
};

// Setting a shot up over the ball (game/shot_setup.h): one step of each
// dial, how a held key repeats them, and how far the stance moves.
struct setup_tuning {
    float aim_step_degrees = 0.0f;
    float face_step_degrees = 0.0f;
    float ball_position_step = 0.0f;
    int backswing_steps = 0;  // from none to full
    int putter_backswing_steps = 0;
    float overswing_max = 0.0f;  // the longest backswing, past full (not a putter's)
    // Metres the stance moves along the aim for one unit of ball position.
    float ball_position_meters = 0.0f;
    float repeat_delay_seconds = 0.0f;
    float repeat_rate = 0.0f;          // steps per second
    float repeat_acceleration = 0.0f;  // steps per second, per second
    float max_repeat_rate = 0.0f;
    // What the wheel changes: the part of the setup within this angle of
    // the view's centre (game/setup_pointer.h).
    float pointer_radius_degrees = 0.0f;
    // The right button held moving further than this (mouse counts) turns
    // the stance; less, it is a click (core/key_bindings.h).
    int drag_threshold_counts = 0;
    // The setup on the ground (game/address_view.h): the face's line past
    // the head each way, and the alignment stick this far out from the ball
    // along my toes, this long each way.
    float face_line_half_length_meters = 0.0f;
    float stick_out_meters = 0.0f;
    float stick_half_length_meters = 0.0f;
};

struct player_tuning {
    float walk_speed = 0.0f;
    float turn_rate = 0.0f;
    // Looking around, first person: the arrow keys look up and down at
    // look_pitch_rate (rad/s) as far as max_look_pitch (rad) either way; the
    // mouse turns mouse_look_radians per count at 100% mouse sensitivity.
    float look_pitch_rate = 0.0f;
    float max_look_pitch = 0.0f;
    float mouse_look_radians = 0.0f;
    float ball_interact_radius = 0.0f;
    // From a cup, standing on its green, P takes its flag out or puts it back.
    float flag_reach = 0.0f;
    // How far behind a ball the player stands teeing it up.
    float ball_stand_off_distance = 0.0f;
    // My bag: in reach on foot this far from it, set down this far to my
    // side (game/golf_bag.h).
    float bag_reach = 0.0f;
    float bag_set_down_distance = 0.0f;
    // A hop on foot (game/player_jump.h): up at jump_speed (world units/s),
    // pulled down at jump_gravity (world units/s^2).
    float jump_speed = 0.0f;
    float jump_gravity = 0.0f;
    // On foot I bump into what stands on the ground (game/body_contact.h)
    // as a circle this wide round my feet, and step up onto a walkable top
    // (a tee box) up to step_up over them, about a stair.
    float body_radius = 0.0f;
    float step_up = 0.0f;
};

// The golf cart (game/golf_cart.h), in world units and seconds: where it
// sits and parks, its body and how it sounds. How it drives is its item's
// spec (physics/cart_dynamics.h) on the surfaces' cart numbers.
struct cart_tuning {
    float steering_wheel_rate = 0.0f;  // how quickly the driver turns the wheel to where A or D hold it (1/s)
    // A named departure from real: a cart's whole physics runs this many
    // times faster than real time, so it takes a real cart's paths (its
    // climbs, slides and tips the same) at this many times its speed, and
    // beats walking as a cart does on a real course (game_top_speed).
    float time_scale = 0.0f;
    // Its two seats, each this far to the side of its middle: the driver's
    // on the left, the passenger's on the right.
    float seat_offset = 0.0f;
    float board_reach = 0.0f;        // from a seat, to step into it; from its middle, to right it
    float bag_behind_seat = 0.0f;    // a bag strapped behind a seat, this far back from it
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
    // Its body is a rectangle body_half_width either side of its seats'
    // middle, from body_back behind it to body_front ahead (golf_cart.h
    // cart_solid): it bumps into what stands on the ground, people on foot
    // and other carts, and others into it (game/body_contact.h). It drives
    // up onto a walkable top up to step_up over its wheels, about a kerb:
    // never a tee box. Crashing, it bounces back by the restitution of what
    // it hit (the solids' tuning), heard from crash_sound_speed (m/s into it).
    float body_half_width = 0.0f;
    float body_front = 0.0f;
    float body_back = 0.0f;
    float step_up = 0.0f;
    float crash_sound_speed = 0.0f;
    // Pressed against something, of its motion along it, the share it loses
    // (0 slides along freely, 1 sticks).
    float scrape_friction = 0.0f;
    // The motor's loop plays at motor_idle_pitch, rising to
    // motor_top_speed_pitch at top speed and by motor_load_pitch more at
    // the motor's full force.
    float motor_idle_pitch = 0.0f;
    float motor_top_speed_pitch = 0.0f;
    float motor_load_pitch = 0.0f;
};

// Camera rigs. "back" is against the aim direction, "side" is to the
// stance's side of the ball. The field of view is the player's setting
// (game/settings.h), not tuning.
struct camera_tuning {
    float walking_eye_height = 0.0f;
    float walking_look_distance = 0.0f;
    // At address, my eyes (game/address_view.h): this high over the
    // ground under the stance and this far out from the ball towards it.
    // The head turns from facing the ball (yaw 0, positive towards the
    // target) within the look's limits; the target and down views are two
    // framed looks in them, which V turns between in address_snap_seconds.
    float address_eye_height = 0.0f;
    float address_eye_out = 0.0f;
    float address_target_yaw_degrees = 0.0f;
    float address_target_pitch_degrees = 0.0f;
    float address_down_yaw_degrees = 0.0f;
    float address_down_pitch_degrees = 0.0f;
    float address_min_yaw_degrees = 0.0f;
    float address_max_yaw_degrees = 0.0f;
    float address_min_pitch_degrees = 0.0f;
    float address_max_pitch_degrees = 0.0f;
    float address_snap_seconds = 0.0f;
    // Following a shot: from beside where it was hit, on the stance's side,
    // looking at the ball.
    float follow_side_distance = 0.0f;
    float follow_back_distance = 0.0f;
    float follow_eye_height = 0.0f;
    float follow_look_height = 0.0f;
    float transition_seconds = 0.0f;        // blend time on rig changes and teleports
    float transition_jump_distance = 0.0f;  // eye movement per frame that counts as a teleport
    // What is held before my eyes (my emote props) is drawn at this field
    // of view, the one it was posed for, whatever the player's setting.
    float viewmodel_fov_degrees = 0.0f;
};

// What shows where a shot will go at address (game/address_play.h
// preview_shot): a full shot's flight, a point every step_seconds of it;
// for a putt only a line on the green this long from the ball along where
// it starts, no break (reading the green is the player's), its points this
// far apart so it lies on the slope.
struct shot_preview_tuning {
    float step_seconds = 0.0f;
    float putt_line_meters = 0.0f;
    float putt_line_step_meters = 0.0f;
};

// The trail drawn behind a ball in flight.
struct flight_path_tuning {
    glm::vec3 color{0.0f};
    float alpha = 0.0f;
    float min_point_spacing = 0.0f;
    int max_points = 0;
    float line_width = 0.0f;
};

// The rangefinder held up on foot (game/rangefinder.h).
struct rangefinder_tuning {
    float magnification = 0.0f;   // the zoom; the look turns this much slower
    float raise_seconds = 0.0f;   // from the eye to fully zoomed
    float max_range_meters = 0.0f;
    float march_step_meters = 0.0f;  // the view ray is marched in these, then bisected
    int refine_steps = 0;
    float flag_lock_degrees = 0.0f;  // the reticle this near the flagstick reads the flag
    // On locking onto the flag its ring flashes this many times over jolt_seconds.
    float jolt_seconds = 0.0f;
    int jolt_flashes = 0;
    // Round the eyepiece the view is unmagnified, darkened and greyed:
    // its brightness and saturation, 0 .. 1.
    float surround_brightness = 0.0f;
    float surround_saturation = 0.0f;
    // How tall people and carts stand to its laser, in world units.
    float person_height = 0.0f;
    float cart_height = 0.0f;
    // A new reading every ping_seconds. Light rain puts each off by up to
    // light_rain_error (a share) either way, light fog by light_fog_error;
    // rain and fog make it nonsense: blank blank_share of the time, else
    // any distance in range, as far up or down as nonsense_slope of it.
    float ping_seconds = 0.0f;
    float light_rain_error = 0.0f;
    float light_fog_error = 0.0f;
    float blank_share = 0.0f;
    float nonsense_slope = 0.0f;
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
    // How far from the server's clock a shot's time (shot_setup::shot_at)
    // may be, and still pick its weather.
    float shot_time_slack_seconds = 0.0f;
    // How much earlier than the server's clock the client may use an item
    // again, or act once its ball rests (it times them by frames).
    float timing_slack_seconds = 0.0f;
    // A cart said to have tipped or sunk where the server has it may be this
    // share short of what tips it (its stability) or sinks it (wade depth).
    float cart_condition_slack = 0.0f;
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

// Fences (course worlds): their poles stop shots like tree trunks, their nets
// catch them, giving back little of the speed.
struct fence_tuning {
    float pole_radius = 0.0f;
};

// The ball off a solid of one kind (physics/solid.h): of its speed into it,
// the share kept; of its speed along it, the share lost.
struct solid_contact_tuning {
    float restitution = 0.0f;
    float friction = 0.0f;
};

// Per kind of solid (game/area_solids.h), as the ball meets it; a cart
// crashing into one bounces back by its restitution too.
struct solids_tuning {
    solid_contact_tuning trunk;
    solid_contact_tuning leaves;
    solid_contact_tuning pole;
    solid_contact_tuning net;
    solid_contact_tuning tee_box;
    solid_contact_tuning board;
    solid_contact_tuning flagstick;
    solid_contact_tuning lamp;
    solid_contact_tuning cart;
    solid_contact_tuning person;
    solid_contact_tuning ball;  // another's at rest
    // The one who hit a shot is in its way only this long after the hit:
    // the club does not strike its own golfer, a ricochet can.
    float self_hit_grace_seconds = 0.0f;
    // A canopy slows a ball by this share of its speed per metre through it
    // (exponentially) and turns it at random by up to this many degrees per
    // metre (from the shot's dice).
    float leaves_drag_per_meter = 0.0f;
    float leaves_scatter_degrees_per_meter = 0.0f;
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
    // The lamp head's box: across, up and along where it points.
    float head_width = 0.0f;
    float head_height = 0.0f;
    float head_depth = 0.0f;
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

// The wind over a course (game/weather.h): its speed at the profile's
// reference height (10 m, where wind is measured) drifts on its own slow
// noise between calm_speed and max_speed, the storm adding up to
// storm_speed in heavy rain; its direction swings about the course's own
// prevailing one, by at most direction_swing sideways per unit along it
// (1 is 45 degrees). Speeds in m/s, times in game hours.
struct wind_tuning {
    wind_profile profile;
    float hours = 0.0f;  // between the speed noise's points
    float calm_speed = 0.0f;  // never less: there is always some wind
    float max_speed = 0.0f;
    float storm_speed = 0.0f;
    float direction_hours = 0.0f;
    float direction_swing = 0.0f;
};

// The weather over a course (game/weather.h), in game hours. The storm is
// the regime, a noise regime_hours apart weighing regime_weight, over
// passing storms (two octaves, `spell_hours` and `detail_hours` apart). It gathers
// clouds from cloud_start to full cover at cloud_full, running ahead of it
// by cloud_trend times how far it moved over rain_lag_hours. Rain follows the
// storm rain_lag_hours behind, from rain_start to its heaviest (1) at
// rain_full, thinned to sun_shower_keep of that under a clear sky; rain
// under less cover than sun_shower_cloud is a sun shower. Thunder grows
// from rain_lag_hours' storm at thunder_start to its most at thunder_full,
// under cloud. The air's damp, a noise fog_hours apart and the regime
// weighing fog_regime_weight, lays fog from fog_start to its thickest at
// fog_full, cleared by rain of fog_rain_clear and by wind from
// fog_calm_wind to fog_wind_clear m/s. light_rain, rain, heavy_rain,
// light_fog and fog are the god menu's presets, and an amount is called the
// one it is nearest. Wetness (0..1) rises in rain by wetting_per_hour at
// amount 1 and falls by drying_per_hour in full sun, less under cloud,
// summed over the last history_hours in steps of step_hours. The rest is
// what rain and wet ground do to a shot.
// The air's temperature through a day (game/weather.h temperature_at).
struct temperature_tuning {
    float coldest_hour = 0.0f;         // local time, 0..24
    float warmest_hour = 0.0f;
    float cloud_range_damping = 0.0f;  // share of the day's range an overcast sky takes away
    float rain_cooling_c = 0.0f;       // degrees the heaviest rain cools the air
    float noise_c = 0.0f;              // degrees the weather wanders either way
    float noise_hours = 0.0f;
};

struct weather_tuning {
    float regime_hours = 0.0f;
    float regime_weight = 0.0f;
    float spell_hours = 0.0f;
    float detail_hours = 0.0f;
    float detail_weight = 0.0f;
    float cloud_start = 0.0f;
    float cloud_full = 0.0f;
    float cloud_trend = 0.0f;
    float rain_start = 0.0f;
    float rain_full = 0.0f;
    float rain_lag_hours = 0.0f;
    float sun_shower_keep = 0.0f;
    float sun_shower_cloud = 0.0f;
    float light_rain = 0.0f;
    float rain = 0.0f;
    float heavy_rain = 0.0f;
    float thunder_start = 0.0f;
    float thunder_full = 0.0f;
    float fog_hours = 0.0f;
    float fog_regime_weight = 0.0f;
    float fog_start = 0.0f;
    float fog_full = 0.0f;
    float fog_rain_clear = 0.0f;
    float fog_calm_wind = 0.0f;
    float fog_wind_clear = 0.0f;
    float light_fog = 0.0f;
    float fog = 0.0f;
    float wetting_per_hour = 0.0f;
    float drying_per_hour = 0.0f;
    float history_hours = 0.0f;
    float step_hours = 0.0f;
    temperature_tuning temperature;
};

// What the ball lies on when it is hit: a tee box, else the material under it.
enum class ball_lie : std::uint8_t {
    tee,
    fairway,
    rough,
    green,
    bunker,
    water
};

// How what the ball lies on changes a strike (game/delivery.h).
struct lie_tuning {
    // Grass between the face and the ball: the share of the face's grip
    // rough leaves (a flier), and the share of what is left that a soaked
    // lie's water film takes.
    float rough_grip_share = 0.0f;
    float wet_grip_loss = 0.0f;
    // Long grass grabbing the hosel: the share of the club's speed it
    // takes, and degrees it closes the face per degree of the club's loft
    // at a full backswing.
    float rough_speed_loss = 0.0f;
    float rough_face_close_per_loft = 0.0f;
    // A ball picked clean from sand, and one exploded out of it.
    float sand_grip_share = 0.0f;
    sand_cushion sand;
    // Metres from the ball to the middle of the stance, and between the feet.
    float stance_ball_distance_meters = 0.0f;
    float stance_width_meters = 0.0f;
};

// How widely a golfer's swing strays and how setups are told apart
// (game/shot_ability.h), designed at every skill 99.
struct golfer_tuning {
    // One deviation of each channel at the top level: the club's speed as
    // a share, the face and the path in degrees, the strike in metres.
    float speed_share_spread = 0.0f;
    float face_spread_degrees = 0.0f;
    float path_spread_degrees = 0.0f;
    float strike_toe_spread_meters = 0.0f;
    float strike_high_spread_meters = 0.0f;
    // Every spread times this at level 1, easing to 1 at the top level.
    float spread_at_level_one = 0.0f;
    // The face and strike spreads times this at the longest overswing.
    float overswing_spread = 0.0f;
    // Playing the natural shape: the spreads times this; and how far the
    // stock face leans to it.
    float natural_shape_spread = 0.0f;
    float natural_face_bias_degrees = 0.0f;
    // A shape in full: the face this far open or closed to the path (a fade
    // or a draw), the ball this far back or forward of stock (a punch or
    // high), a wedge's face opened from start to full (a flop); and how
    // much of a shape names it.
    float curve_full_degrees = 0.0f;
    float ball_full = 0.0f;
    float flop_start_degrees = 0.0f;
    float flop_full_degrees = 0.0f;
    float word_threshold = 0.0f;
};

struct game_tuning {
    world_scale_tuning scale;
    terrain_build_tuning terrain;
    physics_tuning physics;
    wind_tuning wind;
    weather_tuning weather;
    ball_tuning ball;
    surfaces_tuning surfaces;
    cup_tuning cup;
    swing_tuning swing;
    setup_tuning setup;
    player_tuning player;
    cart_tuning cart;
    camera_tuning camera;
    rangefinder_tuning rangefinder;
    shot_preview_tuning shot_preview;
    flight_path_tuning flight_path;
    xp_drop_tuning xp_drops;
    net_tuning net;
    server_tuning server;
    hole_sign_tuning hole_sign;
    tee_box_tuning tee_box;
    fence_tuning fence;
    solids_tuning solids;
    floodlight_tuning floodlights;
    sky_timeline sky_clock;
    lie_tuning lies;
    golfer_tuning golfer;
};
