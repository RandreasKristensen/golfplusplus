// The shot lab: hits every club of the bag (or those named) at a few swings
// on flat fairway, or from a course's tee, in still air, through the game's
// own simulate_shot, and prints what each shot did. For fitting club specs
// to the reference (tests/fixtures/reference/) without running the game
// (tooling/shot_lab/README.md). --fit finds each club's speed_at_99 that
// carries its row of a bag table at full swing on the flat lab hole.
//
//   golfpp-shot-lab [--club <id>]... [--backswing <0..1.1>]... [--course <id> --hole <n>] [--csv] [--fit <bag table>]

#include "game/content_files.h"
#include "game/course_session.h"
#include "game/game_content.h"
#include "game/game_state.h"
#include "game/json_util.h"
#include "game/play_area.h"
#include "game/shot_simulation.h"
#include "physics/vector_math.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

namespace {
// The flat lab hole: a fairway this long and wide, the ball teed at its start.
constexpr float lab_length = 600.0f;
constexpr float lab_width = 80.0f;
constexpr float lab_start = 20.0f;
// Its greens (on a course, the course's).
constexpr float lab_stimp_feet = 10.0f;

struct lab_options {
    std::vector<std::string> clubs;
    std::vector<float> backswings;
    std::optional<std::string> course;
    std::size_t hole = 0;
    bool csv = false;
    std::optional<std::string> fit;  // a bag table (tests/fixtures/reference/bag_targets.json)
};

// What one shot did, in metres and degrees.
struct lab_shot {
    float carry = 0.0f;   // to its first landing, along the aim
    float total = 0.0f;   // to where it rests, along the aim
    float apex = 0.0f;    // over where it was hit from
    float land_angle = 0.0f;
    float curve = 0.0f;   // where it rests off the aim line, positive left
    float flight_seconds = 0.0f;
};

std::optional<lab_options> parse(const int argc, char** argv) {
    lab_options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const bool has_value = i + 1 < argc;
        if (arg == "--club" && has_value) {
            options.clubs.push_back(argv[++i]);
        } else if (arg == "--backswing" && has_value) {
            options.backswings.push_back(std::strtof(argv[++i], nullptr));
        } else if (arg == "--course" && has_value) {
            options.course = argv[++i];
        } else if (arg == "--hole" && has_value) {
            options.hole = static_cast<std::size_t>(std::max(1L, std::strtol(argv[++i], nullptr, 10)) - 1);
        } else if (arg == "--fit" && has_value) {
            options.fit = argv[++i];
        } else if (arg == "--csv") {
            options.csv = true;
        } else {
            return std::nullopt;
        }
    }
    if (options.backswings.empty()) {
        options.backswings = {1.0f, 0.75f, 0.5f};
    }
    return options;
}

hole_data lab_hole() {
    hole_data hole;
    hole.id = "lab";
    hole.name = "Lab";
    hole.par = 5;
    hole.tee_position = glm::vec3(0.0f);
    hole.pin_position = glm::vec3(0.0f, 0.0f, lab_length);
    hole.spline.control_points = {hole.tee_position, hole.pin_position};
    hole.spline.width = lab_width;
    hole.spline.rough_width = lab_width;
    return hole;
}

lab_shot measure(const shot_result& result, const glm::vec3& start, const float aim) {
    lab_shot shot;
    const glm::vec3 ahead = yaw_direction(aim);
    const glm::vec3 left = yaw_left(ahead);
    const auto along = [&](const glm::vec3& point) { return glm::dot(horizontal(point - start), ahead); };
    for (const glm::vec3& point : result.trajectory) {
        shot.apex = std::max(shot.apex, point.y - start.y);
    }
    const auto landing = std::find_if(result.events.begin(), result.events.end(),
                                      [](const shot_event& event) { return event.kind == shot_event_kind::land; });
    if (landing != result.events.end()) {
        const glm::vec3 at = shot_position_at(result, landing->time);
        const glm::vec3 before = shot_position_at(result, std::max(0.0f, landing->time - 2.0f * shot_step_seconds * shot_steps_per_trajectory_point));
        const glm::vec3 falling = at - before;
        shot.carry = along(at);
        shot.flight_seconds = landing->time;
        shot.land_angle = glm::degrees(std::atan2(-falling.y, glm::length(horizontal(falling))));
    }
    shot.total = along(result.rest_position);
    shot.curve = glm::dot(horizontal(result.rest_position - start), left);
    return shot;
}

// The speed_at_99 at which `club`, swung full, carries `carry` metres from
// `start`: bisected, as carry rises with club speed.
float fitted_speed(club_definition club, const float carry, const shot_course& course, const game_tuning& tuning,
                   const glm::vec3& start, const float aim) {
    float low = 1.0f;
    float high = 80.0f;
    for (int i = 0; i < 40; ++i) {
        club.spec.speed_at_99 = 0.5f * (low + high);
        const shot_setup input =
            stock_setup(club, lie_at(course.area, start), start, aim, handedness::right, 0.0f, tuning.setup);
        const float flew = measure(simulate_shot(input, course, tuning, {club}), start, aim).carry;
        (flew < carry ? low : high) = club.spec.speed_at_99;
    }
    return 0.5f * (low + high);
}

int fit(const lab_options& options, const std::vector<club_definition>& clubs, const shot_course& course,
        const game_tuning& tuning, const glm::vec3& start, const float aim) {
    const std::optional<std::string> text = read_text_file(*options.fit);
    const std::optional<json> root = text ? parse_json(*text) : std::nullopt;
    const json* rows = root ? json_array(*root, "clubs") : nullptr;
    if (rows == nullptr) {
        std::fprintf(stderr, "cannot read the bag table %s\n", options.fit->c_str());
        return 2;
    }
    std::printf("club            carry  speed_at_99  fitted\n");
    for (const club_definition& club : clubs) {
        if (!options.clubs.empty() && std::find(options.clubs.begin(), options.clubs.end(), club.id) == options.clubs.end()) {
            continue;
        }
        for (const json& row : *rows) {
            if (json_string(row, "label") != club.label) {
                continue;
            }
            const float carry = json_float(row, "carry").value_or(0.0f);
            std::printf("%-14s %6.1f %12.2f %7.2f\n", club.id.c_str(), carry, club.spec.speed_at_99,
                        fitted_speed(club, carry, course, tuning, start, aim));
        }
    }
    return 0;
}
}

int main(int argc, char** argv) {
    const std::optional<lab_options> options = parse(argc, argv);
    if (!options) {
        std::fprintf(stderr, "usage: golfpp-shot-lab [--club <id>]... [--backswing <0..1.1>]... [--course <id> --hole <n>] [--csv] "
                             "[--fit <bag table>]\n");
        return 2;
    }
    game_content_load_result loaded = load_game_content(GOLFPP_ASSETS_DIR);
    if (!loaded.content) {
        std::fprintf(stderr, "cannot load content: %s\n", loaded.error.c_str());
        return 2;
    }
    const game_content content = std::move(*loaded.content);
    game_state state = make_game_state(content, save_data{});
    // Off the hole's end, where the fairway is.
    glm::vec3 start(0.0f, 0.0f, lab_start);
    glm::vec3 pin(0.0f, 0.0f, lab_length);
    if (options->course) {
        const auto course = std::find_if(content.courses.begin(), content.courses.end(),
                                         [&](const course_definition& each) { return each.id == *options->course; });
        if (course == content.courses.end() || !start_course(state, *course) || options->hole >= state.holes.size()) {
            std::fprintf(stderr, "no such course or hole\n");
            return 2;
        }
        start = tee_anchor_position(state, options->hole);
        pin = pin_anchor_position(state, options->hole);
    } else {
        // The hole's own start, so the land is its fairway rather than rough.
        course_world_definition world;
        world.hole_starts = {course_world_hole_start{0, glm::vec3(0.0f), 0.0f, 0.0f}};
        state.area = build_course_area({lab_hole()}, world, state.tuning);
        start = anchor_on_terrain(state.area, start);
    }
    // Still, dry air: the weather's own default.
    course_definition lab_course = state.course;
    lab_course.green_stimp_feet = lab_stimp_feet;
    shot_course course = make_shot_course(state.area, 0, weather_state{}, lab_course, {}, state.tuning);
    course.hole.pin = pin;
    const float aim = yaw_towards(start, pin);
    if (options->fit) {
        return fit(*options, content.clubs, course, state.tuning, start, aim);
    }

    std::printf(options->csv ? "club,backswing,carry,total,apex,land_angle,curve,flight_seconds\n"
                             : "club         backswing carry   total    apex   land   curve  flight\n");
    for (const club_definition& club : content.clubs) {
        if (!options->clubs.empty() && std::find(options->clubs.begin(), options->clubs.end(), club.id) == options->clubs.end()) {
            continue;
        }
        for (const float backswing : options->backswings) {
            shot_setup input =
                stock_setup(club, lie_at(course.area, start), start, aim, handedness::right, 0.0f, state.tuning.setup);
            input.backswing = backswing;
            const lab_shot shot = measure(simulate_shot(input, course, state.tuning, state.clubs), start, aim);
            std::printf(options->csv ? "%s,%.2f,%.1f,%.1f,%.1f,%.1f,%.1f,%.2f\n"
                                     : "%-14s %6.2f %7.1f %7.1f %7.1f %6.1f %7.1f %7.2f\n",
                        club.id.c_str(), backswing, shot.carry, shot.total, shot.apex, shot.land_angle, shot.curve,
                        shot.flight_seconds);
        }
    }
    return 0;
}
