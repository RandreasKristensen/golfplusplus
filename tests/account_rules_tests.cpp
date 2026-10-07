#include "doctest.h"

#include "account_rules.h"
#include "game/pixel_font_data.h"

#include <set>
#include <string>
#include <vector>

namespace {
// A font with A-Z, Å, Ø, digits and space, and the name limits these tests
// assume, so shipped font and tuning edits do not move them.
pixel_font_data name_font() {
    pixel_font_data font;
    font.height = 1;
    const pixel_glyph glyph{1, {1}, 1};
    for (char32_t c = U'A'; c <= U'Z'; ++c) {
        font.glyphs[c] = glyph;
    }
    for (char32_t c = U'0'; c <= U'9'; ++c) {
        font.glyphs[c] = glyph;
    }
    font.glyphs[U'Å'] = glyph;
    font.glyphs[U'Ø'] = glyph;
    font.glyphs[U' '] = glyph;
    font.glyphs[U'_'] = glyph;
    return font;
}

server_tuning name_tuning() {
    server_tuning tuning;
    tuning.name_min_length = 3;
    tuning.name_max_length = 12;
    return tuning;
}

name_check check_name(const std::string& name) {
    return check_player_name(name, name_font(), name_tuning());
}

login_config official_config() {
    login_config config;
    config.auth_issuer = "https://auth.example";
    config.auth_audience = "golfpp";
    return config;
}

login_claims claims_from(const std::string& issuer) {
    return login_claims{true, issuer, {"golfpp"}};
}

const std::string secret(min_link_secret_length, 's');
}

TEST_CASE("a token's issuer and audience are read from its payload") {
    const login_claims one = parse_login_claims(R"({"iss": "https://auth.example", "aud": "golfpp", "sub": "x"})");
    CHECK(one.present);
    CHECK(one.issuer == "https://auth.example");
    CHECK(one.audience == std::vector<std::string>{"golfpp"});
    const login_claims several = parse_login_claims(R"({"iss":"localhost","aud":["spacetimedb","golfpp"]})");
    const std::vector<std::string> both{"spacetimedb", "golfpp"};
    CHECK(several.audience == both);
    CHECK(!parse_login_claims("not json").present);
}

TEST_CASE("player names are trimmed and case-folded into a key") {
    const name_check check = check_name("  Åse Bo 7 ");
    CHECK(check.error.empty());
    CHECK(check.name == "Åse Bo 7");
    CHECK(check.key == "ÅSE BO 7");
    CHECK(check_name("åSE bo 7").key == check.key);
}

TEST_CASE("player names refuse bad lengths, characters and spacing") {
    CHECK(check_name("ABC").error.empty());
    CHECK(check_name("ABCDEFGHIJKL").error.empty());
    CHECK(check_name("ØØØØØØØØØØØØ").error.empty());  // twelve code points, more bytes
    CHECK(check_name("AB").error == error_invalid_name);
    CHECK(check_name(" AB ").error == error_invalid_name);  // too short once trimmed
    CHECK(check_name("ABCDEFGHIJKLM").error == error_invalid_name);
    CHECK(check_name("AB  CD").error == error_invalid_name);
    CHECK(check_name("AB_CD").error == error_invalid_name);  // a glyph, but not a letter
    CHECK(check_name("ABÆ").error == error_invalid_name);    // a letter without a glyph
    CHECK(check_name("AB\tCD").error == error_invalid_name);
    CHECK(check_name("AB\xc2\xa0" "CD").error == error_invalid_name);  // no-break space
    CHECK(check_name("ABC\xff").error == error_invalid_name);
    CHECK(check_name("    ").error == error_invalid_name);
}

TEST_CASE("sip_hash_24 matches the reference test vector") {
    // Key 00..0f, message 00..0e (SipHash paper, appendix A).
    std::string message;
    for (int i = 0; i < 15; ++i) {
        message.push_back(static_cast<char>(i));
    }
    CHECK(sip_hash_24(0x0706050403020100ULL, 0x0f0e0d0c0b0a0908ULL, message) == 0xa129ca6149be45e5ULL);
}

TEST_CASE("link codes depend on the secret and use the unambiguous alphabet") {
    std::set<std::string> codes;
    for (const char* nonce : {"1:1:7:0", "1:1:7:1", "2:1:7:0", "1:2:7:0"}) {
        const std::string code = make_link_code(secret, nonce);
        CHECK(code.size() == static_cast<std::size_t>(link_code_length));
        CHECK(code.find_first_not_of(link_code_alphabet) == std::string::npos);
        CHECK(make_link_code(secret, nonce) == code);
        codes.insert(code);
    }
    CHECK(codes.size() == 4U);
    CHECK(make_link_code(std::string(min_link_secret_length, 't'), "1:1:7:0") != make_link_code(secret, "1:1:7:0"));
    CHECK(std::string(link_code_alphabet).find_first_of("01OI") == std::string::npos);
    CHECK(link_code_from_bits(0) == std::string(static_cast<std::size_t>(link_code_length), link_code_alphabet[0]));
    CHECK(normalize_link_code(" ab2c-d3ef ") == "AB2CD3EF");
}

TEST_CASE("rate windows allow the limit, then refuse until the period passes") {
    const std::int64_t hour = 3600LL * 1000000LL;
    rate_window window;
    for (int i = 0; i < 3; ++i) {
        const std::optional<rate_window> used = use_rate_window(window, hour * 10 + i, hour, 3);
        REQUIRE(used.has_value());
        window = *used;
    }
    CHECK(window.count == 3U);
    CHECK(!use_rate_window(window, hour * 10 + 100, hour, 3).has_value());
    const std::optional<rate_window> later = use_rate_window(window, hour * 11, hour, 3);
    REQUIRE(later.has_value());
    CHECK(later->count == 1U);
    CHECK(!use_rate_window(rate_window{}, hour * 20, hour, 0).has_value());
}

TEST_CASE("logins from our issuer for our client are browser logins") {
    const login_config config = official_config();
    CHECK(check_login(claims_from(config.auth_issuer), config, false) == login_method::browser);

    login_claims wrong_audience = claims_from(config.auth_issuer);
    wrong_audience.audience = {"someone_else"};
    CHECK(!check_login(wrong_audience, config, false).has_value());
}

TEST_CASE("other tokens are anonymous, allowed only by config or for the owner") {
    login_config config = official_config();
    const login_claims local = claims_from("http://localhost");
    CHECK(!check_login(local, config, false).has_value());
    CHECK(!check_login(login_claims{}, config, false).has_value());
    CHECK(check_login(local, config, true) == login_method::anonymous);

    config.allow_anonymous = true;
    CHECK(check_login(local, config, false) == login_method::anonymous);
    CHECK(check_login(login_claims{}, config, false) == login_method::anonymous);
}

TEST_CASE("only anonymous logins are guests") {
    CHECK(is_guest_login(login_method_name(login_method::anonymous)));
    CHECK(!is_guest_login(login_method_name(login_method::browser)));
}
