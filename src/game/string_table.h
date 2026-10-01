#pragma once

// On-screen strings as data (assets/text/<language>.json): a flat object of
// key -> string. Keys used by code live in game/text_ids.h. Strings may hold
// named placeholders ("HOLE {hole} OF {count}") filled by format_text.

#include <initializer_list>
#include <optional>
#include <string>
#include <unordered_map>

struct string_table {
    std::unordered_map<std::string, std::string> entries;
};

struct text_arg {
    const char* name = "";
    std::string value;
};

// Every value must be a string; returns nullopt otherwise.
std::optional<string_table> parse_string_table(const std::string& text);

// The string for `key`, or "#key#" when it is missing so gaps show in game.
std::string lookup_text(const string_table& table, const char* key);
// lookup_text with each "{name}" replaced by its argument's value. Unknown
// placeholders are left as written.
std::string format_text(const string_table& table, const char* key, std::initializer_list<text_arg> args);
std::string fill_placeholders(const std::string& pattern, std::initializer_list<text_arg> args);
