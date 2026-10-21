#include "game/area_obstacles.h"

#include "game/play_area.h"

#include <glm/geometric.hpp>

namespace {
glm::vec2 flat(const glm::vec3& point) {
    return glm::vec2(point.x, point.z);
}

void add_posts(upright_obstacles& obstacles, const std::vector<tree_body>& bodies) {
    for (const tree_body& body : bodies) {
        obstacles.posts.push_back(upright_post{flat(body.base), body.shape.trunk_radius});
    }
}
}

upright_obstacles standing_obstacles(const play_area& area, const std::vector<hole_data>& holes,
                                     const game_tuning& tuning) {
    upright_obstacles obstacles;
    for (const tree_instance& tree : area.trees) {
        obstacles.posts.push_back(upright_post{flat(tree.position), tree.shape.trunk_radius});
    }
    add_posts(obstacles, area.sign_posts);
    add_posts(obstacles, area.fence_poles);
    add_posts(obstacles, area.floodlight_poles);
    const float flagstick_radius = tuning.scale.flagstick_radius_meters / tuning.scale.meters_per_world_unit;
    for (const hole_data& hole : holes) {
        obstacles.posts.push_back(upright_post{flat(hole.pin_position), flagstick_radius});
    }
    for (const hole_sign& sign : area.signs) {
        const glm::vec2 half = flat(sign.down_hole) * sign.board_half_width;
        obstacles.walls.push_back(
            upright_wall{flat(sign.board_center) - half, flat(sign.board_center) + half, 0.5f * sign.board_thickness});
    }
    for (const fence_panel& net : area.fence_panels) {
        obstacles.walls.push_back(upright_wall{flat(net.a), flat(net.b), tuning.fence.pole_radius});
    }
    return obstacles;
}

upright_obstacles cart_only_obstacles(const play_area& area) {
    upright_obstacles obstacles;
    for (const tee_box& box : area.tee_boxes) {
        obstacles.blocks.push_back(upright_block{flat(box.center), glm::normalize(flat(box.down_hole)), box.half_length, box.half_width});
    }
    return obstacles;
}
