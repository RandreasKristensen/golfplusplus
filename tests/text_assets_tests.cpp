#include "doctest.h"

#include "game/game_content.h"
#include "game/text_assets.h"
#include "game/text_ids.h"

#include <fstream>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {
std::optional<text_assets> shipped_text() {
    return load_text_assets(GOLFPP_ASSETS_DIR);
}

// Every `constexpr const char* <name> = "<value>";` line in text_ids.h.
std::vector<std::pair<std::string, std::string>> declared_ids() {
    std::ifstream file(std::string(GOLFPP_SOURCE_DIR) + "/game/text_ids.h");
    std::stringstream contents;
    contents << file.rdbuf();
    const std::string header = contents.str();

    std::vector<std::pair<std::string, std::string>> ids;
    const std::regex pattern(R"re(constexpr const char\* (\w+) = "([^"]*)";)re");
    for (auto it = std::sregex_iterator(header.begin(), header.end(), pattern); it != std::sregex_iterator(); ++it) {
        ids.emplace_back((*it)[1].str(), (*it)[2].str());
    }
    return ids;
}

bool starts_with(const std::string& value, const char* prefix) {
    return value.rfind(prefix, 0) == 0;
}

// Placeholders like "{hole}" are replaced before drawing, so their braces and
// names never reach the font.
std::string without_placeholders(const std::string& value) {
    return std::regex_replace(value, std::regex(R"(\{[a-z_]+\})"), "");
}

const char* small_font = R"({
  "height": 2,
  "fallback": ["11", "11"],
  "glyphs": { "A": ["10", "01"], " ": ["0", "0"] }
})";
}

TEST_CASE("shipped text assets load") {
    const std::optional<text_assets> text = shipped_text();
    CHECK(text.has_value());
    if (!text) {
        return;
    }
    CHECK(text->font.height == 7);
    CHECK(!text->strings.entries.empty());
    CHECK(!text->styles.styles.empty());
}

TEST_CASE("every text id in text_ids.h exists in en.json and every style id in text_styles.json") {
    const std::optional<text_assets> text = shipped_text();
    const std::vector<std::pair<std::string, std::string>> ids = declared_ids();
    CHECK(ids.size() > 50U);
    if (!text) {
        return;
    }

    for (const auto& id : ids) {
        if (starts_with(id.first, "text_")) {
            CHECK(text->strings.entries.count(id.second) == 1U);
        } else if (starts_with(id.first, "style_")) {
            CHECK(text->styles.styles.count(id.second) == 1U);
        } else {
            CHECK(false);  // ids must be text_* or style_*
        }
    }
}

TEST_CASE("every skill has a name in the string table") {
    const std::optional<text_assets> text = shipped_text();
    if (!text) {
        CHECK(false);
        return;
    }
    for (const skill_definition& skill : load_game_content(GOLFPP_ASSETS_DIR).content->skills) {
        CHECK(text->strings.entries.count(skill_text_key(skill.id)) == 1U);
    }
}

TEST_CASE("every character in every string has a glyph in the font") {
    const std::optional<text_assets> text = shipped_text();
    if (!text) {
        CHECK(false);
        return;
    }
    for (const auto& entry : text->strings.entries) {
        for (const char c : without_placeholders(entry.second)) {
            if (c == '\n') {
                continue;
            }
            CHECK(font_has_glyph(text->font, c));
        }
    }
}

TEST_CASE("font covers the name and menu charset") {
    const std::optional<text_assets> text = shipped_text();
    if (!text) {
        CHECK(false);
        return;
    }
    const std::string required = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -_.,!?:/+()'#@";
    for (const char c : required) {
        CHECK(font_has_glyph(text->font, c));
    }
    CHECK(font_has_glyph(text->font, 'q'));
    CHECK(font_charset(text->font).find('Q') != std::string::npos);
    CHECK(font_charset(text->font).find('q') == std::string::npos);
}

TEST_CASE("missing glyphs draw the fallback box, not a letter") {
    const std::optional<text_assets> text = shipped_text();
    if (!text) {
        CHECK(false);
        return;
    }
    CHECK(!font_has_glyph(text->font, '~'));
    CHECK(&find_glyph(text->font, '~') == &text->font.fallback);
    CHECK(&find_glyph(text->font, '\xe9') == &text->font.fallback);
    CHECK(text->font.fallback.pixels != find_glyph(text->font, 'I').pixels);
    CHECK(text->font.fallback.pixels != find_glyph(text->font, '?').pixels);
}

TEST_CASE("pixel font parses rows and rejects malformed glyphs") {
    const std::optional<pixel_font_data> font = parse_pixel_font(small_font);
    CHECK(font.has_value());
    if (!font) {
        return;
    }
    CHECK(font->height == 2);
    CHECK(find_glyph(*font, 'A').width == 2);
    CHECK(find_glyph(*font, 'A').lit_pixel_count == 2);
    CHECK(find_glyph(*font, 'a').pixels == find_glyph(*font, 'A').pixels);
    CHECK(find_glyph(*font, ' ').lit_pixel_count == 0);
    CHECK(find_glyph(*font, 'B').lit_pixel_count == 4);  // fallback
    CHECK(font_charset(*font) == " A");

    CHECK(!parse_pixel_font("not json").has_value());
    CHECK(!parse_pixel_font(R"({"height": 2, "fallback": ["1"], "glyphs": {}})").has_value());
    CHECK(!parse_pixel_font(R"({"height": 1, "fallback": ["1"], "glyphs": {"A": ["12"]}})").has_value());
    CHECK(!parse_pixel_font(R"({"height": 2, "fallback": ["1", "1"], "glyphs": {"A": ["10", "1"]}})").has_value());
    CHECK(!parse_pixel_font(R"({"height": 1, "fallback": ["1"], "glyphs": {"a": ["1"]}})").has_value());
    CHECK(!parse_pixel_font(R"({"height": 1, "fallback": ["1"], "glyphs": {"AB": ["1"]}})").has_value());
}

TEST_CASE("string table looks up, formats and marks missing keys") {
    const std::optional<string_table> table = parse_string_table(R"({
      "hud.power": "POWER",
      "hud.hole_of": "HOLE {hole} OF {count}",
      "hud.odd": "{a}{a} {unknown} {"
    })");
    CHECK(table.has_value());
    if (!table) {
        return;
    }
    CHECK(lookup_text(*table, "hud.power") == "POWER");
    CHECK(lookup_text(*table, "hud.nope") == "#hud.nope#");
    CHECK(format_text(*table, "hud.hole_of", {{"hole", "3"}, {"count", "18"}}) == "HOLE 3 OF 18");
    CHECK(format_text(*table, "hud.hole_of", {{"hole", "3"}}) == "HOLE 3 OF {count}");
    CHECK(format_text(*table, "hud.odd", {{"a", "X"}}) == "XX {unknown} {");
    CHECK(format_text(*table, "hud.nope", {{"a", "X"}}) == "#hud.nope#");

    CHECK(!parse_string_table(R"({"a": 1})").has_value());
    CHECK(!parse_string_table(R"(["a"])").has_value());
}

TEST_CASE("text styles parse with defaults and reject bad values") {
    const std::optional<text_style_set> set = parse_text_styles(R"({ "styles": {
      "title": { "pixel_size": 0.028, "min_pixel_size": 0.014, "color": [0.95, 0.78, 0.28], "align": "center" },
      "body": { "pixel_size": 0.0092, "color": [0.84, 0.84, 0.74] }
    }})");
    CHECK(set.has_value());
    if (!set) {
        return;
    }
    const text_style& title = find_text_style(*set, "title");
    CHECK(title.pixel_size == 0.028f);
    CHECK(title.min_pixel_size == 0.014f);
    CHECK(title.color == glm::vec3(0.95f, 0.78f, 0.28f));
    CHECK(title.align == text_align::center);

    const text_style& body = find_text_style(*set, "body");
    CHECK(body.min_pixel_size == body.pixel_size);
    CHECK(body.align == text_align::left);

    CHECK(&find_text_style(*set, "nope") == &set->missing);

    CHECK(!parse_text_styles(R"({"styles": {"a": {"color": [1, 1, 1]}}})").has_value());
    CHECK(!parse_text_styles(R"({"styles": {"a": {"pixel_size": 0.01, "color": [1, 1]}}})").has_value());
    CHECK(!parse_text_styles(R"({"styles": {"a": {"pixel_size": 0.01, "color": [1, 1, 1], "align": "right"}}})").has_value());
    CHECK(!parse_text_styles(R"({"styles": {"a": {"pixel_size": 0.01, "min_pixel_size": 0.02, "color": [1, 1, 1]}}})").has_value());
}

TEST_CASE("shipped styles have the sizes the HUD layout is built around") {
    const std::optional<text_assets> text = shipped_text();
    if (!text) {
        CHECK(false);
        return;
    }
    CHECK(find_text_style(*text, style_title).pixel_size == 0.028f);
    CHECK(find_text_style(*text, style_title).min_pixel_size == 0.014f);
    CHECK(find_text_style(*text, style_body).pixel_size == 0.0092f);
    CHECK(find_text_style(*text, style_hud_label).pixel_size == 0.015f);
    CHECK(find_text_style(*text, style_scorecard_row_compact).pixel_size == 0.0090f);
    CHECK(find_text_style(*text, style_debug_profile).pixel_size == 0.006f);
}
