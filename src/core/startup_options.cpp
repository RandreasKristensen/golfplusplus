#include "core/startup_options.h"

#include <cctype>
#include <string>

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
