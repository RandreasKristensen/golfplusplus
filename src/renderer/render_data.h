#pragma once

// Everything the renderer draws in one frame, built by core/render_frame from
// game state. Plain data; the renderer never reads game state itself.

#include "game/scorecard.h"
#include "physics/tree_collision.h"
#include "profiling/profiling.h"
#include "renderer/menu_overlay.h"
#include "renderer/remote_avatar_batch.h"
#include "renderer/render_mesh.h"

#include <cstdint>
#include <string>
#include <vector>

#include <glm/vec3.hpp>

// Which keys are held, for the on-screen key icons.
struct controls_overlay_state {
    bool key_1_down = false;
    bool key_2_down = false;
    bool left_down = false;
    bool right_down = false;
    bool up_down = false;
    bool down_down = false;
    bool space_down = false;
    bool shift_down = false;
    bool enter_down = false;
    bool backspace_down = false;
    bool retee_down = false;
    bool show_group_key = false;  // online only
    bool group_down = false;
};

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

// A name over another player's head (world position of the tag).
struct render_name_tag {
    glm::vec3 position{0.0f};
    std::string name;
};

// Another player's shot trail, fading once their ball stops.
struct render_trail {
    std::vector<glm::vec3> points;
    float alpha = 0.0f;
};

struct render_data {
    glm::vec3 camera_position{0.0f};
    glm::vec3 camera_target{0.0f, 0.0f, 1.0f};
    float camera_fov_degrees = 0.0f;

    // Scene. Pointers borrow caches owned by app or game_state that outlive
    // the render call; null means "none this frame".
    const render_static_mesh* terrain_mesh = nullptr;
    const render_static_mesh* material_overlay_mesh = nullptr;
    // The course's backdrop panorama, relative to the asset root.
    const std::string* backdrop_image = nullptr;
    const std::vector<tree_body>* trees = nullptr;
    // Instance data for `trees` is re-uploaded only when this changes.
    std::uint64_t trees_revision = 0;
    glm::vec3 area_center{0.0f};
    float area_extent = 0.0f;

    bool show_ball = false;
    glm::vec3 ball_position{0.0f};
    glm::vec3 player_position{0.0f};
    float ball_visual_radius_meters = 0.0f;
    float cup_radius_meters = 0.0f;
    float pin_visual_height_meters = 0.0f;
    // The hole being played (tee marker, cup and flag); unset in the hub.
    bool show_hole = false;
    glm::vec3 tee_position{0.0f};
    glm::vec3 pin_position{0.0f};
    // Hub markers: starts of unplayed holes, every tee and pin, and the
    // collectibles that can be claimed now.
    std::vector<glm::vec3> start_markers;
    const std::vector<glm::vec3>* hub_tee_markers = nullptr;
    const std::vector<glm::vec3>* hub_pin_markers = nullptr;
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

    bool cart_active = false;
    bool cart_drifting = false;
    float cart_speed_fraction = 0.0f;  // of the cart's top speed
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
    bool show_interact_prompt = false;
    bool show_power_meter = false;
    int stroke_count = 0;
    std::string selected_club_label;
    bool show_rangefinder = false;
    glm::vec3 rangefinder_target{0.0f};
    std::string rangefinder_label;
    bool show_course_map = false;
    bool show_scorecard = false;
    bool show_course_results = false;
    scorecard_data scorecard;
    std::vector<group_scorecard_row> group_scorecard;  // beside the scorecard, when grouped online
    std::string notice_label;  // a refusal from the server, for a moment
    bool show_skills_panel = false;
    std::vector<render_skill_progress> skills;
    std::vector<render_xp_drop> xp_drops;
    controls_overlay_state controls;
    render_startup_menu startup_menu;

    // Developer overlay (Ctrl).
    bool show_fps = false;
    std::string fps_label;
    frame_profile profile_summary;
};
