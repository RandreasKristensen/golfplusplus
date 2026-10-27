#pragma once

// One golf shot, simulated from launch to rest in fixed steps. Pure, like
// physics/: the same input on the same course gives the same result, so a
// server can replay a client's shot. game_state shows a result over real time
// (play_shot), whatever the frame rate.

#include "game/club_definition.h"
#include "game/course_definition.h"
#include "game/delivery.h"
#include "game/golfer.h"
#include "game/game_tuning.h"
#include "game/play_area.h"
#include "game/reward_rules.h"
#include "game/shot_dice.h"
#include "game/shot_setup.h"
#include "game/weather.h"
#include "physics/ball_state.h"
#include "physics/flight_model.h"
#include "physics/impact.h"
#include "physics/launch.h"
#include "physics/solid.h"
#include "physics/terrain.h"
#include "physics/turf_contact.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <glm/vec3.hpp>

// The fixed step, the longest a shot may run, and how often the trajectory
// keeps a point. Part of the simulation itself, not feel: changing any of
// them changes where every shot lands.
inline constexpr float shot_step_seconds = 1.0f / 120.0f;
inline constexpr int shot_max_steps = 120 * 60;
inline constexpr int shot_steps_per_trajectory_point = 2;

// The hole a shot is played on, in play-area coordinates.
struct shot_hole {
    glm::vec3 pin{0.0f};    // the cup centre, on the terrain
    std::size_t index = 0;  // its cup in play_area::cups
};

// What a shot plays on, borrowed for one simulation.
struct shot_course {
    const play_area& area;  // the ball meets its ground, its solids and its cups
    shot_hole hole;
    weather_state weather;  // the course's at the shot's shot_at, for the whole flight
    // Metres above sea level (course_definition's air): with the weather's
    // temperature, how thin the air is.
    float elevation = 0.0f;
    float green_stimp_feet = 0.0f;  // the course's greens
    // What the ball meets besides the area's solids, as of the hit: every
    // green's flagstick by its flag (area_solids.h flagstick_solids), and
    // the people, carts and balls at rest of the shot's snapshot
    // (shot_snapshot.h). The one who hit it, when there, is the last, met
    // only from solids.self_hit_grace_seconds after the hit.
    std::vector<solid> dynamic;
    std::optional<std::size_t> shooter;
    // The shot's dice: what turns it at random through a canopy.
    std::uint64_t seed = 0;
};

// The course a shot towards hole `hole` plays on: `area`, `course`'s air and
// greens, `weather`, and each green's flag in or out by `flags_in`.
shot_course make_shot_course(const play_area& area, std::size_t hole, const weather_state& weather,
                             const course_definition& course, const std::vector<bool>& flags_in,
                             const game_tuning& tuning);

// The dice of a shot set up as `setup` says: the same for everyone who
// replays it. Online, until the server rolls each shot's seed.
std::uint64_t shot_seed(const shot_setup& setup);

// How `material` takes a ball at `wetness` (0..1), its green at `green_stimp_feet`.
turf turf_for(terrain_material material, float wetness, float green_stimp_feet, const game_tuning& tuning);

// The air `course` flies through (flight_model.h air_at).
air_state shot_air(const shot_course& course, const game_tuning& tuning);

enum class shot_event_kind {
    land,
    tree_hit,    // a trunk
    pole_hit,    // a pole, a fence's net or a lamp head
    board_hit,   // a sign's board, a tee box's side or a cart
    flag_hit,    // a flagstick, standing or lying
    cup_rattle,  // the rim or the inside of a cup
    leaves,      // through a canopy
    person_hit,  // someone standing in the way
    ball_hit     // another's ball at rest
};

// Something to hear during playback.
struct shot_event {
    float time = 0.0f;  // seconds after launch
    shot_event_kind kind = shot_event_kind::land;
    terrain_material material = terrain_material::fairway;  // land only
};

struct shot_result {
    // In the cup when holed; where it was hit from when lost in water.
    glm::vec3 rest_position{0.0f};
    bool holed = false;
    float duration = 0.0f;  // seconds from launch to rest
    // The ball centre every shot_steps_per_trajectory_point steps from
    // launch; the last point is where the simulation stopped, at `duration`.
    std::vector<glm::vec3> trajectory;
    std::vector<shot_event> events;  // in time order
    // How near the face's middle it was struck, 1 dead centre (which sound).
    float strike_centredness = 1.0f;
    // Under water: it sinks to the pond's floor and lies there
    // ball.water_linger_seconds before the shot ends, and the next is played
    // from where this one was, a stroke later.
    bool lost = false;
    // Added to the stroke: 1 for a lost ball, 2 when a shot from a putting
    // green hits another's ball at rest on one (Rule 11.1a).
    int penalty_strokes = 0;
};

// One fixed step of a moving ball, and what happened during it.
struct shot_step {
    ball_state ball;
    terrain_sample ground;  // under the ball after the step
    bool landed = false;
    std::optional<shot_event_kind> hit;  // what it struck, but the ground
    std::optional<std::size_t> hit_dynamic;  // which of the course's dynamic solids that was
    bool through_leaves = false;
    bool plugged = false;  // buried in sand: it stays there
    bool resting = false;  // still, and held where it is
    // The cup the ball is in, by play_area::cups, or touching.
    std::optional<std::size_t> cup;
};

// The club with this id, or null.
const club_definition* find_club(const std::vector<club_definition>& clubs, const std::string& id);

// What a ball at `position` lies on: a tee box, else the material under it.
ball_lie lie_at(const play_area& area, const glm::vec3& position);

// The slope a stance stands on to strike a ball at `ball` along `aim`:
// the ground under both feet (tuning.lies' stance, on the ball's left for
// a right-hander, its right for a left-hander) against the ground under
// the ball.
stance_slope stance_at(const play_area& area, const glm::vec3& ball, float aim, handedness hand,
                       const game_tuning& tuning);

// A ball at `ball` struck along `aim` by a `hand` golfer on a course
// `wetness` wet.
ball_on_lie ball_on_lie_at(const play_area& area, const glm::vec3& ball, float aim, handedness hand, float wetness,
                           const game_tuning& tuning);

// `club` swung as `setup` says, held within the club's limits
// (setup_limits_for), straying by `errors`, struck from `on`
// (game/delivery.h, physics/impact.h).
strike_outcome club_strike(const club_definition& club, const shot_setup& setup, const ball_on_lie& on,
                           const game_tuning& tuning, const swing_errors& errors = {});
ball_state launch_ball(const club_definition& club, const shot_setup& setup, const ball_on_lie& on,
                       const game_tuning& tuning);

// Flight in the course's wind at the ball's height over `ground`, the
// ground (physics/turf_contact.h, its surfaces at the weather's wetness)
// but over a cup, the cups (physics/cup.h), the area's solids and the
// course's dynamic ones, and the canopies it passes through, over `dt`, as
// step number `step` of the shot (its dice for this step). `ground` is the
// terrain under `ball` (the previous step's `ground`).
shot_step step_shot(const ball_state& ball,
                    const terrain_sample& ground,
                    const shot_course& course,
                    const game_tuning& tuning,
                    float dt,
                    int step = 0);

// Steps `launched` until it rests, plugs or hits shot_max_steps. Holed when
// it rests in the cup of the course's hole; resting in another green's cup
// it is dropped beside it (cup.wrong_cup_drop_distance_meters), on the side
// it came in from.
shot_result simulate_ball(const ball_state& launched,
                          const shot_course& course,
                          const game_tuning& tuning);

// The whole shot, its setup held within the club's limits, the swing
// straying by `errors`: none is the shot as set up, the preview's. An
// unknown club leaves the ball where it is. A shot from a putting green
// that hits a ball at rest on one costs two strokes (penalty_strokes).
shot_result simulate_shot(const shot_setup& setup,
                          const shot_course& course,
                          const game_tuning& tuning,
                          const std::vector<club_definition>& clubs,
                          const swing_errors& errors = {});

// The shot as `golfer` hits it: `record`'s setup, its swing straying by
// the golfer's ability (shot_ability.h) times the record's dice, which
// also turn it through canopies, meeting the record's snapshot too.
shot_result simulate_struck_shot(const shot_record& record,
                                 const golfer_profile& golfer,
                                 const shot_course& course,
                                 const game_tuning& tuning,
                                 const std::vector<club_definition>& clubs);

// Where the ball is `time` seconds after launch, between trajectory points.
glm::vec3 shot_position_at(const shot_result& result, float time);
