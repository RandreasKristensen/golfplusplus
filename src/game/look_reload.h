#pragma once

// The look, lighting.json and picture.json, reloaded while the game runs:
// their write times are polled every look_poll_seconds, and when either
// file's text differs from what the look shown was loaded from, both are
// loaded again. A file that fails to load keeps the last good look and says
// why until a good one is saved. What changed is reported by section, so
// only what a section feeds is rebuilt (renderer::set_look). GL-free.

#include "game/lighting_loader.h"
#include "game/picture_loader.h"

#include <filesystem>
#include <optional>
#include <string>

inline constexpr float look_poll_seconds = 0.5f;

struct look {
    lighting_palette lighting;
    picture_style picture;
};

// The files' texts, byte for byte.
struct look_texts {
    std::string lighting;
    std::string picture;
};

enum class look_problem { unreadable, not_json, not_valid };

struct look_error {
    const char* file = lighting_path;  // lighting_path or picture_style_path
    look_problem problem = look_problem::unreadable;
};

struct look_load {
    std::optional<look> loaded;
    std::optional<look_error> error;  // the first file that fails
};

look_load parse_look(const look_texts& texts);

// Which sections differ between two good looks' texts.
struct look_changes {
    bool sky = false;      // the backdrop's settings
    bool shadows = false;  // the shadow map and the blob shadows
    bool shafts = false;   // the shafts' programs and the lamps' maps
    bool streaks = false;  // rain or wind: how many streaks are drawn
    bool ground = false;   // ground colours, baked into the course's meshes
    bool picture = false;  // anything in picture.json
};

look_changes look_changes_between(const look_texts& from, const look_texts& to);

struct look_watch {
    std::filesystem::path lighting_file;
    std::filesystem::path picture_file;
    std::optional<std::filesystem::file_time_type> lighting_time;
    std::optional<std::filesystem::file_time_type> picture_time;
    look_texts shown;                 // what the look shown was loaded from
    std::optional<look_error> error;  // why the files on disk are not shown
    float since_poll = 0.0f;
};

struct look_reload {
    look next;
    look_changes changes;
};

struct look_start {
    std::optional<look> loaded;
    look_watch watch;  // its error says why `loaded` is empty
};

// The look from `asset_root`'s files, watched from now.
look_start start_look(const std::string& asset_root);

// The look from `texts` (as the files hold them now) when it differs from
// the one shown and loads; clears the error when it is the one shown, or
// sets it when it fails, keeping the look shown.
std::optional<look_reload> take_look_texts(look_watch& watch, const look_texts& texts);

// Both files read now, whatever their write times (after writing one),
// through take_look_texts. A file that cannot be read sets the error.
std::optional<look_reload> reload_look(look_watch& watch);

// `dt` seconds on: when a poll finds either file's write time moved,
// reload_look. A file that cannot be read is tried again on the next poll.
std::optional<look_reload> poll_look(look_watch& watch, float dt);
