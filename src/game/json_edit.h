#pragma once

// Editing one value in JSON text in place: the rest of the text, its
// layout, key order and line endings, stays byte for byte. For tools that
// write content back (core/look_tuning.h); loaders read with json_util.h.

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

// Member names and array indices (in decimal) from the root down.
using json_path = std::vector<std::string>;

// "weather/sun_glow" as {"weather", "sun_glow"}; nullopt for an empty segment.
std::optional<json_path> parse_json_path(const std::string& text);

struct text_span {
    std::size_t begin = 0;
    std::size_t end = 0;  // one past the last byte
};

// Where the value at `path` is written in `text`; nullopt when the text is
// not one JSON value or nothing is at `path`. Of repeated keys, the first.
std::optional<text_span> json_value_span(const std::string& text, const json_path& path);

// `text` with the value at `path` written as `value` (JSON text), the rest
// unchanged; nullopt when nothing is at `path`.
std::optional<std::string> replace_json_value(const std::string& text, const json_path& path, const std::string& value);

// `value` with `decimals` places, trailing zeros dropped; with
// `keep_point`, at least one place ("2.0"), as a number written with a
// decimal point is kept one.
std::string json_number_text(double value, int decimals, bool keep_point);

// The places a number's JSON text is written with ("0.035" 3, "16" 0).
int json_number_decimals(const std::string& number);
