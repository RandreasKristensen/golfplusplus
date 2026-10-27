#include "game/area_solids.h"

#include "game/play_area.h"
#include "physics/vector_math.h"

#include <algorithm>

#include <glm/geometric.hpp>

namespace {
solid upright(const solid_kind kind, const glm::vec3& base, const float radius, const float height) {
    solid thing;
    thing.kind = kind;
    thing.base = base;
    thing.radius = radius;
    thing.height = height;
    return thing;
}

// Stops the ball, people and carts, bouncing the ball by `contact`.
solid stopping_all(solid thing, const solid_contact_tuning& contact) {
    thing.stops_ball = true;
    thing.stops_walker = true;
    thing.stops_cart = true;
    thing.restitution = contact.restitution;
    thing.friction = contact.friction;
    return thing;
}

// People and carts bump into it; the ball passes.
solid stopping_bodies(solid thing) {
    thing.stops_walker = true;
    thing.stops_cart = true;
    return thing;
}

// A flag taken out lies this far off the cup's edge.
constexpr float flag_clearance_meters = 0.15f;

solid wall(const solid_kind kind, const glm::vec3& a, const glm::vec3& b, const float half_thickness, const float height) {
    solid thing = upright(kind, a, half_thickness, height);
    thing.shape = solid_shape::wall;
    thing.end = b;
    return thing;
}
}

std::vector<solid> standing_solids(const play_area& area, const game_tuning& tuning) {
    const solids_tuning& contacts = tuning.solids;
    std::vector<solid> solids;
    // Each tree's trunk, then its leaves, which only the ball meets.
    for (const tree_body& tree : standing_trees(area)) {
        solids.push_back(stopping_all(upright(solid_kind::trunk, tree.base, tree.shape.trunk_radius, tree.shape.trunk_height),
                                      contacts.trunk));
        if (tree.shape.leaf_height > 0.0f) {
            solid leaves = upright(solid_kind::leaves,
                                   glm::vec3(tree.base.x, tree.base.y + std::max(0.0f, tree.shape.trunk_height), tree.base.z),
                                   tree.shape.leaf_radius, tree.shape.leaf_height);
            leaves.shape = solid_shape::cone;
            leaves.porous = true;
            solids.push_back(leaves);
        }
    }
    for (const hole_sign& sign : area.signs) {
        for (const glm::vec3& foot : sign.post_feet) {
            solids.push_back(stopping_all(upright(solid_kind::pole, foot, sign.post_radius, hole_sign_top(sign) - foot.y),
                                          contacts.pole));
        }
    }
    for (const course_world_fence& fence : area.fences) {
        for (const glm::vec3& pole : fence.poles) {
            solids.push_back(stopping_all(upright(solid_kind::pole, pole, tuning.fence.pole_radius, fence.height), contacts.pole));
        }
    }
    const floodlight_tuning& lights = tuning.floodlights;
    for (const floodlight& light : area.floodlights) {
        solids.push_back(stopping_all(upright(solid_kind::pole, light.foot, light.radius, light.lamp.y - light.foot.y),
                                      contacts.pole));
        // Its lamp head, high over everyone's heads: the ball's only.
        solid head = upright(solid_kind::lamp, light.lamp - glm::vec3(0.0f, 0.5f * lights.head_height, 0.0f), 0.0f,
                             lights.head_height);
        head.shape = solid_shape::box;
        head.along = safe_normalize(horizontal(light.aim), glm::vec3(0.0f, 0.0f, 1.0f));
        head.half_length = 0.5f * lights.head_depth;
        head.half_width = 0.5f * lights.head_width;
        head.stops_ball = true;
        head.restitution = contacts.lamp.restitution;
        head.friction = contacts.lamp.friction;
        solids.push_back(head);
    }
    for (const hole_sign& sign : area.signs) {
        const glm::vec3 half = sign.down_hole * sign.board_half_width;
        const glm::vec3 low(0.0f, sign.board_half_height, 0.0f);
        solids.push_back(stopping_all(wall(solid_kind::board, sign.board_center - half - low, sign.board_center + half - low,
                                           0.5f * sign.board_thickness, 2.0f * sign.board_half_height),
                                      contacts.board));
    }
    for (const course_world_fence& fence : area.fences) {
        for (std::size_t i = 0; i + 1 < fence.poles.size(); ++i) {
            solids.push_back(stopping_all(wall(solid_kind::net, fence.poles[i], fence.poles[i + 1], tuning.fence.pole_radius,
                                               fence.height),
                                          contacts.net));
        }
    }
    const std::vector<solid> boxes = tee_box_solids(area.tee_boxes, tuning);
    solids.insert(solids.end(), boxes.begin(), boxes.end());
    return solids;
}

std::vector<solid> flagstick_solids(const std::vector<cup>& cups, const std::vector<bool>& flags_in,
                                    const game_tuning& tuning) {
    const world_scale_tuning& scale = tuning.scale;
    const float unit = scale.meters_per_world_unit;
    const float thickness = tuning.cup.flagstick_radius_meters / unit;
    const float length = scale.pin_visual_height_meters / unit;
    std::vector<solid> solids;
    for (std::size_t i = 0; i < cups.size(); ++i) {
        const cup& hole = cups[i];
        const bool in = i >= flags_in.size() || flags_in[i];
        if (in) {
            const glm::vec3 foot = hole.centre - glm::vec3(0.0f, hole.depth, 0.0f);
            solid stick = upright(solid_kind::flagstick, foot, thickness, hole.depth + length);
            stick.stops_ball = true;
            stick.restitution = tuning.solids.flagstick.restitution;
            stick.friction = tuning.solids.flagstick.friction;
            solids.push_back(stick);
            solids.push_back(stopping_bodies(upright(solid_kind::flagstick, hole.centre, scale.flagstick_radius_meters / unit,
                                                     length)));
        } else {
            // Laid down on the green beside the cup, pointing away from it.
            const glm::vec3 away(1.0f, 0.0f, 0.0f);
            const glm::vec3 near_end = hole.centre + away * (hole.radius + thickness + flag_clearance_meters / unit);
            solid stick = wall(solid_kind::flagstick, near_end, near_end + away * length, thickness, 2.0f * thickness);
            stick.stops_ball = true;
            stick.restitution = tuning.solids.flagstick.restitution;
            stick.friction = tuning.solids.flagstick.friction;
            solids.push_back(stick);
        }
    }
    return solids;
}

std::vector<solid> tee_box_solids(const std::vector<tee_box>& boxes, const game_tuning& tuning) {
    std::vector<solid> solids;
    for (const tee_box& box : boxes) {
        solid block = stopping_all(upright(solid_kind::tee_box, glm::vec3(box.center.x, box.bottom, box.center.z), 0.0f,
                                           box.center.y - box.bottom),
                                   tuning.solids.tee_box);
        block.shape = solid_shape::box;
        block.along = safe_normalize(horizontal(box.down_hole), glm::vec3(0.0f, 0.0f, 1.0f));
        block.half_length = box.half_length;
        block.half_width = box.half_width;
        block.walkable = true;
        solids.push_back(block);
    }
    return solids;
}
