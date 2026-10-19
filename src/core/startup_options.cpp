#include "core/startup_options.h"

#include "game/sky_clock.h"

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <cstdlib>
#include <string>
#include <vector>

namespace {
bool is_space(const char value) {
    return std::isspace(static_cast<unsigned char>(value)) != 0;
}

std::string trimmed(const char* value) {
    if (value == nullptr) {
        return std::string();
    }
    const std::string text(value);
    std::size_t first = 0;
    while (first < text.size() && is_space(text[first])) {
        ++first;
    }
    std::size_t last = text.size();
    while (last > first && is_space(text[last - 1U])) {
        --last;
    }
    return text.substr(first, last - first);
}

// A whole number made only of digits, at most 5 of them; 0 otherwise.
int positive_number(const std::string& text) {
    constexpr std::size_t max_digits = 5;
    if (text.empty() || text.size() > max_digits) {
        return 0;
    }
    int value = 0;
    for (const char character : text) {
        if (std::isdigit(static_cast<unsigned char>(character)) == 0) {
            return 0;
        }
        value = value * 10 + (character - '0');
    }
    return value;
}

std::string lowercase(std::string text) {
    for (char& character : text) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    return text;
}
}

startup_options parse_startup_options(const char* vsync_value, const char* course_value) {
    startup_options options;

    const std::string vsync = lowercase(trimmed(vsync_value));
    if (vsync == "0" || vsync == "off" || vsync == "no" || vsync == "false" ||
        vsync == "disable" || vsync == "disabled") {
        options.vsync = false;
    }

    options.boot_course_id = trimmed(course_value);
    return options;
}

startup_options with_review_options(startup_options options, const char* window_value, const char* screenshot_value) {
    const std::string window = lowercase(trimmed(window_value));
    const std::size_t separator = window.find('x');
    if (separator != std::string::npos) {
        const int width = positive_number(window.substr(0, separator));
        const int height = positive_number(window.substr(separator + 1U));
        if (width > 0 && height > 0) {
            options.window_width = width;
            options.window_height = height;
        }
    }
    options.screenshot_path = trimmed(screenshot_value);
    return options;
}

startup_options with_game_time(startup_options options, const char* game_time_value) {
    const std::string text = trimmed(game_time_value);
    if (text.empty()) {
        return options;
    }
    constexpr std::size_t date_length = 10;
    const bool dated = text.size() > date_length && text[date_length] == 'T';
    const std::optional<std::int64_t> date = dated ? parse_civil_date(text.substr(0, date_length)) : std::nullopt;
    if (dated && !date) {
        return options;
    }
    const std::string hour_text = dated ? text.substr(date_length + 1U) : text;
    char* end = nullptr;
    const float hour = std::strtof(hour_text.c_str(), &end);
    if (!hour_text.empty() && !is_space(hour_text.front()) && end == hour_text.c_str() + hour_text.size() && hour >= 0.0f &&
        hour <= 24.0f) {
        options.game_hour = hour;
        options.game_date = date;
    }
    return options;
}

startup_options with_online_options(startup_options options,
                                    const char* server_value,
                                    const char* database_value,
                                    const std::vector<std::string>& arguments) {
    options.online_server = trimmed(server_value);
    options.online_database = trimmed(database_value);
    for (std::size_t i = 0; i < arguments.size(); ++i) {
        const std::string& argument = arguments[i];
        const bool has_value = i + 1 < arguments.size();
        if (argument == "--server" && has_value) {
            options.online_server = trimmed(arguments[++i].c_str());
        } else if (argument == "--db" && has_value) {
            options.online_database = trimmed(arguments[++i].c_str());
        } else if (argument == "--anonymous") {
            options.online_anonymous = true;
        }
    }
    return options;
}
