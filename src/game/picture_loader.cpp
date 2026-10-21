#include "game/picture_loader.h"

#include "game/content_files.h"
#include "game/json_util.h"
#include "physics/vector_math.h"

#include <cstddef>
#include <filesystem>

#include <glm/common.hpp>
#include <glm/vector_relational.hpp>

namespace {
// A field of `T` and its allowed range, both ends included.
template <typename T>
struct float_field {
    const char* key;
    float T::*member;
    float min;
    float max;
};

template <typename T, std::size_t count>
bool read_fields(const json& object, const float_field<T> (&fields)[count], T& out) {
    for (const float_field<T>& field : fields) {
        const std::optional<float> value = json_float(object, field.key);
        if (!value || *value < field.min || *value > field.max) {
            return false;
        }
        out.*field.member = *value;
    }
    return true;
}

constexpr float_field<tape_look> look_fields[] = {
    {"luma_lines", &tape_look::luma_lines, 8.0f, 2000.0f},
    {"chroma_lines", &tape_look::chroma_lines, 4.0f, 2000.0f},
    {"peaking", &tape_look::peaking, 0.0f, 2.0f},
    {"chroma_delay", &tape_look::chroma_delay, 0.0f, 0.1f},
    {"black_lift", &tape_look::black_lift, 0.0f, 0.5f},
    {"highlight_knee", &tape_look::highlight_knee, 0.3f, 1.0f},
    {"saturation", &tape_look::saturation, 0.0f, 2.0f},
    {"grain", &tape_look::grain, 0.0f, 1.0f},
    {"chroma_noise", &tape_look::chroma_noise, 0.0f, 1.0f},
    {"wobble", &tape_look::wobble, 0.0f, 0.05f},
    {"head_switch_skew", &tape_look::head_switch_skew, 0.0f, 0.2f},
    {"head_switch_noise", &tape_look::head_switch_noise, 0.0f, 1.0f},
    {"dropouts_per_second", &tape_look::dropouts_per_second, 0.0f, 60.0f},
};

constexpr float_field<tape_style> tape_fields[] = {
    {"fields_per_second", &tape_style::fields_per_second, 1.0f, 240.0f},
    {"grain_width", &tape_style::grain_width, 0.0f, 0.2f},
    {"chroma_noise_size", &tape_style::chroma_noise_size, 0.001f, 1.0f},
    {"wobble_span", &tape_style::wobble_span, 0.001f, 1.0f},
    {"head_switch_height", &tape_style::head_switch_height, 0.0f, 0.1f},
    {"dropout_length", &tape_style::dropout_length, 0.0f, 0.5f},
    {"tracking_height", &tape_style::tracking_height, 0.001f, 1.0f},
    {"tracking_speed", &tape_style::tracking_speed, 0.0f, 10.0f},
    {"tracking_noise", &tape_style::tracking_noise, 0.0f, 1.0f},
    {"tracking_skew", &tape_style::tracking_skew, 0.0f, 0.2f},
    {"tracking_burst_seconds", &tape_style::tracking_burst_seconds, 0.0f, 10.0f},
    {"tracking_fade_seconds", &tape_style::tracking_fade_seconds, 0.01f, 10.0f},
};

constexpr float_field<tv_style> tv_fields[] = {
    {"horizontal_blur", &tv_style::horizontal_blur, 0.0f, 2.0f},
    {"scanline_depth", &tv_style::scanline_depth, 0.0f, 1.0f},
    {"scanline_gap", &tv_style::scanline_gap, 0.0f, 0.9f},
    {"mask", &tv_style::mask, 0.0f, 0.5f},
    {"curvature", &tv_style::curvature, 0.0f, 0.2f},
    {"corner_radius", &tv_style::corner_radius, 0.0f, 0.5f},
    {"halation", &tv_style::halation, 0.0f, 2.0f},
    {"halation_threshold", &tv_style::halation_threshold, 0.0f, 0.99f},
    {"halation_radius", &tv_style::halation_radius, 0.0f, 0.1f},
    {"vignette", &tv_style::vignette, 0.0f, 1.0f},
};

constexpr float_field<camcorder_style> camcorder_fields[] = {
    {"middle_grey", &camcorder_style::middle_grey, 0.01f, 1.0f},
    {"iris_min_stops", &camcorder_style::iris_min_stops, -4.0f, 0.0f},
    {"iris_max_stops", &camcorder_style::iris_max_stops, 0.0f, 4.0f},
    {"iris_seconds", &camcorder_style::iris_seconds, 0.01f, 10.0f},
    {"white_balance_correction", &camcorder_style::white_balance_correction, 0.0f, 1.0f},
    {"white_balance_seconds", &camcorder_style::white_balance_seconds, 0.01f, 30.0f},
    {"shoulder", &camcorder_style::shoulder, 0.3f, 1.0f},
    {"toe", &camcorder_style::toe, 0.0f, 0.5f},
    {"bloom_threshold", &camcorder_style::bloom_threshold, 0.0f, 16.0f},
    {"bloom_strength", &camcorder_style::bloom_strength, 0.0f, 4.0f},
    {"bloom_width", &camcorder_style::bloom_width, 0.0f, 0.2f},
    {"bloom_height", &camcorder_style::bloom_height, 0.0f, 0.2f},
    {"smear_threshold", &camcorder_style::smear_threshold, 0.0f, 64.0f},
    {"smear_strength", &camcorder_style::smear_strength, 0.0f, 4.0f},
    {"smear_fade", &camcorder_style::smear_fade, 0.0f, 4.0f},
    {"gain_grain", &camcorder_style::gain_grain, 0.0f, 1.0f},
    {"gain_chroma", &camcorder_style::gain_chroma, 0.0f, 1.0f},
};

constexpr int max_scanline_full_factor = 16;

bool unit_color(const std::optional<glm::vec3>& color) {
    return color && !glm::any(glm::lessThan(*color, glm::vec3(0.0f))) && !glm::any(glm::greaterThan(*color, glm::vec3(1.0f)));
}

std::optional<camcorder_style> parse_camcorder(const json& object) {
    camcorder_style camcorder;
    const std::optional<glm::vec3> tint = json_vec3(object, "shadow_tint");
    if (!read_fields(object, camcorder_fields, camcorder) || !unit_color(tint)) {
        return std::nullopt;
    }
    camcorder.shadow_tint = *tint;
    return camcorder;
}

std::optional<tape_style> parse_tape(const json& object) {
    tape_style tape;
    const json* fresh = json_object(object, "fresh");
    const json* worn = json_object(object, "worn");
    if (!read_fields(object, tape_fields, tape) || fresh == nullptr || worn == nullptr ||
        !read_fields(*fresh, look_fields, tape.fresh) || !read_fields(*worn, look_fields, tape.worn)) {
        return std::nullopt;
    }
    return tape;
}

std::optional<tv_style> parse_tv(const json& object) {
    tv_style tv;
    const std::optional<int> full_factor = json_int(object, "scanline_full_factor");
    const std::optional<glm::vec3> border = json_vec3(object, "border_color");
    if (!read_fields(object, tv_fields, tv) || !full_factor || *full_factor < 2 ||
        *full_factor > max_scanline_full_factor || !unit_color(border)) {
        return std::nullopt;
    }
    tv.scanline_full_factor = *full_factor;
    tv.border_color = *border;
    return tv;
}
}

std::optional<picture_style> parse_picture_from_text(const std::string& text) {
    const std::optional<json> root = parse_json(text);
    if (!root) {
        return std::nullopt;
    }
    const std::optional<int> target_lines = json_int(*root, "target_lines");
    const json* camcorder_object = json_object(*root, "camcorder");
    const json* tape_object = json_object(*root, "tape");
    const json* tv_object = json_object(*root, "tv");
    const std::optional<camcorder_style> camcorder =
        camcorder_object != nullptr ? parse_camcorder(*camcorder_object) : std::nullopt;
    const std::optional<tape_style> tape = tape_object != nullptr ? parse_tape(*tape_object) : std::nullopt;
    const std::optional<tv_style> tv = tv_object != nullptr ? parse_tv(*tv_object) : std::nullopt;
    if (!target_lines || *target_lines <= 0 || !camcorder || !tape || !tv) {
        return std::nullopt;
    }
    return picture_style{*target_lines, *camcorder, *tape, *tv};
}

std::optional<picture_style> load_picture_style(const std::string& asset_root) {
    const std::optional<std::string> text = read_text_file(std::filesystem::path(asset_root) / picture_style_path);
    return text ? parse_picture_from_text(*text) : std::nullopt;
}

tape_look tape_look_at(const tape_style& tape, const float wear) {
    const float t = clamp01(wear);
    tape_look look;
    for (const float_field<tape_look>& field : look_fields) {
        look.*field.member = glm::mix(tape.fresh.*field.member, tape.worn.*field.member, t);
    }
    look.luma_lines = 1.0f / glm::mix(1.0f / tape.fresh.luma_lines, 1.0f / tape.worn.luma_lines, t);
    look.chroma_lines = 1.0f / glm::mix(1.0f / tape.fresh.chroma_lines, 1.0f / tape.worn.chroma_lines, t);
    return look;
}
