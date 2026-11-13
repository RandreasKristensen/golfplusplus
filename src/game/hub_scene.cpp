#include "game/hub_scene.h"
#include "physics/vector_math.h"

// Same yaw-only frame as the renderer: +X is left, +Z forward.
glm::vec3 prop_point(const static_prop& placed, const glm::vec3& local) {
    const auto forward = yaw_direction(placed.heading);
    return placed.position + yaw_left(forward) * local.x + world_up * local.y + forward * local.z;
}

std::optional<ground_pad> bus_ground_pad(const course_world_definition& world, const prop_catalog& props) {
    if (!world.bus_stop || props.empty()) { return std::nullopt; }
    const auto& bus = props.at("hub_bus");
    const auto low = bus.point("body_min"), high = bus.point("body_max");
    const static_prop placed{"hub_bus", world.bus_stop->position, world.bus_stop->heading};
    return ground_pad{prop_point(placed, horizontal((low + high) * .5f)), yaw_direction(placed.heading),
        (high.x-low.x)*.5f, (high.z-low.z)*.5f};
}

void place_hub_scene(play_area& area, const course_world_definition& world, const prop_catalog& props, const game_tuning& tuning) {
    if (props.empty()) { return; }
    const auto place = [&](const static_prop& placed) {
        const auto& model = props.at(placed.model);
        area.props.push_back(placed);
        const auto low = model.point("body_min"), high = model.point("body_max");
        solid body;
        body.shape = solid_shape::box; body.kind = solid_kind::board;
        body.base = prop_point(placed, (low + high) * .5f); body.base.y = placed.position.y + low.y;
        body.height = high.y - low.y; body.along = yaw_direction(placed.heading);
        body.half_length = (high.z - low.z) * .5f; body.half_width = (high.x - low.x) * .5f;
        body.stops_ball = body.stops_walker = body.stops_cart = true;
        body.restitution = tuning.solids.board.restitution; body.friction = tuning.solids.board.friction;
        area.solids.push_back(body);
    };
    if (world.hub) {
        place({"tee_book", anchor_on_terrain(area, world.hub->book.position) - props.at("tee_book").point("approach"), 0});
    }
    if (world.bus_stop) {
        const static_prop parked{"hub_bus", anchor_on_terrain(area, world.bus_stop->position), world.bus_stop->heading};
        place(parked);
        area.bus = hub_anchor{prop_point(parked, props.at("hub_bus").point("approach")),
            tuning.server.bus_reach_meters / tuning.scale.meters_per_world_unit};
    }
    area.solids_grid = build_solid_grid(area.solids, solid_grid_cell_size);
}

glm::vec3 warmup_position(const play_area& area, const std::size_t hole, const game_tuning& tuning) {
    const glm::vec3 pin = area.cups[hole].centre;
    // The warmup station is a short putt, on the near side of this green.
    const glm::vec3 tee = area.holes[hole].vertices.front().position;
    return anchor_on_terrain(area, pin + safe_normalize(horizontal(tee - pin), glm::vec3(0,0,-1)) *
        (tuning.server.warmup_distance_meters / tuning.scale.meters_per_world_unit));
}
