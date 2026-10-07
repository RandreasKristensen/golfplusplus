#include "account_rules.h"

#include "game/json_util.h"
#include "game/utf8.h"

#include <algorithm>
#include <cstddef>
#include <cstring>

namespace {
bool is_name_letter(const char32_t c) {
    const char32_t upper = to_upper_letter(c);
    const bool ascii = upper >= U'A' && upper <= U'Z';
    const bool latin1 = upper >= 0xC0 && upper <= 0xDE && upper != 0xD7;
    return ascii || latin1;
}

std::uint64_t rotl(const std::uint64_t value, const int bits) {
    return (value << bits) | (value >> (64 - bits));
}

void sip_round(std::uint64_t& v0, std::uint64_t& v1, std::uint64_t& v2, std::uint64_t& v3) {
    v0 += v1;
    v1 = rotl(v1, 13);
    v1 ^= v0;
    v0 = rotl(v0, 32);
    v2 += v3;
    v3 = rotl(v3, 16);
    v3 ^= v2;
    v0 += v3;
    v3 = rotl(v3, 21);
    v3 ^= v0;
    v2 += v1;
    v1 = rotl(v1, 17);
    v1 ^= v2;
    v2 = rotl(v2, 32);
}

bool is_name_digit(const char32_t c) {
    return c >= U'0' && c <= U'9';
}
}

name_check check_player_name(const std::string& requested, const pixel_font_data& font, const server_tuning& tuning) {
    name_check check;
    std::u32string name = decode_utf8(requested);
    const auto first = std::find_if(name.begin(), name.end(), [](const char32_t c) { return c != U' '; });
    const auto last = std::find_if(name.rbegin(), name.rend(), [](const char32_t c) { return c != U' '; }).base();
    name = first < last ? std::u32string(first, last) : std::u32string();

    const int length = static_cast<int>(name.size());
    bool valid = length >= tuning.name_min_length && length <= tuning.name_max_length;
    for (std::size_t i = 0; valid && i < name.size(); ++i) {
        const char32_t c = name[i];
        const bool double_space = c == U' ' && i > 0 && name[i - 1] == U' ';
        valid = (is_name_letter(c) || is_name_digit(c) || c == U' ') && !double_space && font_has_glyph(font, c);
    }
    if (!valid) {
        check.error = error_invalid_name;
        return check;
    }

    std::u32string key = name;
    std::transform(key.begin(), key.end(), key.begin(), to_upper_letter);
    check.name = encode_utf8(name);
    check.key = encode_utf8(key);
    return check;
}

// Little-endian message words, as in the reference implementation.
std::uint64_t sip_hash_24(const std::uint64_t k0, const std::uint64_t k1, const std::string& message) {
    std::uint64_t v0 = 0x736f6d6570736575ULL ^ k0;
    std::uint64_t v1 = 0x646f72616e646f6dULL ^ k1;
    std::uint64_t v2 = 0x6c7967656e657261ULL ^ k0;
    std::uint64_t v3 = 0x7465646279746573ULL ^ k1;
    const std::size_t length = message.size();
    const std::size_t whole = length - length % 8;
    for (std::size_t i = 0; i < whole; i += 8) {
        std::uint64_t word = 0;
        for (int b = 0; b < 8; ++b) {
            word |= static_cast<std::uint64_t>(static_cast<unsigned char>(message[i + static_cast<std::size_t>(b)])) << (8 * b);
        }
        v3 ^= word;
        sip_round(v0, v1, v2, v3);
        sip_round(v0, v1, v2, v3);
        v0 ^= word;
    }
    std::uint64_t last = static_cast<std::uint64_t>(length & 0xFFU) << 56;
    for (std::size_t i = whole; i < length; ++i) {
        last |= static_cast<std::uint64_t>(static_cast<unsigned char>(message[i])) << (8 * (i - whole));
    }
    v3 ^= last;
    sip_round(v0, v1, v2, v3);
    sip_round(v0, v1, v2, v3);
    v0 ^= last;
    v2 ^= 0xFF;
    for (int i = 0; i < 4; ++i) {
        sip_round(v0, v1, v2, v3);
    }
    return v0 ^ v1 ^ v2 ^ v3;
}

std::uint64_t keyed_hash(const std::string& secret, const std::string& message) {
    const std::uint64_t k0 = sip_hash_24(0, 0, secret + "\x01");
    const std::uint64_t k1 = sip_hash_24(0, 0, secret + "\x02");
    return sip_hash_24(k0, k1, message);
}

std::string make_link_code(const std::string& secret, const std::string& nonce) {
    return link_code_from_bits(keyed_hash(secret, nonce));
}

std::string link_code_from_bits(std::uint64_t bits) {
    const std::size_t alphabet_size = std::strlen(link_code_alphabet);
    std::string code;
    for (int i = 0; i < link_code_length; ++i) {
        code.push_back(link_code_alphabet[bits % alphabet_size]);
        bits /= alphabet_size;
    }
    return code;
}

std::string normalize_link_code(const std::string& typed) {
    std::string code;
    for (const char c : typed) {
        if (c == ' ' || c == '-') {
            continue;
        }
        code.push_back(c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c);
    }
    return code;
}

std::optional<rate_window> use_rate_window(const rate_window& window,
                                           const std::int64_t now_micros,
                                           const std::int64_t period_micros,
                                           const std::uint32_t limit) {
    if (now_micros - window.start_micros >= period_micros) {
        return limit > 0 ? std::optional<rate_window>(rate_window{now_micros, 1}) : std::nullopt;
    }
    if (window.count >= limit) {
        return std::nullopt;
    }
    return rate_window{window.start_micros, window.count + 1};
}

const char* login_method_name(const login_method method) {
    switch (method) {
    case login_method::browser:
        return "browser";
    case login_method::anonymous:
        return "anonymous";
    }
    return "anonymous";
}

bool is_guest_login(const std::string& stored_method) {
    return stored_method == login_method_name(login_method::anonymous);
}

login_claims parse_login_claims(const std::string& payload) {
    const std::optional<json> root = parse_json(payload);
    if (!root || !root->is_object()) {
        return {};
    }
    login_claims claims;
    claims.present = true;
    claims.issuer = json_string(*root, "iss").value_or("");
    if (const std::optional<std::string> audience = json_string(*root, "aud")) {
        claims.audience = {*audience};
    } else {
        claims.audience = json_string_array(*root, "aud");
    }
    return claims;
}

std::optional<login_method> check_login(const login_claims& claims, const login_config& config, const bool is_owner) {
    const bool ours = claims.present && !config.auth_issuer.empty() && claims.issuer == config.auth_issuer &&
        std::find(claims.audience.begin(), claims.audience.end(), config.auth_audience) != claims.audience.end();
    if (ours) {
        return login_method::browser;
    }
    if (config.allow_anonymous || is_owner) {
        return login_method::anonymous;
    }
    return std::nullopt;
}
