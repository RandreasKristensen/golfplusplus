#pragma once

// The whole mutable game: owned by app, passed by reference, never global.
// Course flow (starting courses and holes, completing them) is in
// game/course_session.h; this file is the per-frame update.

#include "game/address_view.h"
#include "game/bag_place.h"
#include "game/club_definition.h"
#include "game/course_definition.h"
#include "game/course_world_definition.h"
#include "game/game_content.h"
#include "game/game_input.h"
#include "game/game_tuning.h"
#include "game/golf_cart.h"
#include "game/hole_data.h"
#include "game/item_catalog.h"
#include "game/loadout.h"
#include "game/motion_sync.h"
#include "game/net_types.h"
#include "game/play_area.h"
#include "game/rangefinder.h"
#include "game/reward_rules.h"
#include "game/round_state.h"
#include "game/save_data.h"
#include "game/shot_setup.h"
#include "game/shot_simulation.h"
#include "game/weather.h"
#include "physics/body_collision.h"
#include "physics/terrain.h"
#include "physics/tree_body.h"
#include "profiling/profiling.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <glm/vec3.hpp>

enum class game_mode {
    walking,
    addressing,  // standing over my ball: game_state::address
    following_shot
};

struct player_state {
    glm::vec3 position{0.0f};
    float yaw = 0.0f;  // where I face and look; see yaw_direction in physics/vector_math.h
    // How the player moved this frame, on foot or in a cart.
    float speed = 0.0f;      // m/s along yaw + move_angle, negative backwards
    float turn_rate = 0.0f;  // rad/s, positive turns towards +X like yaw
    // Off yaw while stepping sideways, in [-pi/2, pi/2]: positive left.
    float move_angle = 0.0f;
    // Up and down on foot: on the ground, or in the air hopping or falling
    // off an edge (game/player_jump.h); its y is the position's.
    vertical_body vertical;
    // On foot, pressed against something standing (game/body_contact.h).
    bool against_obstacle = false;
};

// Where I look beyond my yaw, first person. Presentation, but set from
// game_input like the yaw it adds to.
struct look_state {
    float pitch = 0.0f;     // radians, positive up
    float head_yaw = 0.0f;  // sat in a cart: turned from its heading, radians
};

// An item being used (game/item_catalog.h), shown from its first frame for
// its seconds_per_use, mine and other players' alike. Presentation: what a
// use earns and does is mode_dispatch's, by the shared clock.
struct item_use_shown {
    std::string item;
    float elapsed = 0.0f;
};

// Plays `use` on for `dt`; it ends after its item's seconds_per_use.
void tick_item_use(std::optional<item_use_shown>& use, const item_catalog& items, float dt);

// Each hole where it sits on the course (play area coordinates, authored heights).
struct course_hole {
    glm::vec3 tee_position{0.0f};
    glm::vec3 pin_position{0.0f};
    int par = 0;
};

// Where my ball is. A shot in flight is game_state::shot, and the ball's
// position follows it meanwhile. Its life: in my pocket (no ball) -> teed on
// hole N -> at rest <-> in flight -> in its cup -> picked up -> pocket.
enum class ball_place {
    teed,
    at_rest,
    in_cup
};

struct my_ball {
    std::size_t hole = 0;  // the hole it is played on
    glm::vec3 position{0.0f};
    int strokes = 0;
    ball_place place = ball_place::teed;
};

// The club picker open at my bag: the club it has highlighted (in bag order).
struct club_picker {
    std::size_t highlighted = 0;
};

// How long each dial key has been held at address, seconds; negative while
// it is up (shot_setup.h dial_steps).
struct dial_holds {
    float aim_left = -1.0f;
    float aim_right = -1.0f;
    float face_open = -1.0f;
    float face_close = -1.0f;
    float ball_back = -1.0f;
    float ball_forward = -1.0f;
    float longer = -1.0f;
    float shorter = -1.0f;
};

// Standing over my ball (game/address_play.h): the setup, where my head
// looks, a V turn under way, and the swing once Space commits the setup.
// It stays as it was through the shot, which the follow camera watches
// from where it was hit.
// What a setup would do, as the OSD shows it at address
// (game/address_play.h refresh_shot_preview): the flight to where it first
// lands and where it comes to rest, or for a putt only its start line.
struct shot_preview {
    shot_setup setup;  // what it previews
    std::vector<glm::vec3> line;
    std::optional<glm::vec3> landing;  // none for a putt
    std::optional<glm::vec3> rest;     // none for a putt
};

struct address_state {
    shot_setup setup;
    std::optional<shot_preview> preview;
    address_look look;
    std::optional<look_snap> snap;
    dial_holds holds;
    std::optional<float> swing_elapsed;  // seconds since Space
    // Online, the server's answer to my swing: the shot it played, launched
    // at impact. Until it comes the swing holds at its top.
    std::optional<room_shot> answer;
};

// A simulated shot shown over real time while following it.
struct shot_playback {
    shot_result result;
    float elapsed = 0.0f;
    std::size_t next_event = 0;  // first of result.events not played yet
    // Online: where the server says it rests, less where it played to here;
    // blended in over the playback.
    glm::vec3 correction{0.0f};
};

// Online: my ball moving to where the server says it rests, after its shot
// has played here.
struct ball_blend {
    glm::vec3 from{0.0f};
    glm::vec3 to{0.0f};
    float elapsed = 0.0f;
};

// Online: what the server has said about the hole my ball is played on.
struct online_hole_state {
    bool entered = false;  // it has my ball in play on this hole
};

// Another player in my room as shown here (presentation only): carried on
// along their last motion, a new motion blending in.
struct remote_avatar {
    glm::vec3 position{0.0f};
    float yaw = 0.0f;
    motion_mode mode = motion_mode::idle;
    std::int64_t motion_at = 0;      // the motion followed (server time)
    glm::vec3 correction{0.0f};      // shown minus followed when that motion arrived
    float correction_seconds = 0.0f;  // the blend's length: longer stepping into or out of a cart
    float correction_left = 0.0f;    // seconds of the blend still to run
    // What they smoke and drink, from the server's item use events.
    std::optional<item_use_shown> smoking;
    std::optional<item_use_shown> drinking;
    // The motion (server time) their last shot was hit from: its swing is over.
    std::int64_t hit_motion_at = 0;
    // Up and down as shown: hopping when a motion of theirs came from the
    // air, falling off what they walk off (game/player_jump.h).
    vertical_body vertical;
};

// Another player's shot playing back here, from the server's event: the
// ball leaves as the event arrives, as mine leaves when I hit it.
struct remote_shot {
    shot_setup input;
    shot_result result;
    glm::vec3 correction{0.0f};  // the server's rest less this simulation's
    float elapsed = 0.0f;
    std::size_t next_event = 0;  // first of result.events not heard yet
    std::vector<glm::vec3> trail;
};

// A message from the server shown for a moment (a refused action).
struct game_notice {
    std::string text_key;
    float age = 0.0f;
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
    ball_hit,  // anything but the ground: `hit` says what
    ball_cup,
    club_change,
    bag_moved,
    cart_start,
    cart_crash,
    cart_tip,
    cart_sink,
    cart_righted,
    player_bump,
    item_use
};

// Game code pushes these (game/world_sounds.h); app plays them
// (audio/sound_ids.h) from where they happen and clears them.
struct audio_event {
    audio_event_type type = audio_event_type::swing_start;
    glm::vec3 position{0.0f};
    terrain_material material = terrain_material::fairway;  // ball_land only
    shot_event_kind hit = shot_event_kind::tree_hit;         // ball_hit only
    std::string sound;  // club_hit and item_use: the club's or the item's
};

// Terrain-anchored positions of static objects, so a frame does not resample
// them. Rebuilt whenever terrain_render_revision changes; every code path that
// changes `area`, `world` or `holes` bumps it (see mark_terrain_render_dirty).
// Nothing here depends on which hole my ball is on.
struct static_anchor_cache {
    bool valid = false;
    std::uint64_t revision = 0;
    std::vector<glm::vec3> tees;  // one per hole
    std::vector<glm::vec3> pins;  // one per hole
    std::vector<tree_body> trees;
    std::vector<glm::vec3> collectibles;  // one per course world collectible, available or not
};

struct game_state {
    // Content, copied in when the state is made. Never changed by play.
    std::string asset_root;
    game_tuning tuning;
    std::vector<club_definition> clubs;
    reward_rules rewards;
    item_catalog items;
    loadout default_loadout;

    // The course and the ground under the player.
    course_definition course;
    std::vector<hole_data> course_holes;  // as authored, in hole coordinates
    round_state round;
    course_world_definition world;
    std::vector<course_hole> holes;  // empty until a course starts
    play_area area;                  // the whole course
    // Each green's flag, by hole: in its cup, or taken out and laid beside
    // it (P). Every flag is in when a course starts; never saved. Online the
    // room's, as its rows have them.
    std::vector<bool> flags_in;

    // Offline or online, fixed from the menu until the player returns to it.
    // Progress changes only go through game/mode_dispatch.h, which keeps the
    // two apart: offline touches `save`, online pushes `net_commands`.
    play_mode play = play_mode::offline;

    // The offline save. `save_requested` asks app to write it (hole and course
    // completion); app clears it.
    save_data save;
    bool save_requested = false;

    // Online only. The network client sends and clears `net_commands`, and
    // fills `online` from the server.
    std::vector<net_command> net_commands;
    online_view online;
    motion_sync_state motion_sync;
    online_hole_state online_hole;
    std::optional<ball_blend> ball_correction;
    std::map<std::uint64_t, remote_avatar> remote_avatars;
    std::map<std::uint64_t, remote_shot> remote_shots;
    // Others' strokes per hole this round as seen here (game/group_round.h).
    std::map<std::uint64_t, std::vector<int>> group_strokes;
    std::optional<game_notice> notice;

    // nullopt: in my pocket. Not saved: leaving the course puts it away.
    std::optional<my_ball> ball;
    std::optional<shot_playback> shot;  // while following_shot
    // F at an unplayed tee with my ball in play on another hole: that tee,
    // until F gives the hole up there or Esc keeps playing it.
    std::optional<std::size_t> give_up_prompt;
    player_state player;
    cart_state cart;
    std::optional<cart_ride> ride;  // sat in a cart, or stepping into or out of one
    std::optional<cart_righting> righting;  // standing a tipped cart back up
    look_state look;
    // What I smoke and drink while I use it; offline, the timed effects of
    // what I used (online the server's: online_view::effects).
    std::optional<item_use_shown> smoking;
    std::optional<item_use_shown> drinking;
    std::vector<active_effect> effects;
    game_mode mode = game_mode::walking;
    std::size_t selected_club = 0;  // the club in hand
    bag_place bag;  // not saved: it stays where it is from hole to hole
    std::optional<club_picker> picker;
    address_state address;
    // Which side of the ball I stand (a setting, game/settings.h), which
    // app sets every frame.
    handedness hand = handedness::right;
    // Offline, shot number n of the round rolls round_shot_seed(round_seed,
    // n) (game/shot_dice.h); a course starting picks the round's seed.
    std::uint64_t round_seed = 0;
    int shots_hit = 0;
    // Online: my bag and club have been taken from my player_bag row since
    // the course started (online_play.h); after that mine lead.
    bool server_bag_adopted = false;
    // Online: the hand and natural shape last sent to my player row
    // (set_golfer), so a change is sent once.
    std::optional<std::pair<handedness, std::uint8_t>> golfer_sent;

    // This machine's UTC clock in milliseconds, which app sets every frame.
    std::int64_t local_unix_ms = 0;
    // The weather over the course now (course_weather_at in
    // game/mode_dispatch.h), refreshed every update. Offline the god menu
    // (core/god_menu.h) may hold one in its place.
    weather_state weather;
    std::optional<weather_state> weather_override;

    // Hold-to-view overlays, refreshed every update.
    bool rangefinder_active = false;
    float rangefinder_zoom = 0.0f;  // 0 at the eye .. 1 fully zoomed (game/rangefinder.h)
    rangefinder_reading rangefinder;
    float rangefinder_locked_seconds = 0.0f;  // on the flag this long
    std::uint32_t rangefinder_ping = 0;       // its readings so far, each its own roll of the weather's
    float rangefinder_ping_left = 0.0f;       // until its next
    bool course_map_active = false;
    bool scorecard_active = false;
    bool skills_panel_active = false;

    // Meters travelled that have not earned movement XP yet.
    float walk_meters_pending = 0.0f;
    float cart_meters_pending = 0.0f;

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

// A sound for app to play this frame.

void update_game(game_state& state, const game_input& input, float dt, frame_profile* profile = nullptr);

void update_xp_drops(game_state& state, float dt);

// Where I look, walking or in a cart: my yaw, turned by my head in a cart.
float view_yaw(const game_state& state);

// A course has been started (start_course).
bool course_started(const game_state& state);
// My ball is on a hole and not yet in its cup.
bool ball_in_play(const game_state& state);
bool shot_playing(const game_state& state);
// My ball's hole's course for simulate_shot. Borrows from `state`; needs a ball.
shot_course current_shot_course(const game_state& state);

// The cup whose flag P reaches from where I stand: on its green, within
// player.flag_reach. Online the room's flags (green_flag rows) are the server's.
std::optional<std::size_t> flag_in_reach(const game_state& state);
// P: takes the flag in reach out of its cup, or puts it back.
void toggle_flag_in_reach(game_state& state);
// Follows `result` from the ball's position until it has played.
void play_shot(game_state& state, shot_result result);
// Walking on foot with something interact would act on in reach
// (game/interactables.h).
bool interact_available(const game_state& state);
bool cart_on_road(const game_state& state);

// Static anchors: see static_anchor_cache.
static_anchor_cache build_static_anchor_cache(const game_state& state, frame_profile* profile = nullptr);
bool static_anchor_cache_is_current(const game_state& state);
void refresh_static_anchor_cache(game_state& state, frame_profile* profile = nullptr);
void mark_terrain_render_dirty(game_state& state);
// A replacement state restarts terrain_render_revision, but render caches
// outside game_state key on it. Call this so the revision keeps increasing.
void continue_terrain_render_revision(game_state& state, std::uint64_t previous_revision);
// Hole `hole`'s tee and pin on the ground.
glm::vec3 tee_anchor_position(const game_state& state, std::size_t hole);
glm::vec3 pin_anchor_position(const game_state& state, std::size_t hole);
