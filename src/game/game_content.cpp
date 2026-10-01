#include "game/game_content.h"

#include "game/club_loader.h"
#include "game/course_loader.h"
#include "game/json_util.h"
#include "game/tuning_loader.h"

#include <filesystem>

game_content_load_result load_game_content(const std::string& asset_root) {
    game_content_load_result result;
    const std::filesystem::path root(asset_root);
    const auto fail = [&result, &root](const std::string& what) {
        result.error = what + " (asset root " + root.string() + ")";
        return result;
    };

    game_content content;
    content.asset_root = asset_root;

    const std::optional<std::string> tuning_text = read_text_file(root / game_tuning_path);
    if (!tuning_text) {
        return fail(std::string("cannot read ") + game_tuning_path);
    }
    game_tuning_parse_result tuning = parse_game_tuning_from_text(*tuning_text);
    if (!tuning.tuning) {
        return fail(tuning.error);
    }
    content.tuning = *tuning.tuning;

    const std::optional<std::string> skills_text = read_text_file(root / skills_path);
    std::optional<std::vector<skill_definition>> skills = skills_text ? parse_skills_from_text(*skills_text) : std::nullopt;
    if (!skills) {
        return fail(std::string("cannot load ") + skills_path);
    }
    content.skills = std::move(*skills);

    const std::optional<std::string> rewards_text = read_text_file(root / rewards_path);
    const std::optional<reward_rules> rewards = rewards_text ? parse_rewards_from_text(*rewards_text) : std::nullopt;
    if (!rewards) {
        return fail(std::string("cannot load ") + rewards_path);
    }
    content.rewards = *rewards;

    content.clubs = load_clubs_from_directory((root / "clubs").string());
    if (content.clubs.empty()) {
        return fail("no valid clubs in clubs/");
    }
    content.courses = load_courses_from_directory((root / "courses").string());
    if (content.courses.empty()) {
        return fail("no valid courses in courses/");
    }

    result.content = std::move(content);
    return result;
}
