#include "game/game_content.h"

#include "game/club_loader.h"
#include "game/course_loader.h"
#include "game/shop.h"
#include "quest/quest_loader.h"

#include <filesystem>

game_content load_game_content(const std::string& asset_root) {
    game_content content;
    content.asset_root = asset_root;
    const std::filesystem::path root(asset_root);
    content.clubs = load_clubs_from_directory((root / "clubs").string());
    content.courses = load_courses_from_directory((root / "courses").string());
    if (content.courses.empty()) {
        content.courses.push_back(fallback_course_definition());
    }
    content.shops = load_shops_from_directory((root / "shops").string());
    if (content.shops.empty()) {
        content.shops = fallback_shop_definitions();
    }
    content.quests = load_quests_from_directory((root / "quests").string());
    return content;
}
