#include "renderer/shader_source.h"

#include <cstddef>

namespace {
constexpr const char* include_prefix = "#include \"";

// The name in a line that is only `#include "name"` (surrounding blanks
// allowed); nullopt for any other line.
std::optional<std::string> included_name(const std::string& line) {
    const std::size_t first = line.find_first_not_of(" \t");
    const std::size_t last = line.find_last_not_of(" \t\r");
    if (first == std::string::npos || line.compare(first, std::char_traits<char>::length(include_prefix), include_prefix) != 0 ||
        line[last] != '"') {
        return std::nullopt;
    }
    const std::size_t name_start = first + std::char_traits<char>::length(include_prefix);
    if (last <= name_start) {
        return std::nullopt;
    }
    return line.substr(name_start, last - name_start);
}

bool has_include(const std::string& text) {
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = text.find('\n', start);
        if (included_name(text.substr(start, end == std::string::npos ? std::string::npos : end - start))) {
            return true;
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return false;
}
}

std::optional<std::string> expand_shader_includes(const std::string& source, const shader_include_reader& read_include) {
    std::string expanded;
    std::size_t start = 0;
    while (start < source.size()) {
        std::size_t end = source.find('\n', start);
        const bool last_line = end == std::string::npos;
        const std::string line = source.substr(start, last_line ? std::string::npos : end - start);
        if (const std::optional<std::string> name = included_name(line)) {
            const std::optional<std::string> included = read_include(*name);
            if (!included || has_include(*included)) {
                return std::nullopt;
            }
            expanded += *included;
            if (!included->empty() && included->back() != '\n') {
                expanded += '\n';
            }
        } else {
            expanded += line;
            if (!last_line) {
                expanded += '\n';
            }
        }
        start = last_line ? source.size() : end + 1;
    }
    return expanded;
}
