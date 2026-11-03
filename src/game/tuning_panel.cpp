#include "game/tuning_panel.h"

#include "game/content_files.h"
#include "game/json_util.h"
#include "game/lighting_numbers.h"

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace {
constexpr int max_places = 6;

// The places `step` needs (0.005: 3).
int places_of(const double step) {
    double scaled = step;
    for (int places = 0; places < max_places; ++places) {
        if (std::abs(scaled - std::round(scaled)) < 1e-6) {
            return places;
        }
        scaled *= 10.0;
    }
    return max_places;
}
}

std::optional<std::vector<tuning_entry>> parse_tuning_panel_from_text(const std::string& text,
                                                                      const lighting_palette& palette) {
    const std::optional<json> root = parse_json(text);
    const json* list = root ? json_array(*root, "entries") : nullptr;
    if (list == nullptr || list->empty()) {
        return std::nullopt;
    }
    std::vector<tuning_entry> entries;
    for (const json& value : *list) {
        const std::optional<std::string> label = json_string(value, "label");
        const std::optional<std::string> path_text = json_string(value, "path");
        const std::optional<float> step = json_float(value, "step");
        const std::optional<json_path> path = path_text ? parse_json_path(*path_text) : std::nullopt;
        if (!label || label->empty() || !path || !step || !(*step > 0.0f)) {
            return std::nullopt;
        }
        tuning_entry entry{*label, *path, static_cast<double>(*step)};
        if (!lighting_number_at(palette, tuning_entry_path(entry, 0))) {
            return std::nullopt;
        }
        entries.push_back(std::move(entry));
    }
    return entries;
}

std::optional<std::vector<tuning_entry>> load_tuning_panel(const std::string& asset_root, const lighting_palette& palette) {
    const std::optional<std::string> text = read_text_file(std::filesystem::path(asset_root) / tuning_panel_path);
    return text ? parse_tuning_panel_from_text(*text, palette) : std::nullopt;
}

json_path tuning_entry_path(const tuning_entry& entry, const std::size_t key) {
    json_path path = entry.path;
    for (std::string& segment : path) {
        if (segment == sun_key_segment) {
            segment = std::to_string(key);
        }
    }
    return path;
}

double tuning_step(const tuning_entry& entry, const bool whole, const bool fine) {
    if (whole) {
        return std::max(1.0, std::round(entry.step));
    }
    return fine ? entry.step * fine_step_share : entry.step;
}

int tuning_decimals(const tuning_entry& entry, const bool whole) {
    return whole ? 0 : places_of(tuning_step(entry, false, true));
}

std::optional<std::string> step_lighting_text(const std::string& text, const tuning_entry& entry, const json_path& path,
                                              const int direction, const bool fine) {
    const std::optional<lighting_palette> palette = parse_lighting_from_text(text);
    if (!palette) {
        return std::nullopt;
    }
    const std::optional<lighting_number> number = lighting_number_at(*palette, path);
    const std::optional<text_span> span = json_value_span(text, path);
    if (!number || !span) {
        return std::nullopt;
    }
    const std::string written = text.substr(span->begin, span->end - span->begin);
    const std::optional<json> current = parse_json(written);
    if (!current || !current->is_number()) {
        return std::nullopt;
    }
    const double step = tuning_step(entry, number->whole, fine);
    const double now = current->get<double>();
    const double low = static_cast<double>(number->low);
    const double high = static_cast<double>(number->high);
    const double moved = std::clamp(now + static_cast<double>(direction) * step, low, high);
    // At an end of the range, as many places as that end has (0.95).
    const int end_places = moved == low || moved == high ? places_of(moved) : 0;
    const int places = number->whole ? 0 : std::max({places_of(step), json_number_decimals(written), end_places});
    const bool point = written.find('.') != std::string::npos;
    const std::string value = json_number_text(moved, places, point);
    if (value == json_number_text(now, places, point)) {
        return std::nullopt;
    }
    std::optional<std::string> stepped = replace_json_value(text, path, value);
    if (!stepped || !parse_lighting_from_text(*stepped)) {
        return std::nullopt;
    }
    return stepped;
}
