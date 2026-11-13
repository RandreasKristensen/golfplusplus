#pragma once
// Static hub models and their shared approach anchors. Model points define the
// body box and approach; collision is appended to the area's sole solids list.
#include "game/play_area.h"
#include "game/prop_loader.h"
#include <cstddef>

// Bus pads enter build_ground alongside tee pads, before solids are placed.
std::optional<ground_pad> bus_ground_pad(const course_world_definition& world, const prop_catalog& props);
glm::vec3 prop_point(const static_prop& placed, const glm::vec3& local);

void place_hub_scene(play_area& area, const course_world_definition& world, const prop_catalog& props, const game_tuning& tuning);
glm::vec3 warmup_position(const play_area& area, std::size_t hole, const game_tuning& tuning);
