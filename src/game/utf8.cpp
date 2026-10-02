#include "game/utf8.h"

namespace {
constexpr char32_t replacement_character = 0xFFFD;
constexpr char32_t max_code_point = 0x10FFFF;

bool is_continuation(const unsigned char byte) {
    return (byte & 0xC0U) == 0x80U;
}

// Lead byte -> sequence length and the payload bits it carries; 0 when it
// can't start a sequence.
int sequence_length(const unsigned char lead) {
    if (lead < 0x80U) {
        return 1;
    }
    if ((lead & 0xE0U) == 0xC0U) {
        return 2;
    }
    if ((lead & 0xF0U) == 0xE0U) {
        return 3;
    }
    if ((lead & 0xF8U) == 0xF0U) {
        return 4;
    }
    return 0;
}

char32_t smallest_for_length(const int length) {
    return length == 2 ? 0x80 : length == 3 ? 0x800 : 0x10000;
}
}

std::u32string decode_utf8(const std::string& text) {
    std::u32string result;
    result.reserve(text.size());
    std::size_t i = 0;
    while (i < text.size()) {
        const unsigned char lead = static_cast<unsigned char>(text[i]);
        const int length = sequence_length(lead);
        if (length == 1) {
            result.push_back(lead);
            ++i;
            continue;
        }
        if (length == 0) {
            result.push_back(replacement_character);
            ++i;
            continue;
        }

        char32_t code_point = lead & (0x7FU >> length);
        int consumed = 1;
        while (consumed < length && i + static_cast<std::size_t>(consumed) < text.size() &&
               is_continuation(static_cast<unsigned char>(text[i + static_cast<std::size_t>(consumed)]))) {
            code_point = (code_point << 6U) | (static_cast<unsigned char>(text[i + static_cast<std::size_t>(consumed)]) & 0x3FU);
            ++consumed;
        }
        const bool overlong = code_point < smallest_for_length(length);
        const bool surrogate = code_point >= 0xD800 && code_point <= 0xDFFF;
        if (consumed != length || overlong || surrogate || code_point > max_code_point) {
            result.push_back(replacement_character);
        } else {
            result.push_back(code_point);
        }
        i += static_cast<std::size_t>(consumed);
    }
    return result;
}

std::string encode_utf8(const std::u32string& code_points) {
    std::string result;
    result.reserve(code_points.size());
    for (char32_t c : code_points) {
        if (c > max_code_point || (c >= 0xD800 && c <= 0xDFFF)) {
            c = replacement_character;
        }
        if (c < 0x80) {
            result.push_back(static_cast<char>(c));
        } else if (c < 0x800) {
            result.push_back(static_cast<char>(0xC0U | (c >> 6U)));
            result.push_back(static_cast<char>(0x80U | (c & 0x3FU)));
        } else if (c < 0x10000) {
            result.push_back(static_cast<char>(0xE0U | (c >> 12U)));
            result.push_back(static_cast<char>(0x80U | ((c >> 6U) & 0x3FU)));
            result.push_back(static_cast<char>(0x80U | (c & 0x3FU)));
        } else {
            result.push_back(static_cast<char>(0xF0U | (c >> 18U)));
            result.push_back(static_cast<char>(0x80U | ((c >> 12U) & 0x3FU)));
            result.push_back(static_cast<char>(0x80U | ((c >> 6U) & 0x3FU)));
            result.push_back(static_cast<char>(0x80U | (c & 0x3FU)));
        }
    }
    return result;
}

char32_t to_upper_letter(const char32_t code_point) {
    const bool ascii_lower = code_point >= U'a' && code_point <= U'z';
    const bool latin1_lower = code_point >= 0xE0 && code_point <= 0xFE && code_point != 0xF7;
    return ascii_lower || latin1_lower ? code_point - 0x20 : code_point;
}
