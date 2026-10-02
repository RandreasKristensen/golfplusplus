#include "doctest.h"

#include "game/pixel_font_data.h"
#include "game/text_assets.h"
#include "game/text_ids.h"
#include "game/utf8.h"
#include "renderer/pixel_font.h"

#include <cmath>
#include <optional>
#include <string>

TEST_CASE("utf8 decodes and encodes ASCII and Danish letters") {
    CHECK(decode_utf8("AB") == U"AB");
    CHECK(decode_utf8(u8"ÆØÅ æøå") == U"ÆØÅ æøå");
    CHECK(encode_utf8(U"ÆØÅ æøå") == std::string(u8"ÆØÅ æøå"));
    CHECK(encode_utf8(decode_utf8(u8"€ 𝄞")) == std::string(u8"€ 𝄞"));
    CHECK(decode_utf8("").empty());
}

TEST_CASE("malformed utf8 decodes to the replacement character") {
    CHECK(decode_utf8("\xC3") == U"�");          // truncated
    CHECK(decode_utf8("A\xFF" "B") == U"A�B");    // invalid lead byte
    CHECK(decode_utf8("\x80") == U"�");          // stray continuation
    CHECK(decode_utf8("\xC0\x80") == U"�");      // overlong NUL
    CHECK(decode_utf8("\xED\xA0\x80") == U"�");  // surrogate
}

TEST_CASE("to_upper_letter covers ASCII and Latin-1 letters only") {
    CHECK(to_upper_letter(U'a') == U'A');
    CHECK(to_upper_letter(U'æ') == U'Æ');
    CHECK(to_upper_letter(U'ø') == U'Ø');
    CHECK(to_upper_letter(U'å') == U'Å');
    CHECK(to_upper_letter(U'Å') == U'Å');
    CHECK(to_upper_letter(U'÷') == U'÷');
    CHECK(to_upper_letter(U'1') == U'1');
}

TEST_CASE("text layout counts Danish letters as one glyph each") {
    const std::optional<text_assets> text = load_text_assets(GOLFPP_ASSETS_DIR);
    CHECK(text.has_value());
    if (!text) {
        return;
    }
    const overlay_grid grid{640, 360};
    const text_layout layout = layout_text_at_scale(text->font,
                                                    find_text_style(*text, style_body),
                                                    u8"æøå",
                                                    ui_rect{glm::vec2(0.0f), glm::vec2(0.5f)},
                                                    grid,
                                                    1);
    REQUIRE(layout.lines.size() == 1U);
    CHECK(layout.lines[0].glyphs == U"æøå");  // drawn with the uppercase glyphs
    const float pixels = static_cast<float>(find_glyph(text->font, U'Æ').width + 1 +
                                            find_glyph(text->font, U'Ø').width + 1 +
                                            find_glyph(text->font, U'Å').width);
    CHECK(std::abs(layout.bounds.half_size.x * 2.0f - pixels * 2.0f / 640.0f) < 1e-5f);
}
