#include "core/startup_options.h"

#include <cctype>
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
