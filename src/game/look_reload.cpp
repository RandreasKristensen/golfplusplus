#include "game/look_reload.h"

#include "game/content_files.h"
#include "game/json_util.h"

#include <utility>

namespace {
// The look's texts on disk, or which file cannot be read.
struct disk_texts {
    std::optional<look_texts> texts;
    std::optional<look_error> error;
};

disk_texts read_look_texts(const look_watch& watch) {
    std::optional<std::string> lighting = read_text_file(watch.lighting_file);
    std::optional<std::string> picture = read_text_file(watch.picture_file);
    if (!lighting) {
        return {std::nullopt, look_error{lighting_path, look_problem::unreadable}};
    }
    if (!picture) {
        return {std::nullopt, look_error{picture_style_path, look_problem::unreadable}};
    }
    return {look_texts{std::move(*lighting), std::move(*picture)}, std::nullopt};
}

// Whether member `key` of two JSON objects differs (a missing one counts).
bool section_differs(const json& from, const json& to, const char* key) {
    const auto member = [key](const json& root) -> const json* {
        const auto it = root.is_object() ? root.find(key) : root.end();
        return root.is_object() && it != root.end() ? &*it : nullptr;
    };
    const json* a = member(from);
    const json* b = member(to);
    return a == nullptr || b == nullptr ? a != b : *a != *b;
}
}

look_load parse_look(const look_texts& texts) {
    look_load load;
    const auto fail = [&load](const char* file, const std::string& text) {
        load.error = look_error{file, parse_json(text) ? look_problem::not_valid : look_problem::not_json};
        return load;
    };
    std::optional<lighting_palette> lighting = parse_lighting_from_text(texts.lighting);
    if (!lighting) {
        return fail(lighting_path, texts.lighting);
    }
    std::optional<picture_style> picture = parse_picture_from_text(texts.picture);
    if (!picture) {
        return fail(picture_style_path, texts.picture);
    }
    load.loaded = look{std::move(*lighting), std::move(*picture)};
    return load;
}

look_changes look_changes_between(const look_texts& from, const look_texts& to) {
    const json before = parse_json(from.lighting).value_or(json());
    const json after = parse_json(to.lighting).value_or(json());
    look_changes changes;
    changes.sky = section_differs(before, after, "sky");
    changes.shadows = section_differs(before, after, "shadows");
    changes.shafts = section_differs(before, after, "shafts");
    changes.streaks = section_differs(before, after, "rain") || section_differs(before, after, "wind");
    changes.ground = section_differs(before, after, "ground");
    changes.picture = from.picture != to.picture;
    return changes;
}

look_start start_look(const std::string& asset_root) {
    look_start start;
    look_watch& watch = start.watch;
    watch.lighting_file = std::filesystem::path(asset_root) / lighting_path;
    watch.picture_file = std::filesystem::path(asset_root) / picture_style_path;
    watch.lighting_time = file_write_time(watch.lighting_file);
    watch.picture_time = file_write_time(watch.picture_file);
    disk_texts disk = read_look_texts(watch);
    if (!disk.texts) {
        watch.error = disk.error;
        return start;
    }
    look_load load = parse_look(*disk.texts);
    watch.error = load.error;
    if (load.loaded) {
        watch.shown = std::move(*disk.texts);
        start.loaded = std::move(load.loaded);
    }
    return start;
}

std::optional<look_reload> take_look_texts(look_watch& watch, const look_texts& texts) {
    if (texts.lighting == watch.shown.lighting && texts.picture == watch.shown.picture) {
        watch.error.reset();
        return std::nullopt;
    }
    look_load load = parse_look(texts);
    if (!load.loaded) {
        watch.error = load.error;
        return std::nullopt;
    }
    look_reload reload{std::move(*load.loaded), look_changes_between(watch.shown, texts)};
    watch.shown = texts;
    watch.error.reset();
    return reload;
}

std::optional<look_reload> reload_look(look_watch& watch) {
    const std::optional<std::filesystem::file_time_type> lighting_time = file_write_time(watch.lighting_file);
    const std::optional<std::filesystem::file_time_type> picture_time = file_write_time(watch.picture_file);
    disk_texts disk = read_look_texts(watch);
    if (!disk.texts) {
        watch.error = disk.error;
        return std::nullopt;
    }
    watch.lighting_time = lighting_time;
    watch.picture_time = picture_time;
    return take_look_texts(watch, *disk.texts);
}

std::optional<look_reload> poll_look(look_watch& watch, const float dt) {
    watch.since_poll += dt;
    if (watch.since_poll < look_poll_seconds) {
        return std::nullopt;
    }
    watch.since_poll = 0.0f;
    const std::optional<std::filesystem::file_time_type> lighting_time = file_write_time(watch.lighting_file);
    const std::optional<std::filesystem::file_time_type> picture_time = file_write_time(watch.picture_file);
    if (lighting_time && picture_time && lighting_time == watch.lighting_time && picture_time == watch.picture_time) {
        return std::nullopt;
    }
    return reload_look(watch);
}
