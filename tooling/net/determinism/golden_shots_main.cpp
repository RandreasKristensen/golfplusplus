// Plays the golden shots (tests/fixtures/golden_shots/shots.json) the way the
// server does (server_content.h) and prints each result at full precision.
// check_determinism.ps1 builds this natively and to WASM and compares the two.
//
// Usage: golden_shots <repo root>

#include "game/content_files.h"
#include "game/course_loader.h"
#include "game/json_util.h"
#include "game/play_area.h"
#include "game/shot_simulation.h"
#include "physics/vector_math.h"
#include "server_content.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <map>
#include <optional>
#include <string>

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
    // rewards, the shipped font, and the fixture courses, as the golden test
    // uses them.
    std::map<std::string, std::string> texts;
    add_folder(texts, golden, "play");
    add_folder(texts, repo + "/assets", "interface/fonts");
    add_folder(texts, fixtures, courses_path);
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
        std::printf("%zu %.9g %.9g %.9g %d %.9g %zu\n", i, result.rest_position.x, result.rest_position.y,
                    result.rest_position.z, result.holed ? 1 : 0, result.duration, result.events.size());
    }
    return 0;
}
