#include "game/hole_sign.h"

#include "game/play_area.h"
#include "physics/vector_math.h"

#include <algorithm>
#include <cmath>

namespace {
// The horizontal unit vector from `from` towards `to`, or `fallback`.
glm::vec3 direction_towards(const glm::vec3& from, const glm::vec3& to, const glm::vec3& fallback) {
    return safe_normalize(horizontal(to - from), fallback);
}

// The player's right looking along `forward` (see terrain_spline::bank).
glm::vec3 right_of(const glm::vec3& forward) {
    return glm::vec3(-forward.z, 0.0f, forward.x);
}

}

std::vector<glm::vec3> line_of_play(const hole_data& hole) {
    std::vector<glm::vec3> points{hole.tee_position};
    const std::vector<glm::vec3>& controls = hole.spline.control_points;
    for (std::size_t i = 1; i + 1 < controls.size(); ++i) {
        points.push_back(controls[i]);
    }
    points.push_back(hole.pin_position);
    return points;
}

float polyline_length(const std::vector<glm::vec3>& points) {
    float length = 0.0f;
    for (std::size_t i = 1; i < points.size(); ++i) {
        length += horizontal_distance(points[i - 1], points[i]);
    }
    return length;
}

glm::vec3 point_along(const std::vector<glm::vec3>& points, const float distance) {
    if (points.empty()) {
        return glm::vec3(0.0f);
    }
    float left = std::max(0.0f, distance);
    for (std::size_t i = 1; i < points.size(); ++i) {
        const float segment = horizontal_distance(points[i - 1], points[i]);
        if (segment > 0.0f && left <= segment) {
            return points[i - 1] + (points[i] - points[i - 1]) * (left / segment);
        }
        left -= segment;
    }
    return points.back();
}

glm::vec3 down_hole_at_tee(const hole_data& hole, const float look_ahead) {
    const glm::vec3 towards_pin = direction_towards(hole.tee_position, hole.pin_position, glm::vec3(0.0f, 0.0f, 1.0f));
    return direction_towards(hole.tee_position, point_along(line_of_play(hole), look_ahead), towards_pin);
}

hole_sign place_hole_sign(const play_area& area,
                          const hole_data& hole,
                          const std::size_t area_hole,
                          const hole_sign_tuning& tuning) {
    hole_sign sign;
    sign.area_hole = area_hole;
    sign.line_of_play = line_of_play(hole);
    sign.length = polyline_length(sign.line_of_play);
    sign.half_width = std::max(hole.spline.width, hole.spline.rough_width) * 0.5f;
    sign.zones = hole.material_zones;

    const glm::vec3 tee = hole.tee_position;
    sign.down_hole = down_hole_at_tee(hole, tuning.look_ahead);
    const glm::vec3 right = right_of(sign.down_hole);
    sign.face_normal = -right;

    sign.board_half_width = tuning.board_width * 0.5f;
    sign.board_half_height = tuning.board_height * 0.5f;
    sign.board_thickness = tuning.board_thickness;
    sign.post_radius = tuning.post_radius;
    // Posts stand just inside the board's ends, behind it.
    const float post_reach = std::max(0.0f, sign.board_half_width - sign.post_radius);
    const glm::vec3 behind = -sign.face_normal * (sign.board_thickness * 0.5f + sign.post_radius);

    // On the tee box's tiles, just inside its right edge.
    const float box_edge = area_hole < area.tee_boxes.size() ? area.tee_boxes[area_hole].half_width : 0.0f;
    const glm::vec3 center =
        tee + sign.down_hole * tuning.forward_offset + right * std::max(0.0f, box_edge - tuning.edge_inset);
    sign.post_feet = {center - sign.down_hole * post_reach + behind, center + sign.down_hole * post_reach + behind};

    for (glm::vec3& foot : sign.post_feet) {
        foot = anchor_on_terrain(area, foot);
    }
    const float bottom = std::max(sign.post_feet[0].y, sign.post_feet[1].y) + tuning.board_lift;
    sign.board_center = glm::vec3(center.x, bottom + sign.board_half_height, center.z);
    return sign;
}

std::vector<hole_sign> place_hole_signs(const play_area& area,
                                        const std::vector<hole_data>& holes,
                                        const hole_sign_tuning& tuning) {
    std::vector<hole_sign> signs;
    signs.reserve(holes.size());
    for (std::size_t i = 0; i < holes.size(); ++i) {
        signs.push_back(place_hole_sign(area, holes[i], i, tuning));
    }
    return signs;
}

float hole_sign_top(const hole_sign& sign) {
    return sign.board_center.y + sign.board_half_height;
}

std::vector<tree_body> hole_sign_posts(const std::vector<hole_sign>& signs) {
    std::vector<tree_body> posts;
    posts.reserve(signs.size() * 2);
    for (const hole_sign& sign : signs) {
        for (const glm::vec3& foot : sign.post_feet) {
            tree_shape shape;
            shape.trunk_radius = sign.post_radius;
            shape.trunk_height = hole_sign_top(sign) - foot.y;
            posts.push_back(tree_body{foot, shape});
        }
    }
    return posts;
}
