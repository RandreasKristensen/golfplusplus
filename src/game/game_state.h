#pragma once

// The whole mutable game: owned by app, passed by reference, never global.
// Course flow (starting courses and holes, completing them) is in
// game/course_session.h; this file is the per-frame update.

#include "game/club_definition.h"
#include "game/course_definition.h"
#include "game/course_world_definition.h"
#include "game/game_content.h"
#include "game/game_input.h"
#include "game/game_tuning.h"
#include "game/hole_data.h"
#include "game/play_area.h"
#include "game/reward_rules.h"
#include "game/round_state.h"
#include "game/save_data.h"
#include "game/swing.h"
#include "physics/ball_state.h"
#include "physics/terrain.h"
#include "physics/tree_collision.h"
#include "profiling/profiling.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <glm/vec3.hpp>

enum class game_mode {
    walking,
    aiming,
    addressing,
    following_shot
};

struct player_state {
    glm::vec3 position{0.0f};
    float yaw = 0.0f;  // see yaw_direction in physics/vector_math.h
};

struct cart_state {
    bool active = false;
    float velocity = 0.0f;
    float yaw = 0.0f;
    float drift_timer = 0.0f;
};

// A short emote animation (presentation only).
struct emote_state {
    float elapsed = 0.0f;
    bool active = false;
};

// The hole being played, in the play area's coordinates.
struct active_hole {
    std::size_t index = 0;
    glm::vec3 tee_position{0.0f};
    glm::vec3 pin_position{0.0f};
    std::uint32_t wind_seed = 0;
};

// Where each hole sits in the hub (course coordinates, authored heights).
struct hub_hole_marker {
    glm::vec3 tee_position{0.0f};
    glm::vec3 pin_position{0.0f};
    glm::vec3 start_position{0.0f};
};

// A course with a course world. Its whole course is the play area, both
// while walking around and while playing a hole.
struct course_hub {
    course_world_definition world;
    std::vector<hub_hole_marker> markers;
};

// A "+XP" popup. Age runs to tuning.xp_drops.lifetime_seconds.
struct xp_drop {
    std::string skill_id;
    int xp = 0;
    float age = 0.0f;
};

enum class audio_event_type {
    swing_start,
    club_hit,
    ball_land,
    ball_tree_hit,
    ball_cup,
    club_change,
    cart_start,
    cart_drift,
    emote_smoke,
    emote_beer
};

// Game code pushes these; app plays them (audio/sound_ids.h) and clears them.
struct audio_event {
    audio_event_type type = audio_event_type::swing_start;
    terrain_material material = terrain_material::fairway;  // ball_land only
    std::string club_hit_sound;                             // club_hit only
};

// Terrain-anchored positions of static objects, so a frame does not resample
// them. Rebuilt whenever terrain_render_revision changes; every code path that
// changes `area`, `hole` or `hub` bumps it (see mark_terrain_render_dirty).
struct static_anchor_cache {
    bool valid = false;
    std::uint64_t revision = 0;
    glm::vec3 tee_anchor{0.0f};  // only meaningful while a hole is played
    glm::vec3 pin_anchor{0.0f};
    std::vector<tree_body> trees;
    std::vector<glm::vec3> hub_tee_markers;
    std::vector<glm::vec3> hub_pin_markers;
    std::vector<glm::vec3> hub_start_markers;
    std::vector<glm::vec3> collectibles;  // one per hub collectible, available or not
};

struct game_state {
    // Content, copied in when the state is made. Never changed by play.
    std::string asset_root;
    game_tuning tuning;
    std::vector<club_definition> clubs;
    reward_rules rewards;

    // The course and the ground under the player.
    course_definition course;
    std::vector<hole_data> course_holes;  // as authored, in hole coordinates
    round_state round;
    std::optional<course_hub> hub;
    std::optional<active_hole> hole;  // nullopt while walking around a hub
    // The whole course on a hub course; the current hole on a course without one.
    play_area area;

    // The offline save. `save_requested` asks app to write it (hole and course
    // completion); app clears it.
    save_data save;
    bool save_requested = false;

    ball_state ball;
    player_state player;
    cart_state cart;
    emote_state smoke_emote;
    emote_state beer_emote;
    float cigarette_seconds_left = 0.0f;
    game_mode mode = game_mode::walking;
    float aim_angle = 0.0f;
    std::size_t selected_club = 0;
    swing_state swing;
    int stroke_count = 0;
    float hole_time = 0.0f;  // drives the wind

    // Hold-to-view overlays, refreshed every update.
    bool rangefinder_active = false;
    float rangefinder_distance_meters = 0.0f;
    bool course_map_active = false;
    bool scorecard_active = false;
    bool skills_panel_active = false;

    // Meters travelled that have not earned movement XP yet.
    float walk_meters_pending = 0.0f;
    float cart_meters_pending = 0.0f;
    float drift_meters_pending = 0.0f;

    std::vector<xp_drop> xp_drops;
    // XP gains too small to show yet, per skill (see xp_drop_tuning).
    std::map<std::string, int> pending_xp_drop_amounts;
    std::vector<glm::vec3> flight_path_points;
    // Where the ball sat when the current shot was hit (the follow camera
    // stays at the address view of this spot).
    glm::vec3 shot_start_position{0.0f};
    std::vector<audio_event> audio_events;

    // Render caches outside game_state key on this; it only ever increases.
    std::uint64_t terrain_render_revision = 0;
    static_anchor_cache static_anchors;
};

// A state with `content` copied in and no course yet; start one with
// start_course (game/course_session.h).
game_state make_game_state(const game_content& content, const save_data& save);

void update_game(game_state& state, const game_input& input, float dt, frame_profile* profile = nullptr);

// Adds XP to the save and shows it as an XP drop.
void award_skill_xp(game_state& state, const xp_reward& reward);
void update_xp_drops(game_state& state, float dt);

bool in_hub(const game_state& state);
bool ball_is_moving(const game_state& state);
bool ball_is_in_cup(const game_state& state);
bool can_interact_with_ball(const game_state& state);
// Index into hub->world.hole_starts of the nearest start of an unplayed hole
// in reach, or nullopt.
std::optional<std::size_t> nearby_hole_start(const game_state& state);
// Index into hub->world.collectibles of the nearest available one in reach.
std::optional<std::size_t> nearby_collectible(const game_state& state);
bool cart_on_road(const game_state& state);
// Whole meters shown on the rangefinder.
int rounded_rangefinder_meters(float distance_meters);

// Static anchors: see static_anchor_cache.
static_anchor_cache build_static_anchor_cache(const game_state& state, frame_profile* profile = nullptr);
bool static_anchor_cache_is_current(const game_state& state);
void refresh_static_anchor_cache(game_state& state, frame_profile* profile = nullptr);
void mark_terrain_render_dirty(game_state& state);
// A replacement state restarts terrain_render_revision, but render caches
// outside game_state key on it. Call this so the revision keeps increasing.
void continue_terrain_render_revision(game_state& state, std::uint64_t previous_revision);
glm::vec3 pin_anchor_position(const game_state& state);
