#pragma once

// The game content the server plays with, parsed from content files keyed by
// their path relative to assets/ (the module's embedded_content.h). Plain
// C++ with no SpacetimeDB types, so the native tests check it against what
// the game builds from the same files.

#include "game/club_definition.h"
#include "game/course_definition.h"
#include "game/course_world_definition.h"
#include "game/game_tuning.h"
#include "game/hole_data.h"
#include "game/pixel_font_data.h"
#include "game/play_area.h"
#include "game/reward_rules.h"
#include "game/shot_simulation.h"
#include "physics/tree_collision.h"

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Relative path ("holes/x.json") -> file text.
using content_files = std::map<std::string, std::string_view>;

// Everything but the courses' terrain, which build_server_course makes on
// demand.
struct server_content {
    game_tuning tuning;
    std::vector<club_definition> clubs;
    reward_rules rewards;
    pixel_font_data font;
    std::vector<course_definition> courses;  // only courses with a world: rooms are hubs
};

struct server_content_load_result {
    std::optional<server_content> content;
    std::string error;  // what is missing, when content is nullopt
};

server_content_load_result parse_server_content(const content_files& files);

// One course as the server plays it, built the way the game builds it
// (game/course_session.h), so a shot lands in the same place on both.
struct server_course {
    course_definition course;
    course_world_definition world;
    std::vector<hole_data> holes;  // placed where the world puts them
    play_area area;
    std::vector<tree_body> trees;
    std::vector<shot_hole> shot_holes;  // one per hole
};

const course_definition* find_server_course(const server_content& content, const std::string& course_id);

// nullopt when the course is unknown or a hole or its world fails to load.
std::optional<server_course> build_server_course(const content_files& files,
                                                 const server_content& content,
                                                 const std::string& course_id);
