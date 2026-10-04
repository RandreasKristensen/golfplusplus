// Plays the golden shots (tests/fixtures/golden_shots/shots.json) the way the
// server does (server_content.h) and prints each result at full precision.
// check_determinism.ps1 builds this natively and to WASM and compares the two.
//
// Usage: golden_shots <repo root>

#include "game/content_files.h"
#include "game/json_util.h"
#include "game/play_area.h"
#include "game/shot_simulation.h"
#include "physics/vector_math.h"
#include "server_content.h"

#include <cstdio>
#include <filesystem>
#include <map>
#include <optional>
#include <string>

#include <glm/trigonometric.hpp>

namespace {
void add_directory(std::map<std::string, std::string>& texts, const std::string& root, const std::string& directory) {
    for (const std::filesystem::path& path : json_files_in_directory(root + "/" + directory)) {
        if (const std::optional<std::string> text = read_text_file(path)) {
            texts[directory + "/" + path.filename().string()] = *text;
        }
    }
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

    // The golden shots' frozen tuning, clubs and rewards, the shipped font,
    // and the fixture courses, as the golden test uses them.
    std::map<std::string, std::string> texts;
    add_directory(texts, golden, "tuning");
    add_directory(texts, golden, "clubs");
    add_directory(texts, golden, "progression");
    add_directory(texts, repo + "/assets", "fonts");
    for (const char* directory : {"courses", "holes", "course_worlds"}) {
        add_directory(texts, fixtures, directory);
    }
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
        const shot_hole& target = course->shot_holes[hole];
        shot_input input;
        input.ball_start = resting_ball_position(course->area, course->holes[hole].tee_position, radius);
        input.aim_angle = yaw_towards(input.ball_start, target.pin);
        if (const std::optional<float> putt = json_float(shot, "putt_from_pin_meters")) {
            input.ball_start = resting_ball_position(course->area, target.pin - yaw_direction(input.aim_angle) * *putt, radius);
            input.aim_angle = yaw_towards(input.ball_start, target.pin);
        }
        input.aim_angle += glm::radians(json_float(shot, "aim_offset_degrees").value_or(0.0f));
        input.club_id = json_string(shot, "club").value_or("");
        input.power = json_float(shot, "power").value_or(0.0f);
        input.wind_time = json_float(shot, "wind_time").value_or(0.0f);
        input.cigarette_active = json_bool(shot, "cigarette").value_or(false);

        const shot_result result =
            simulate_shot(input, shot_course{course->area, course->trees, target}, content.tuning, content.clubs, content.rewards);
        std::printf("%zu %.9g %.9g %.9g %d %.9g %zu\n", i, result.rest_position.x, result.rest_position.y,
                    result.rest_position.z, result.holed ? 1 : 0, result.duration, result.events.size());
    }
    return 0;
}
