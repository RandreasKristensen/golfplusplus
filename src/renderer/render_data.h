#pragma once

// Everything the renderer draws in one frame, built by core/render_frame from
// game state. Plain data; the renderer never reads game state itself.

#include "game/course_definition.h"
#include "game/lighting_loader.h"
#include "game/remote_players.h"
#include "game/scorecard.h"
#include "game/tee_box.h"
#include "physics/tree_collision.h"
#include "profiling/profiling.h"
#include "renderer/cart_batch.h"
#include "renderer/menu_overlay.h"
#include "renderer/remote_avatar_batch.h"
#include "renderer/render_mesh.h"

#include <cstdint>
#include <string>
#include <vector>

#include <glm/vec3.hpp>

struct render_fences;  // renderer/fence_batch.h
struct render_floodlights;  // renderer/floodlight_batch.h
struct render_water;   // renderer/water_batch.h
struct render_hole_signs;  // renderer/hole_sign_batch.h

struct render_skill_progress {
    std::string label;
    int level = 1;
    int xp = 0;
    int xp_to_next = 0;
};

enum class skill_icon_id {
    golf_swing,
    smoking,
    fitness,
    generic
};

// "golf_swing", "smoking", "fitness"; anything else is generic.
skill_icon_id skill_icon_from_name(const std::string& name);

struct render_xp_drop {
    skill_icon_id icon = skill_icon_id::generic;
    int xp = 0;
    float progress = 0.0f;  // 0 when it appears, 1 when it disappears
};

// A name over another player's head (world position of the tag), coloured
// by what they are to me.
struct render_name_tag {
    glm::vec3 position{0.0f};
    std::string name;
    player_relationship relationship = player_relationship::unknown;
};

// Another player's shot trail, fading once their ball stops.
struct render_trail {
    std::vector<glm::vec3> points;
    float alpha = 0.0f;
};

// A hole's number on the course map, along its line of play (tee, the
// hole's interior control points, pin).
struct render_map_hole {
    std::string number;
    std::vector<glm::vec3> line_of_play;
};

// How the frame's shadows fall (lighting.glsl's `shadow`): blob shadows
// (blob_shadow.vert) and the land's shadow at low sun (sun_shadow).
struct render_shadows {
    float sun_share = 0.0f;    // how much of their shadow the sun casts, 0..1 (sun_shadow_share)
    float night_share = 0.0f;  // a blob's strength with no sun
    float max_stretch = 1.0f;  // the longest a blob is cast, in its widths
    float horizon_softness = 0.0f;  // the land's shadow edge, as a sine of the sun's elevation
};

// A light after dark (lighting.glsl's local_light): its light at `position`,
// falling off to nothing at `radius` metres, in a cone along `direction`
// whose edge is at cos `cone_edge_cos` and that is full inside cos
// `cone_full_cos`. A point light has no direction and cosines -2 and -1.
struct render_light {
    glm::vec3 position{0.0f};
    glm::vec3 color{0.0f};  // times its intensity: past 1 it blooms
    float radius = 0.0f;
    glm::vec3 direction{0.0f};
    float cone_edge_cos = -2.0f;
    float cone_full_cos = -1.0f;
};

// A soft dark ellipse on the ground under something (blob_shadow.vert):
// cast along the sun onto the ground at `base`, which faces `normal`, from
// `lift` metres over it, `radius` wide and `strength` dark at its middle.
// Uploaded as nine packed floats.
struct render_blob_shadow {
    glm::vec3 base{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    float lift = 0.0f;
    float radius = 0.0f;
    float strength = 0.0f;
};

// The scene's light this frame, from the sky clock and the lighting palette
// (core/sky_frame.h). The renderer never sees the clock.
// The colours are the palette's (game/lighting_loader.h, lighting_key).
struct render_lighting {
    glm::vec3 sun_direction{0.0f, 1.0f, 0.0f};  // world space, towards the sun
    glm::vec3 sun_color{0.0f};
    glm::vec3 ambient_sky{0.0f};
    glm::vec3 ambient_ground{0.0f};
    glm::vec3 sky_zenith{0.0f};
    float sky_blend = 0.0f;
    glm::vec3 haze_tint{0.0f};
    glm::vec3 horizon_glow{0.0f};
    float horizon_glow_strength = 0.0f;
    glm::vec3 moon_direction{0.0f, -1.0f, 0.0f};  // world space, towards the moon
    float moon_phase = 0.0f;                      // how much of it is lit: 0 new .. 1 full
    float star_visibility = 0.0f;                 // 0..1 (star_visibility in game/lighting_loader.h)
    render_shadows shadows;
    float night_lights = 0.0f;  // how far the night lights are on, 0..1 (night_light_share)
    glm::vec3 lamp_glow{0.0f};  // a floodlight lamp's face: past 1 when on, none by day
    // The camcorder's (renderer/camcorder_pass.h).
    float exposure = 0.0f;
    glm::vec3 white_balance{0.0f};
    float gain_noise = 0.0f;
    float saturation = 0.0f;
};

// The camcorder's burn-in, two lines in the corner (core/vcr_frame.h).
struct render_datestamp {
    std::string time;
    std::string date;
};

// What the VCR's on-screen display says in its corner.
enum class vcr_tag { none, play, pause };

// The camera and the VCR this frame (core/vcr_frame.h).
struct render_vcr {
    vcr_tag tag = vcr_tag::none;
    // Paused: the scene stays the last frame recorded; the tape keeps playing over it.
    bool still_frame = false;
    bool show_datestamp = false;
    render_datestamp datestamp;
    // Offline play, whatever the datestamp setting: which progress the round is for.
    bool show_offline = false;
};

struct render_data {
    glm::vec3 camera_position{0.0f};
    glm::vec3 camera_target{0.0f, 0.0f, 1.0f};
    float camera_fov_degrees = 0.0f;
    // My emote props, pinned to the camera, are drawn at this field of view
    // in a pass of their own, so the player's setting never changes how they
    // sit in the frame.
    float viewmodel_fov_degrees = 0.0f;

    render_lighting lighting;
    // This frame's glow floors: the palette's, the ball's and ember's risen at night.
    glow_floors glow;
    // Every light this frame (core/light_frame.h); the renderer keeps the
    // nearest few (renderer/scene_lights.h).
    std::vector<render_light> lights;
    // Blobs under the ball, players and carts, cast on the ground this frame.
    std::vector<render_blob_shadow> blob_shadows;
    // The trees' blobs, re-uploaded only when the revision changes.
    const std::vector<render_blob_shadow>* tree_shadows = nullptr;
    std::uint64_t tree_shadows_revision = 0;

    // Scene. Pointers borrow caches owned by app or game_state that outlive
    // the render call; null means "none this frame".
    const render_static_mesh* terrain_mesh = nullptr;
    const render_static_mesh* material_overlay_mesh = nullptr;
    // What is drawn behind the course, and the haze the scene fades into.
    const course_backdrop* backdrop = nullptr;
    const std::vector<tree_body>* trees = nullptr;
    // Instance data for `trees` is re-uploaded only when this changes.
    std::uint64_t trees_revision = 0;
    const render_hole_signs* hole_signs = nullptr;  // one at every tee of the area
    const render_fences* fences = nullptr;          // the course world's fences
    const render_floodlights* floodlights = nullptr;  // along every hole of the area
    const render_water* water = nullptr;            // the ponds' surfaces
    bool camera_underwater = false;                 // the scene is seen from under a pond's surface
    glm::vec3 area_center{0.0f};
    float area_extent = 0.0f;

    bool show_ball = false;
    glm::vec3 ball_position{0.0f};  // the drawn ball's centre (drawn_ball_center)
    glm::vec3 player_position{0.0f};
    float ball_visual_radius_meters = 0.0f;
    float cup_radius_meters = 0.0f;
    float pin_visual_height_meters = 0.0f;
    // The hole being played (its cup and flag; its tee for the course map);
    // unset in the hub.
    bool show_hole = false;
    glm::vec3 tee_position{0.0f};
    glm::vec3 pin_position{0.0f};
    // Every tee box of the area, hub or hole.
    const std::vector<tee_box>* tee_boxes = nullptr;
    // The pins of every hole on the course but the one being played (whose
    // pin is `pin_position`): a course is one world, so they stay up in a round.
    std::vector<glm::vec3> pin_markers;
    // Hub only: the collectibles that can be claimed now.
    std::vector<glm::vec3> collectible_markers;

    bool show_aim_indicator = false;
    std::vector<glm::vec3> aim_arc_points;
    float aim_angle = 0.0f;
    bool show_swing_club = false;
    float swing_power = 0.0f;

    bool show_flight_path = false;
    const std::vector<glm::vec3>* flight_path_points = nullptr;
    glm::vec3 flight_path_color{0.0f};
    float flight_path_alpha = 0.0f;
    float flight_path_width = 0.0f;

    // Sat at my cart's wheel: the cart HUD shows.
    bool driving_cart = false;
    float cart_speed_fraction = 0.0f;  // of the cart's top speed
    // Every cart drawn whole in the world, mine too while I sit in it; the
    // driver's seat is cart_seat_offset to a cart's left.
    std::vector<render_cart> carts;
    float cart_seat_offset = 0.0f;
    bool smoke_emote_active = false;
    float smoke_emote_elapsed = 0.0f;
    bool beer_emote_active = false;
    float beer_emote_elapsed = 0.0f;

    // Other players in my room (online).
    std::vector<render_remote_avatar> remote_avatars;
    std::vector<render_remote_ball> remote_balls;
    std::vector<render_trail> remote_trails;
    std::vector<render_name_tag> name_tags;
    float avatar_eye_height = 0.0f;

    // HUD.
    // Which progress this round plays for: offline, or the online room.
    std::string mode_label;
    // Over the ball: the swing clock shows, its hand at swing_power; D
    // starts the backswing, then A hits.
    bool show_swing_clock = false;
    bool swing_running = false;
    int stroke_count = 0;
    std::string selected_club_label;  // short, e.g. "7I"
    std::string selected_club_name;
    bool show_rangefinder = false;
    glm::vec3 rangefinder_target{0.0f};
    std::string rangefinder_label;
    bool show_course_map = false;
    // The course map: the XZ box around every hole's ribbon and zones that it
    // fits to its paper, and each hole's number at its tee.
    glm::vec3 course_map_low{0.0f};
    glm::vec3 course_map_high{0.0f};
    std::vector<render_map_hole> map_holes;
    bool show_scorecard = false;
    bool show_course_results = false;
    scorecard_data scorecard;
    std::string notice_label;  // a refusal from the server, for a moment
    bool show_skills_panel = false;
    std::vector<render_skill_progress> skills;
    std::vector<render_xp_drop> xp_drops;
    render_startup_menu startup_menu;

    // The tape (renderer/tape_pass.h); looks only, never read by gameplay.
    float clock_seconds = 0.0f;  // since start, wrapped so it keeps its precision
    float tape_tracking = 0.0f;  // 0..1 tracking noise (core/tape_tracking.h)
    render_vcr vcr;

    // Developer overlay (Ctrl).
    bool show_fps = false;
    std::string fps_label;
    frame_profile profile_summary;
};
