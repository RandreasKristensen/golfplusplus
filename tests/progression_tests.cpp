#include "doctest.h"

#include "game/progress_rules.h"
#include "game/progression.h"
#include "game/round_state.h"
#include "game/save_data.h"
#include "game/save_manager.h"

#include "test_support.h"

#include <filesystem>
#include <fstream>

TEST_CASE("the xp curve rises every level and is front loaded") {
    CHECK(xp_for_level(1) == 0);
    CHECK(xp_for_level(skill_max_level) == skill_max_xp);
    for (int level = 2; level <= skill_max_level; ++level) {
        CHECK(xp_for_level(level) > xp_for_level(level - 1));
    }
    const float level_80_share = static_cast<float>(xp_for_level(80)) / static_cast<float>(skill_max_xp);
    CHECK(level_80_share > 0.18f);
    CHECK(level_80_share < 0.22f);
    CHECK(skill_level(0) == 1);
    CHECK(skill_level(skill_max_xp) == skill_max_level);
}

TEST_CASE("adding xp reports what was applied and stops at the cap") {
    skill_progression skills;
    const add_skill_xp_result nothing = add_skill_xp(skills, "golf_swing", 0);
    CHECK(nothing.applied_xp == 0);
    CHECK(skills.empty());

    const add_skill_xp_result new_skill = add_skill_xp(skills, "future_skill", 12);
    CHECK(new_skill.before_xp == 0);
    CHECK(new_skill.after_xp == 12);
    CHECK(new_skill.applied_xp == 12);

    skills["smoking"].xp = skill_max_xp - 3;
    const add_skill_xp_result capped = add_skill_xp(skills, "smoking", 10);
    CHECK(capped.after_xp == skill_max_xp);
    CHECK(capped.applied_xp == 3);
    CHECK(add_skill_xp(skills, "smoking", 10).applied_xp == 0);
}

TEST_CASE("movement xp carries leftover meters") {
    const movement_xp_rate rate{"fitness", 10.0f};
    const movement_xp_update first = award_movement_xp(save_data{}, rate, 0.0f, 25.0f);
    CHECK(skill_xp(first.update.progress.skills, "fitness") == 2);
    CHECK(near(first.remainder_meters, 5.0f));
    REQUIRE(first.update.awarded.size() == 1U);
    CHECK(first.update.awarded[0].xp == 2);

    const movement_xp_update second = award_movement_xp(first.update.progress, rate, first.remainder_meters, 4.0f);
    CHECK(skill_xp(second.update.progress.skills, "fitness") == 2);
    CHECK(near(second.remainder_meters, 9.0f));
    CHECK(second.update.awarded.empty());
}

TEST_CASE("a one-off collectible is claimed once and sets its flag") {
    course_world_collectible lost_ball;
    lost_ball.id = "lost_ball";
    lost_ball.world_flag = "found_lost_ball";
    lost_ball.skill_rewards = {{"fitness", 12}};

    save_data save;
    CHECK(collectible_available(save, lost_ball));
    const claim_update claim = claim_collectible(save, lost_ball);
    CHECK(claim.claimed);
    CHECK(skill_xp(claim.update.progress.skills, "fitness") == 12);
    CHECK(claim.update.progress.collected_ids == std::vector<std::string>{"lost_ball"});
    CHECK(claim.update.progress.world_flags == std::vector<std::string>{"found_lost_ball"});
    CHECK(!collectible_available(claim.update.progress, lost_ball));
    CHECK(!claim_collectible(claim.update.progress, lost_ball).claimed);
}

TEST_CASE("a repeatable collectible cools down over completed holes, across rounds") {
    course_world_collectible token;
    token.id = "token";
    token.repeatable = true;
    token.repeatable_cooldown_holes = 2;
    token.skill_rewards = {{"fitness", 5}};

    save_data save;
    save.holes_completed = 7;
    save = claim_collectible(save, token).update.progress;
    CHECK(save.repeatable_collectibles["token"].claim_count == 1);
    CHECK(!collectible_available(save, token));

    save = apply_hole_completed(save);
    CHECK(!collectible_available(save, token));
    save = apply_hole_completed(save);
    CHECK(collectible_available(save, token));
}

TEST_CASE("collectible requirements check skill level, flags and courses") {
    course_world_collectible cache;
    cache.id = "cache";
    cache.requirement.skill_id = "fitness";
    cache.requirement.min_level = 2;
    cache.requirement.required_world_flag = "found_map";
    cache.requirement.required_completed_course_id = "course_01";

    save_data save;
    add_skill_xp(save.skills, "fitness", xp_for_level(2));
    save.world_flags = {"found_map"};
    CHECK(!collectible_available(save, cache));
    save = apply_course_completed(save, "course_01");
    CHECK(collectible_available(save, cache));
    CHECK(apply_course_completed(save, "course_01").completed_course_ids.size() == 1U);
}

TEST_CASE("save data round trips") {
    save_data save;
    save.completed_course_ids = {"course_01"};
    save.holes_completed = 11;
    save.skills["golf_swing"].xp = 125;
    save.skills["future_skill"].xp = 42;
    save.collected_ids = {"lost_ball"};
    save.repeatable_collectibles["token"] = repeatable_collectible_state{2, 9};
    save.repeatable_collectibles["never"] = repeatable_collectible_state{0, std::nullopt};
    save.world_flags = {"found_cache"};

    const std::optional<save_data> parsed = parse_save_data(save_data_to_json(save));
    REQUIRE(parsed.has_value());
    CHECK(parsed->version == current_save_version);
    CHECK(parsed->completed_course_ids == save.completed_course_ids);
    CHECK(parsed->holes_completed == 11);
    CHECK(skill_xp(parsed->skills, "golf_swing") == 125);
    CHECK(skill_xp(parsed->skills, "future_skill") == 42);
    CHECK(parsed->collected_ids == save.collected_ids);
    CHECK(parsed->repeatable_collectibles.at("token").claim_count == 2);
    CHECK(parsed->repeatable_collectibles.at("token").claimed_at_holes_completed == std::optional<int>(9));
    CHECK(!parsed->repeatable_collectibles.at("never").claimed_at_holes_completed);
    CHECK(parsed->world_flags == save.world_flags);
}

TEST_CASE("old saves migrate and newer saves are refused") {
    const std::optional<save_data> v5 = parse_save_data(R"({
      "version": 5, "money": 5, "current_hole_index": 3, "hole_scores": {"0": 4},
      "skills": {"smoking": -12, "future_skill": 17},
      "repeatable_collectibles": {"token": {"claim_count": 2, "last_claimed_hole_index": 5}}
    })");
    REQUIRE(v5.has_value());
    CHECK(v5->version == current_save_version);
    CHECK(v5->holes_completed == 0);
    CHECK(skill_xp(v5->skills, "smoking") == 0);
    CHECK(skill_xp(v5->skills, "future_skill") == 17);
    CHECK(v5->repeatable_collectibles.at("token").claim_count == 2);
    CHECK(!v5->repeatable_collectibles.at("token").claimed_at_holes_completed);

    CHECK(parse_save_data("{}").has_value());
    CHECK(!parse_save_data(R"({"version": 99})").has_value());
    CHECK(!parse_save_data("not json").has_value());
}

TEST_CASE("the save file is written and read back") {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "golfpp_test_save";
    std::filesystem::remove_all(root);
    const std::filesystem::path path = save_file_path(root);

    const save_load_result fresh = load_save(path);
    CHECK(!fresh.loaded_existing);
    CHECK(!fresh.existing_was_unreadable);

    save_data save;
    save.skills["golf_swing"].xp = 15;
    REQUIRE(write_save(path, save));
    save.skills["golf_swing"].xp = 20;
    REQUIRE(write_save(path, save));

    const save_load_result loaded = load_save(path);
    CHECK(loaded.loaded_existing);
    CHECK(skill_xp(loaded.save.skills, "golf_swing") == 20);
    CHECK(!std::filesystem::exists(path.string() + ".tmp"));
    CHECK(!std::filesystem::exists(path.string() + ".bak"));

    std::filesystem::remove_all(root);
}

TEST_CASE("an unreadable save is backed up, never silently lost") {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "golfpp_test_bad_save";
    std::filesystem::remove_all(root);
    const std::filesystem::path path = save_file_path(root);
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path) << "{ corrupt";

    const save_load_result result = load_save(path);
    CHECK(!result.loaded_existing);
    CHECK(result.existing_was_unreadable);
    REQUIRE(!result.unreadable_backup.empty());
    CHECK(std::filesystem::exists(result.unreadable_backup));

    std::filesystem::remove_all(root);
}

TEST_CASE("a round finishes once every hole has a score") {
    round_state round = start_round(3);
    CHECK(!round_finished(round));

    round = complete_hole(round, 1, 4);
    CHECK(hole_played(round, 1));
    CHECK(round.current_hole_index == 2U);
    CHECK(!round_finished(round));

    round = complete_hole(round, 2, 5);
    CHECK(round.current_hole_index == 0U);
    round = complete_hole(round, 0, 3);
    CHECK(round_finished(round));
    CHECK(!round_finished(start_round(0)));
}
