#include "server_content.h"

#include "game/club_loader.h"
#include "game/course_loader.h"
#include "game/course_world_loader.h"
#include "game/hole_loader.h"
#include "game/text_assets.h"
#include "game/tuning_loader.h"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace {
std::optional<std::string> file_text(const content_files& files, const std::string& path) {
    const auto found = files.find(path);
    return found != files.end() ? std::optional<std::string>(std::string(found->second)) : std::nullopt;
}

// Every file directly in `directory` ("clubs"), in path order.
std::vector<std::string> files_in(const content_files& files, const std::string& directory) {
    std::vector<std::string> texts;
    const std::string prefix = directory + "/";
    for (const auto& [path, text] : files) {
        if (path.compare(0, prefix.size(), prefix) == 0 && path.find('/', prefix.size()) == std::string::npos) {
            texts.emplace_back(text);
        }
    }
    return texts;
}
}

server_content_load_result parse_server_content(const content_files& files) {
    server_content_load_result result;
    server_content content;

    const std::optional<std::string> tuning_text = file_text(files, game_tuning_path);
    if (!tuning_text) {
        result.error = std::string("missing ") + game_tuning_path;
        return result;
    }
    const game_tuning_parse_result tuning = parse_game_tuning_from_text(*tuning_text);
    if (!tuning.tuning) {
        result.error = tuning.error;
        return result;
    }
    content.tuning = *tuning.tuning;

    const std::optional<std::string> rewards_text = file_text(files, rewards_path);
    const std::optional<reward_rules> rewards = rewards_text ? parse_rewards_from_text(*rewards_text) : std::nullopt;
    if (!rewards) {
        result.error = std::string("cannot load ") + rewards_path;
        return result;
    }
    content.rewards = *rewards;

    const std::optional<std::string> font_text = file_text(files, text_font_path);
    const std::optional<pixel_font_data> font = font_text ? parse_pixel_font(*font_text) : std::nullopt;
    if (!font) {
        result.error = std::string("cannot load ") + text_font_path;
        return result;
    }
    content.font = *font;

    content.clubs = parse_clubs_from_texts(files_in(files, "clubs"));
    if (content.clubs.empty()) {
        result.error = "no valid clubs in clubs/";
        return result;
    }

    for (const std::string& text : files_in(files, "courses")) {
        std::optional<course_definition> course = parse_course_from_text(text);
        if (course && !course->world.empty()) {
            content.courses.push_back(std::move(*course));
        }
    }
    if (content.courses.empty()) {
        result.error = "no valid course with a world in courses/";
        return result;
    }

    result.content = std::move(content);
    return result;
}

const course_definition* find_server_course(const server_content& content, const std::string& course_id) {
    const auto found = std::find_if(content.courses.begin(), content.courses.end(),
                                    [&course_id](const course_definition& course) { return course.id == course_id; });
    return found != content.courses.end() ? &*found : nullptr;
}

std::optional<server_course> build_server_course(const content_files& files,
                                                 const server_content& content,
                                                 const std::string& course_id) {
    const course_definition* course = find_server_course(content, course_id);
    if (course == nullptr) {
        return std::nullopt;
    }

    std::vector<hole_data> holes;
    for (std::size_t i = 0; i < course->holes.size(); ++i) {
        const std::optional<std::string> text = file_text(files, course_hole_reference(*course, i));
        std::optional<hole_data> hole = text ? parse_hole_from_text(*text) : std::nullopt;
        if (!hole) {
            return std::nullopt;
        }
        holes.push_back(std::move(*hole));
    }
    const std::optional<std::string> world_text = file_text(files, course->world);
    std::optional<course_world_definition> world =
        world_text ? parse_course_world_from_text(*world_text, *course) : std::nullopt;
    if (!world || holes.empty()) {
        return std::nullopt;
    }

    server_course built;
    built.course = *course;
    built.area = build_course_area(holes, *world, content.tuning);
    built.trees = standing_trees(built.area);
    for (std::size_t i = 0; i < holes.size(); ++i) {
        hole_data placed = place_hole(holes[i], world->hole_starts[i]);
        built.shot_holes.push_back(shot_hole{anchor_on_terrain(built.area, placed.pin_position), placed.wind_seed});
        built.holes.push_back(std::move(placed));
    }
    built.world = std::move(*world);
    return built;
}
