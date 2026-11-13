// Plays the golden shots (tests/fixtures/golden_shots/shots.json) the way the
// server does (server_content.h), drives the golden cart through fixed
// manoeuvres, and walks and drives from every tee of the fixture course in
// the shared fixed steps (game/movement_step.h), and prints each result at
// full precision. check_determinism.ps1 builds this natively and to WASM and
// compares the two.
//
// Usage: golden_shots <repo root>

#include "game/content_files.h"
#include "game/course_loader.h"
#include "game/json_util.h"
#include "game/movement_step.h"
#include "game/play_area.h"
#include "game/shot_simulation.h"
#include "physics/cart_dynamics.h"
#include "physics/cart_rollover.h"
#include "physics/vector_math.h"
#include "server_content.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <map>
#include <optional>
#include <string>

#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

namespace {
// Every JSON file under `root`/`folder`, keyed by its path relative to `root`.
void add_folder(std::map<std::string, std::string>& texts, const std::string& root, const std::string& folder) {
    for (const std::filesystem::path& path : json_files_under(std::filesystem::path(root) / folder)) {
        if (const std::optional<std::string> text = read_text_file(path)) {
            texts[path.lexically_relative(root).generic_string()] = *text;
        }
    }
}
}

namespace {
// A golden shot's snapshot: each entry `ahead_meters` along the aim from
// the ball, on the ground (a ball resting there).
std::vector<snapshot_entry> golden_snapshot(const json& shot, const play_area& area, const glm::vec3& ball, const float aim,
                                            const float ball_radius) {
    std::vector<snapshot_entry> entries;
    if (const json* listed = json_array(shot, "snapshot")) {
        for (const json& entry : *listed) {
            const std::string kind = json_string(entry, "kind").value_or("person");
            const glm::vec3 at = ball + yaw_direction(aim) * json_float(entry, "ahead_meters").value_or(0.0f);
            const snapshot_kind of = kind == "cart" ? snapshot_kind::cart : kind == "ball" ? snapshot_kind::ball : snapshot_kind::person;
            const glm::vec3 placed = of == snapshot_kind::ball ? resting_ball_position(area, at, ball_radius)
                                                               : anchor_on_terrain(area, at);
            entries.push_back(snapshot_entry{of, placed, aim, json_bool(entry, "shooter").value_or(false)});
        }
    }
    return entries;
}

// A cart manoeuvre: one surface, sloped `grade` ahead and `side_rise` to the
// left (rise over run), the pedal held and the steering swung from `steer`
// to its opposite every `swing_seconds`.
struct golden_drive {
    const cart_surface_tuning* surface;
    float grade;
    float side_rise;
    float water_depth;
    cart_controls controls;
    float swing_seconds;
    float seconds;
};

std::vector<golden_drive> golden_drives(const cart_surfaces_tuning& surfaces) {
    return {
        {&surfaces.path, 0.0f, 0.0f, 0.0f, {1.0f, 0.0f}, 0.0f, 8.0f},
        {&surfaces.fairway, 0.12f, 0.0f, 0.0f, {1.0f, 0.35f}, 1.5f, 10.0f},
        {&surfaces.rough, -0.18f, 0.05f, 0.0f, {0.6f, -0.6f}, 0.0f, 6.0f},
        {&surfaces.path, 0.0f, 0.0f, 0.0f, {-1.0f, 1.0f}, 2.0f, 6.0f},
        {&surfaces.fairway, 0.0f, 0.3f, 0.0f, {1.0f, 1.0f}, 0.0f, 8.0f},
        {&surfaces.green, 0.0f, 0.0f, 0.5f, {1.0f, 0.0f}, 0.0f, 1.0f},
    };
}

std::vector<int> golden_flags_out(const json& shot) {
    std::vector<int> out;
    if (const json* listed = json_array(shot, "flags_out")) {
        for (const json& hole : *listed) {
            out.push_back(hole.get<int>());
        }
    }
    return out;
}
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: golden_shots <repo root>\n");
        return 2;
    }
    const std::string repo = argv[1];
    const std::string golden = repo + "/tests/fixtures/golden_shots";
    const std::string fixtures = repo + "/tests/fixtures";

    // The golden shots' frozen tuning, items (the clubs among them) and
    // rewards, the shipped font, and the fixture courses and hub, as the golden test
    // uses them.
    std::map<std::string, std::string> texts;
    add_folder(texts, golden, "play");
    add_folder(texts, golden, "world/props");
    add_folder(texts, repo + "/assets", "interface/fonts");
    add_folder(texts, fixtures, courses_path);
    add_folder(texts, fixtures, "world/hub");
    content_files files;
    for (const auto& [path, text] : texts) {
        files.emplace(path, text);
    }

    const server_content_load_result loaded = parse_server_content(files);
    if (!loaded.content) {
        std::fprintf(stderr, "content: %s\n", loaded.error.c_str());
        return 1;
    }
    const server_content& content = *loaded.content;
    const std::optional<server_course> course = build_server_course(files, content, "fixture_hub");
    const std::optional<std::string> shots_text = read_text_file(golden + "/shots.json");
    const std::optional<json> shots_root = shots_text ? parse_json(*shots_text) : std::nullopt;
    const json* shots = shots_root ? json_array(*shots_root, "shots") : nullptr;
    if (!course || shots == nullptr) {
        std::fprintf(stderr, "cannot load the fixture hub course or shots.json\n");
        return 1;
    }

    const float radius = content.tuning.scale.ball_physics_radius_meters;
    for (std::size_t i = 0; i < shots->size(); ++i) {
        const json& shot = (*shots)[i];
        const std::size_t hole = static_cast<std::size_t>(json_int(shot, "hole").value_or(0));
        const shot_hole target{course->area.cups[hole].centre, hole};
        shot_setup input;
        input.ball_start = resting_ball_position(course->area, course->holes[hole].tee_position, radius);
        input.aim_angle = yaw_towards(input.ball_start, target.pin);
        if (const std::optional<float> putt = json_float(shot, "putt_from_pin_meters")) {
            input.ball_start = resting_ball_position(course->area, target.pin - yaw_direction(input.aim_angle) * *putt, radius);
            input.aim_angle = yaw_towards(input.ball_start, target.pin);
        }
        input.aim_angle += glm::radians(json_float(shot, "aim_offset_degrees").value_or(0.0f));
        // Online the aim arrives as the client's float; here it comes from
        // atan2, which may round differently in the WASM, so both builds
        // take it on the same coarse step.
        constexpr float aim_steps_per_radian = 4096.0f;
        input.aim_angle = std::round(input.aim_angle * aim_steps_per_radian) / aim_steps_per_radian;
        const club_definition* club = find_club(content.clubs, json_string(shot, "club").value_or(""));
        if (club == nullptr) {
            std::fprintf(stderr, "golden shot %zu: unknown club\n", i);
            return 1;
        }
        const float aim = input.aim_angle;
        input = stock_setup(*club, lie_at(course->area, input.ball_start), input.ball_start, aim, handedness::right, 0.0f,
                            content.tuning.setup);
        input.aim_angle = aim;
        input.backswing = json_float(shot, "backswing").value_or(0.0f);
        input.shot_at = shot.value("shot_at", std::int64_t{0});

        const shot_record record{input, shot.value("seed", std::uint64_t{0}),
                                 golden_snapshot(shot, course->area, input.ball_start, aim, radius)};
        const shot_result result =
            simulate_struck_shot(record, golfer_profile{}, server_shot_course(*course, hole, input.shot_at, golden_flags_out(shot), content),
                                 content.tuning, content.clubs);
        std::printf("shot %zu %.9g %.9g %.9g %d %.9g %zu\n", i, result.rest_position.x, result.rest_position.y,
                    result.rest_position.z, result.holed ? 1 : 0, result.duration, result.events.size());
    }

    const cart_spec& spec = *content.items.find(content.default_loadout.cart)->cart;
    const std::vector<golden_drive> drives = golden_drives(content.tuning.surfaces.cart);
    for (std::size_t i = 0; i < drives.size(); ++i) {
        const golden_drive& drive = drives[i];
        cart_ground ground;
        ground.normal = glm::normalize(glm::vec3(-drive.side_rise, 1.0f, -drive.grade));
        ground.rolling_resistance = drive.surface->rolling_resistance;
        ground.grip = drive.surface->grip;
        ground.water_depth = drive.water_depth;
        const int steps = static_cast<int>(std::lround(drive.seconds / cart_step_seconds));
        const int swing = static_cast<int>(std::lround(drive.swing_seconds / cart_step_seconds));
        cart_motion motion;
        motion.yaw = 0.3f;
        glm::vec2 position(0.0f);
        cart_controls controls = drive.controls;
        for (int step = 0; step < steps; ++step) {
            if (swing > 0 && step > 0 && step % swing == 0) {
                controls.steer = -controls.steer;
            }
            motion = step_cart(motion, controls, ground, spec, cart_step_seconds);
            position += motion.velocity * cart_step_seconds;
        }
        cart_fall fall;
        if (cart_tipped(motion.condition)) {
            fall = start_cart_fall(spec);
            while (!fall.resting) {
                fall = step_cart_fall(fall, spec, content.tuning.cart.fall_restitution, cart_fall_step_seconds);
            }
        }
        std::printf("cart %zu %.9g %.9g %.9g %d %.9g %.9g\n", i, position.x, motion.yaw, position.y,
                    static_cast<int>(motion.condition), cart_forward_speed(motion), fall.angle);
    }

    // On every hole of the fixture course, from its tee: a walk (turning,
    // stepping sideways, running and hopping) and a drive (weaving), each
    // through the shared fixed steps over the course's ground and solids.
    const std::vector<bool> flags_in(course->area.cups.size(), true);
    const std::vector<solid> walker_solids = walker_obstacles({}, course->area.cups, flags_in, content.tuning);
    const std::vector<solid> cart_solids = cart_obstacles_of({}, {}, course->area.cups, flags_in, content.tuning);
    const walk_world on_foot{course->area, walker_solids, content.tuning, content.tuning.player.walk_speed, nullptr};
    const cart_world at_wheel{course->area, content.tuning, spec, 0.4f, 1.0f, cart_solids, nullptr};
    constexpr int steps = 720;
    for (std::size_t hole = 0; hole < course->holes.size(); ++hole) {
        const glm::vec3 tee = anchor_on_terrain(course->area, course->holes[hole].tee_position);
        walker me{tee, vertical_body{tee.y, 0.0f, true}};
        float yaw = yaw_towards(tee, course->area.cups[hole].centre);
        for (int step = 0; step < steps; ++step) {
            move_input input;
            input.forward = static_cast<std::int8_t>(step % 240 < 200 ? 1 : -1);
            input.side = static_cast<std::int8_t>((step / 90) % 3 - 1);
            input.run = (step / 150) % 2 == 1;
            input.jump = step % 180 == 60;
            input.paced = step >= 240 && step < 480;
            input.crouched = step >= 360 && step < 600;
            yaw += 0.4f * move_step_seconds;
            input.yaw = quantise_yaw(yaw);
            me = step_walker(me, input, on_foot).after;
        }
        std::printf("walk %zu %.9g %.9g %.9g %d %.9g %.9g\n", hole, me.position.x, me.position.y, me.position.z,
                    me.vertical.grounded ? 1 : 0, me.speed, me.vertical.speed);

        cart_mover cart;
        cart.pose = cart_pose{tee, yaw_towards(tee, course->area.cups[hole].centre)};
        cart.motion.yaw = cart.pose.yaw;
        cart.vertical = vertical_body{tee.y, 0.0f, true};
        for (int step = 0; step < steps; ++step) {
            const float steer = step % 300 < 150 ? 0.6f : -0.6f;
            cart = step_cart_mover(cart, cart_controls{1.0f, steer_of(quantise_steer(steer))}, at_wheel).after;
        }
        std::printf("drive %zu %.9g %.9g %.9g %d %.9g %.9g\n", hole, cart.pose.position.x, cart.pose.position.y,
                    cart.pose.position.z, static_cast<int>(cart.motion.condition), cart.pose.yaw, cart_forward_speed(cart.motion));
    }
    return 0;
}
