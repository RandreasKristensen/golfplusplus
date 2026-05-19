#include "game/hole_loader.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {
std::string read_file(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) {
        return {};
    }

    std::ostringstream stream;
    stream << file.rdbuf();
    return stream.str();
}

void skip_whitespace(const std::string& text, std::size_t& cursor) {
    while (cursor < text.size() && std::isspace(static_cast<unsigned char>(text[cursor])) != 0) {
        ++cursor;
    }
}

std::optional<std::size_t> find_matching(const std::string& text, const std::size_t begin, const char open, const char close) {
    int depth = 0;
    bool in_string = false;
    bool escaped = false;

    for (std::size_t i = begin; i < text.size(); ++i) {
        const char c = text[i];
        if (in_string) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                in_string = false;
            }
            continue;
        }

        if (c == '"') {
            in_string = true;
        } else if (c == open) {
            ++depth;
        } else if (c == close) {
            --depth;
            if (depth == 0) {
                return i;
            }
        }
    }

    return std::nullopt;
}

std::optional<std::size_t> value_begin_for_key(const std::string& text, const char* key) {
    const std::string needle = std::string("\"") + key + "\"";
    const std::size_t key_pos = text.find(needle);
    if (key_pos == std::string::npos) {
        return std::nullopt;
    }

    const std::size_t colon = text.find(':', key_pos + needle.size());
    if (colon == std::string::npos) {
        return std::nullopt;
    }

    std::size_t value_begin = colon + 1;
    skip_whitespace(text, value_begin);
    return value_begin;
}

std::optional<std::string> string_for_key(const std::string& text, const char* key) {
    std::optional<std::size_t> cursor = value_begin_for_key(text, key);
    if (!cursor || *cursor >= text.size() || text[*cursor] != '"') {
        return std::nullopt;
    }

    std::string value;
    bool escaped = false;
    for (std::size_t i = *cursor + 1; i < text.size(); ++i) {
        const char c = text[i];
        if (escaped) {
            value.push_back(c);
            escaped = false;
        } else if (c == '\\') {
            escaped = true;
        } else if (c == '"') {
            return value;
        } else {
            value.push_back(c);
        }
    }

    return std::nullopt;
}

std::optional<float> parse_float(const std::string& text, std::size_t& cursor) {
    skip_whitespace(text, cursor);
    char* end = nullptr;
    const float value = std::strtof(text.c_str() + cursor, &end);
    if (end == text.c_str() + cursor) {
        return std::nullopt;
    }

    cursor = static_cast<std::size_t>(end - text.c_str());
    return value;
}

std::optional<float> float_for_key(const std::string& text, const char* key) {
    std::optional<std::size_t> cursor = value_begin_for_key(text, key);
    if (!cursor) {
        return std::nullopt;
    }
    return parse_float(text, *cursor);
}

std::optional<int> int_for_key(const std::string& text, const char* key) {
    const std::optional<float> value = float_for_key(text, key);
    if (!value) {
        return std::nullopt;
    }
    return static_cast<int>(*value);
}

std::optional<std::uint32_t> uint_for_key(const std::string& text, const char* key) {
    const std::optional<float> value = float_for_key(text, key);
    if (!value || *value < 0.0f) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(*value);
}

std::optional<std::string> bracketed_value_for_key(const std::string& text, const char* key, const char open, const char close) {
    std::optional<std::size_t> begin = value_begin_for_key(text, key);
    if (!begin || *begin >= text.size() || text[*begin] != open) {
        return std::nullopt;
    }

    const std::optional<std::size_t> end = find_matching(text, *begin, open, close);
    if (!end) {
        return std::nullopt;
    }

    return text.substr(*begin, *end - *begin + 1);
}

std::optional<std::string> object_for_key(const std::string& text, const char* key) {
    return bracketed_value_for_key(text, key, '{', '}');
}

std::optional<std::string> array_for_key(const std::string& text, const char* key) {
    return bracketed_value_for_key(text, key, '[', ']');
}

std::optional<glm::vec3> parse_vec3_at(const std::string& text, std::size_t& cursor) {
    skip_whitespace(text, cursor);
    if (cursor >= text.size() || text[cursor] != '[') {
        return std::nullopt;
    }
    ++cursor;

    const std::optional<float> x = parse_float(text, cursor);
    skip_whitespace(text, cursor);
    if (!x || cursor >= text.size() || text[cursor] != ',') {
        return std::nullopt;
    }
    ++cursor;

    const std::optional<float> y = parse_float(text, cursor);
    skip_whitespace(text, cursor);
    if (!y || cursor >= text.size() || text[cursor] != ',') {
        return std::nullopt;
    }
    ++cursor;

    const std::optional<float> z = parse_float(text, cursor);
    skip_whitespace(text, cursor);
    if (!z || cursor >= text.size() || text[cursor] != ']') {
        return std::nullopt;
    }
    ++cursor;

    return glm::vec3(*x, *y, *z);
}

std::optional<glm::vec3> vec3_for_key(const std::string& text, const char* key) {
    std::optional<std::size_t> cursor = value_begin_for_key(text, key);
    if (!cursor) {
        return std::nullopt;
    }
    return parse_vec3_at(text, *cursor);
}

std::optional<std::vector<glm::vec3>> parse_vec3_array(const std::string& text) {
    std::vector<glm::vec3> points;
    if (text.empty() || text.front() != '[') {
        return std::nullopt;
    }

    std::size_t cursor = 1;
    while (cursor < text.size()) {
        skip_whitespace(text, cursor);
        if (cursor >= text.size() || text[cursor] == ']') {
            return points;
        }

        if (text[cursor] != '[') {
            return std::nullopt;
        }

        const std::optional<glm::vec3> point = parse_vec3_at(text, cursor);
        if (!point) {
            return std::nullopt;
        }
        points.push_back(*point);

        skip_whitespace(text, cursor);
        if (cursor >= text.size()) {
            return std::nullopt;
        }
        if (text[cursor] == ',') {
            ++cursor;
            continue;
        }
        if (text[cursor] == ']') {
            return points;
        }
        return std::nullopt;
    }

    return std::nullopt;
}

material_zone_type material_type_from_string(const std::string& value) {
    if (value == "green") {
        return material_zone_type::green;
    }
    if (value == "bunker") {
        return material_zone_type::bunker;
    }
    if (value == "water") {
        return material_zone_type::water;
    }
    return material_zone_type::unknown;
}

std::vector<material_zone> parse_material_zones(const std::string& text) {
    std::vector<material_zone> zones;
    const std::optional<std::string> array = array_for_key(text, "material_zones");
    if (!array) {
        return zones;
    }

    std::size_t cursor = 1;
    while (cursor < array->size()) {
        const std::size_t begin = array->find('{', cursor);
        if (begin == std::string::npos) {
            break;
        }

        const std::optional<std::size_t> end = find_matching(*array, begin, '{', '}');
        if (!end) {
            break;
        }

        const std::string object = array->substr(begin, *end - begin + 1);
        material_zone zone;
        zone.type = material_type_from_string(string_for_key(object, "type").value_or(""));

        const std::optional<glm::vec3> center = vec3_for_key(object, "center");
        const std::optional<float> radius = float_for_key(object, "radius");
        if (center && radius) {
            zone.center = *center;
            zone.radius = *radius;
            zone.has_radius = true;
        }

        const std::optional<std::string> bounds = array_for_key(object, "bounds");
        if (bounds) {
            const std::optional<std::vector<glm::vec3>> corners = parse_vec3_array(*bounds);
            if (corners && corners->size() >= 2) {
                zone.bounds_min = glm::vec3(std::min((*corners)[0].x, (*corners)[1].x),
                                            std::min((*corners)[0].y, (*corners)[1].y),
                                            std::min((*corners)[0].z, (*corners)[1].z));
                zone.bounds_max = glm::vec3(std::max((*corners)[0].x, (*corners)[1].x),
                                            std::max((*corners)[0].y, (*corners)[1].y),
                                            std::max((*corners)[0].z, (*corners)[1].z));
                zone.has_bounds = true;
            }
        }

        zones.push_back(zone);
        cursor = *end + 1;
    }

    return zones;
}

float extent_for_point(const glm::vec3& point, const float padding) {
    return std::max(std::abs(point.x), std::abs(point.z)) + padding;
}
}

std::optional<hole_data> load_hole_from_file(const std::string& path) {
    const std::string text = read_file(path);
    if (text.empty()) {
        return std::nullopt;
    }

    const std::optional<glm::vec3> tee = vec3_for_key(text, "tee");
    const std::optional<glm::vec3> pin = vec3_for_key(text, "pin");
    const std::optional<std::string> spline_object = object_for_key(text, "spline");
    if (!tee || !pin || !spline_object) {
        return std::nullopt;
    }

    const std::optional<std::string> control_points_array = array_for_key(*spline_object, "control_points");
    const std::optional<float> width = float_for_key(*spline_object, "width");
    if (!control_points_array || !width || *width <= 0.0f) {
        return std::nullopt;
    }

    const std::optional<std::vector<glm::vec3>> control_points = parse_vec3_array(*control_points_array);
    if (!control_points || control_points->size() < 2) {
        return std::nullopt;
    }

    hole_data hole;
    hole.id = string_for_key(text, "id").value_or("");
    hole.name = string_for_key(text, "name").value_or(hole.id);
    hole.par = int_for_key(text, "par").value_or(3);
    hole.wind_seed = uint_for_key(text, "wind_seed").value_or(42U);
    hole.tee_position = *tee;
    hole.pin_position = *pin;
    hole.spline.width = *width;
    hole.spline.control_points = *control_points;
    hole.material_zones = parse_material_zones(text);

    return hole;
}

float estimate_course_extent(const hole_data& hole) {
    float extent = std::max(extent_for_point(hole.tee_position, hole.spline.width),
                            extent_for_point(hole.pin_position, hole.spline.width));

    for (const glm::vec3& point : hole.spline.control_points) {
        extent = std::max(extent, extent_for_point(point, hole.spline.width));
    }

    for (const material_zone& zone : hole.material_zones) {
        if (zone.has_radius) {
            extent = std::max(extent, extent_for_point(zone.center, zone.radius));
        }
        if (zone.has_bounds) {
            extent = std::max(extent, extent_for_point(zone.bounds_min, 0.0f));
            extent = std::max(extent, extent_for_point(zone.bounds_max, 0.0f));
        }
    }

    return std::max(20.0f, extent);
}
