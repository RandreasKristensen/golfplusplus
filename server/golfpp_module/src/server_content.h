#pragma once

// The game content the server plays with, parsed from content files keyed by
// their path relative to assets/ (the module's embedded_content.h). Plain
// C++ with no SpacetimeDB types, so the native tests check it against what
// the game builds from the same files.

#include "game/club_definition.h"
#include "game/hub_definition.h"
#include "game/prop_loader.h"
#include "game/content_hash.h"
#include "game/course_definition.h"
#include "game/course_world_definition.h"
#include "game/game_tuning.h"
#include "game/hole_data.h"
#include "game/item_catalog.h"
#include "game/loadout.h"
#include "game/pixel_font_data.h"
#include "game/play_area.h"
#include "game/reward_rules.h"
#include "game/shot_simulation.h"
#include "game/skill_catalog.h"
#include "physics/tree_body.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Everything but the courses' terrain, which build_server_course makes on
// demand.
struct server_content {
    std::uint64_t content_hash = 0;
    game_tuning tuning;
    prop_catalog hub_props;
    std::vector<club_definition> clubs;
    reward_rules rewards;
    skill_catalog skills;
    item_catalog items;
    loadout default_loadout;  // fits `items`
    pixel_font_data font;
    std::optional<hub_definition> hub;
    std::vector<course_definition> courses;  // only courses with a world: a room is one
};

struct server_content_load_result {
    std::optional<server_content> content;
    std::string error;  // what is missing, when content is nullopt
};

server_content_load_result parse_server_content(const content_files& files);

// One course as the server plays it, built the way the game builds it
// (game/course_session.h), so a shot lands in the same place on both.
struct server_course {
    place_kind kind = place_kind::course;
    place_definition course;
    std::optional<hub_definition> hub;
    course_world_definition world;
    std::vector<hole_data> holes;  // placed where the world puts them
    play_area area;
};

// Hole `hole` of `course` as a shot hit at `shot_at` plays on it: in the
// course's weather then (game/weather.h), each green's flag in or out by
// `flags_out` (hole indices), as every client replays it.
shot_course server_shot_course(const server_course& course, std::size_t hole, std::int64_t shot_at,
                               const std::vector<int>& flags_out, const server_content& content);

const course_definition* find_server_course(const server_content& content, const std::string& course_id);

// nullopt when the course is unknown or a hole or its world fails to load.
std::optional<server_course> build_server_course(const content_files& files,
                                                 const server_content& content,
                                                 const std::string& course_id);
