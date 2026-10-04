#include "doctest.h"

#include "game/game_content.h"
#include "game/text_assets.h"
#include "game/text_ids.h"
#include "game/utf8.h"

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
  "line_gap": 1,
  "ellipsis": "A",
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

namespace {
// The values of `constexpr const char* <prefix>... = "...";` lines in `path`.
std::vector<std::string> declared_values(const std::string& path, const std::string& prefix) {
    std::ifstream file(path);
    std::stringstream contents;
    contents << file.rdbuf();
    const std::string header = contents.str();
    std::vector<std::string> values;
    const std::regex pattern(R"re(constexpr const char\* (\w+) = "([^"]*)";)re");
    for (auto it = std::sregex_iterator(header.begin(), header.end(), pattern); it != std::sregex_iterator(); ++it) {
        if ((*it)[1].str().rfind(prefix, 0) == 0) {
            values.push_back((*it)[2].str());
        }
    }
    return values;
}
}

TEST_CASE("every online failure and link result the game can receive has a string") {
    const std::optional<text_assets> text = shipped_text();
    REQUIRE(text.has_value());
    const std::string source = GOLFPP_SOURCE_DIR;
    const std::string server = source + "/../server/golfpp_module/src";

    std::vector<std::string> errors = declared_values(server + "/server_errors.h", "error_");
    const std::vector<std::string> bridge = declared_values(source + "/game/net_types.h", "net_failure_");
    CHECK(errors.size() > 20U);
    CHECK(bridge.size() > 5U);
    errors.insert(errors.end(), bridge.begin(), bridge.end());
    for (const std::string& id : errors) {
        CHECK(text->strings.entries.count(online_error_text_key(id)) == 1U);
    }

    const std::vector<std::string> links = declared_values(source + "/game/net_types.h", "link_result_");
    CHECK(links.size() == 5U);
    for (const std::string& id : links) {
        CHECK(text->strings.entries.count(online_link_text_key(id)) == 1U);
    }
}

TEST_CASE("every skill has a name in the string table") {
    const std::optional<text_assets> text = shipped_text();
    if (!text) {
        CHECK(false);
        return;
    }
    const game_content_load_result content = load_game_content(GOLFPP_ASSETS_DIR);
    REQUIRE(content.content.has_value());
    for (const skill_definition& skill : content.content->skills) {
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
        for (const char32_t c : decode_utf8(without_placeholders(entry.second))) {
            if (c == U'\n') {
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

TEST_CASE("font draws Danish letters, lowercase with the uppercase glyph") {
    const std::optional<text_assets> text = shipped_text();
    if (!text) {
        CHECK(false);
        return;
    }
    const std::u32string upper = U"ÆØÅ";
    const std::u32string lower = U"æøå";
    for (std::size_t i = 0; i < upper.size(); ++i) {
        CHECK(font_has_glyph(text->font, upper[i]));
        CHECK(&find_glyph(text->font, upper[i]) != &text->font.fallback);
        CHECK(&find_glyph(text->font, lower[i]) == &find_glyph(text->font, upper[i]));
    }
    const std::string charset = font_charset(text->font);
    CHECK(charset.find(u8"Æ") != std::string::npos);
    CHECK(charset.find(u8"æ") == std::string::npos);
}

TEST_CASE("missing glyphs draw the fallback box, not a letter") {
    const std::optional<text_assets> text = shipped_text();
    if (!text) {
        CHECK(false);
        return;
    }
    CHECK(!font_has_glyph(text->font, '~'));
    CHECK(&find_glyph(text->font, '~') == &text->font.fallback);
    CHECK(&find_glyph(text->font, U'é') == &text->font.fallback);
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
    CHECK(font->line_gap == 1);
    CHECK(font->ellipsis == U"A");
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
    CHECK(!parse_pixel_font(R"({"height": 1, "line_gap": 1, "ellipsis": ".", "fallback": ["1"], "glyphs": {".": ["1"], "æ": ["1"]}})").has_value());
    CHECK(parse_pixel_font(R"({"height": 1, "line_gap": 1, "ellipsis": ".", "fallback": ["1"], "glyphs": {".": ["1"], "Æ": ["1"]}})").has_value());
    // The line gap and an ellipsis made of glyphs the font has are required.
    CHECK(!parse_pixel_font(R"({"height": 1, "ellipsis": ".", "fallback": ["1"], "glyphs": {".": ["1"]}})").has_value());
    CHECK(!parse_pixel_font(R"({"height": 1, "line_gap": 1, "fallback": ["1"], "glyphs": {".": ["1"]}})").has_value());
    CHECK(!parse_pixel_font(R"({"height": 1, "line_gap": 1, "ellipsis": "~", "fallback": ["1"], "glyphs": {".": ["1"]}})").has_value());
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
      "title": { "fill": 0.8, "width_fill": 0.7, "min_scale": 2, "max_scale": 4, "wrap": true,
                 "color": [0.95, 0.78, 0.28], "align": "right", "valign": "top" },
      "body": { "fill": 0.5, "color": [0.84, 0.84, 0.74] }
    }})");
    CHECK(set.has_value());
    if (!set) {
        return;
    }
    const text_style& title = find_text_style(*set, "title");
    CHECK(title.fill == 0.8f);
    CHECK(title.width_fill == 0.7f);
    CHECK(title.min_scale == 2);
    CHECK(title.max_scale == std::optional<int>(4));
    CHECK(title.wrap);
    CHECK(title.color == glm::vec3(0.95f, 0.78f, 0.28f));
    CHECK(title.align == text_align::right);
    CHECK(title.valign == text_valign::top);

    const text_style& body = find_text_style(*set, "body");
    CHECK(body.width_fill > 0.0f);
    CHECK(body.width_fill <= 1.0f);
    CHECK(body.min_scale == 1);
    CHECK(!body.max_scale.has_value());
    CHECK(!body.wrap);
    CHECK(body.align == text_align::left);
    CHECK(body.valign == text_valign::center);

    CHECK(&find_text_style(*set, "nope") == &set->missing);

    CHECK(!parse_text_styles(R"({"styles": {"a": {"color": [1, 1, 1]}}})").has_value());
    CHECK(!parse_text_styles(R"({"styles": {"a": {"fill": 0.5, "color": [1, 1]}}})").has_value());
    CHECK(!parse_text_styles(R"({"styles": {"a": {"fill": 1.5, "color": [1, 1, 1]}}})").has_value());
    CHECK(!parse_text_styles(R"({"styles": {"a": {"fill": 0.5, "width_fill": 0, "color": [1, 1, 1]}}})").has_value());
    CHECK(!parse_text_styles(R"({"styles": {"a": {"fill": 0.5, "min_scale": 0, "color": [1, 1, 1]}}})").has_value());
    CHECK(!parse_text_styles(R"({"styles": {"a": {"fill": 0.5, "min_scale": 3, "max_scale": 2, "color": [1, 1, 1]}}})").has_value());
    CHECK(!parse_text_styles(R"({"styles": {"a": {"fill": 0.5, "wrap": 1, "color": [1, 1, 1]}}})").has_value());
    CHECK(!parse_text_styles(R"({"styles": {"a": {"fill": 0.5, "color": [1, 1, 1], "align": "justify"}}})").has_value());
    CHECK(!parse_text_styles(R"({"styles": {"a": {"fill": 0.5, "color": [1, 1, 1], "valign": "middle"}}})").has_value());
}
