#include "game/sight_line.h"

#include "physics/vector_math.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <glm/geometric.hpp>

namespace {
constexpr float tiny = 0.000001f;

// The nearer of `best` and a hit at `t` ahead of the eye.
void keep_nearest(std::optional<float>& best, const std::optional<float> t) {
    if (t && *t > 0.0f && (!best || *t < *best)) {
        best = t;
    }
}

// How far `point` stands from the line of sight's first `length` seen from above.
float flat_distance_to_line(const glm::vec3& point, const glm::vec3& eye, const glm::vec3& direction, const float length) {
    const glm::vec3 flat_direction = horizontal(direction);
    const float flat_length = glm::length(flat_direction);
    const glm::vec3 to_point = horizontal(point - eye);
    if (flat_length < tiny) {
        return glm::length(to_point);
    }
    const glm::vec3 unit = flat_direction / flat_length;
    const float along = std::clamp(glm::dot(to_point, unit), 0.0f, length * flat_length);
    return glm::length(to_point - unit * along);
}

// A tree's cone of leaves: its foot `bottom` high, `radius` across there,
// narrowing to its tip `height` above.
std::optional<float> cone_hit(const glm::vec3& axis, const float bottom, const float radius, const float height,
                              const glm::vec3& eye, const glm::vec3& direction) {
    if (radius <= 0.0f || height <= 0.0f) {
        return std::nullopt;
    }
    const float top = bottom + height;
    const float k2 = (radius / height) * (radius / height);
    const glm::vec3 q(eye.x - axis.x, eye.y - top, eye.z - axis.z);
    const float a = direction.x * direction.x + direction.z * direction.z - k2 * direction.y * direction.y;
    const float b = 2.0f * (q.x * direction.x + q.z * direction.z - k2 * q.y * direction.y);
    const float c = q.x * q.x + q.z * q.z - k2 * q.y * q.y;
    std::optional<float> best;
    const auto on_side = [&](const float t) {
        const float y = eye.y + direction.y * t;
        if (y >= bottom && y <= top) {
            keep_nearest(best, t);
        }
    };
    if (std::abs(a) < tiny) {
        if (std::abs(b) > tiny) {
            on_side(-c / b);
        }
    } else {
        const float disc = b * b - 4.0f * a * c;
        if (disc >= 0.0f) {
            const float root = std::sqrt(disc);
            on_side((-b - root) / (2.0f * a));
            on_side((-b + root) / (2.0f * a));
        }
    }
    if (std::abs(direction.y) > tiny) {
        const float t = (bottom - eye.y) / direction.y;
        const glm::vec3 at = eye + direction * t;
        if (glm::length(horizontal(at - axis)) <= radius) {
            keep_nearest(best, t);
        }
    }
    return best;
}

std::optional<float> tree_hit(const tree_body& tree, const glm::vec3& eye, const glm::vec3& direction) {
    std::optional<float> best = post_hit(sight_post{tree.base, tree.shape.trunk_radius, tree.shape.trunk_height}, eye, direction);
    keep_nearest(best, cone_hit(tree.base, tree.base.y + tree.shape.trunk_height, tree.shape.leaf_radius, tree.shape.leaf_height,
                                eye, direction));
    return best;
}

void hit_trees(std::optional<float>& best, const std::vector<tree_body>& trees, const glm::vec3& eye,
               const glm::vec3& direction, const float max_distance) {
    for (const tree_body& tree : trees) {
        const float reach = std::max(tree.shape.trunk_radius, tree.shape.leaf_radius);
        if (flat_distance_to_line(tree.base, eye, direction, best.value_or(max_distance)) <= reach) {
            keep_nearest(best, tree_hit(tree, eye, direction));
        }
    }
}

// A fence's net: upright between its poles, from the ground up its height.
std::optional<float> net_hit(const fence_panel& panel, const glm::vec3& eye, const glm::vec3& direction) {
    const glm::vec3 span = horizontal(panel.b - panel.a);
    const float span_length2 = glm::dot(span, span);
    if (span_length2 < tiny) {
        return std::nullopt;
    }
    const glm::vec3 normal(span.z, 0.0f, -span.x);
    const float facing = glm::dot(direction, normal);
    if (std::abs(facing) < tiny) {
        return std::nullopt;
    }
    const float t = glm::dot(panel.a - eye, normal) / facing;
    const glm::vec3 at = eye + direction * t;
    const float s = glm::dot(horizontal(at - panel.a), span) / span_length2;
    if (s < 0.0f || s > 1.0f) {
        return std::nullopt;
    }
    const float ground = panel.a.y + (panel.b.y - panel.a.y) * s;
    return at.y >= ground && at.y <= ground + panel.height ? std::optional<float>(t) : std::nullopt;
}

sight_box board_box(const hole_sign& sign) {
    return sight_box{sign.board_center, sign.down_hole,
                     glm::vec3(sign.board_half_width, sign.board_half_height, 0.5f * sign.board_thickness)};
}
}

std::optional<float> post_hit(const sight_post& post, const glm::vec3& eye, const glm::vec3& direction) {
    if (post.radius <= 0.0f || post.height <= 0.0f) {
        return std::nullopt;
    }
    const float top = post.base.y + post.height;
    const glm::vec3 from = horizontal(eye - post.base);
    const glm::vec3 flat = horizontal(direction);
    const float a = glm::dot(flat, flat);
    const float b = glm::dot(from, flat);
    const float c = glm::dot(from, from) - post.radius * post.radius;
    std::optional<float> best;
    if (a > tiny && c > 0.0f) {
        const float disc = b * b - a * c;
        if (disc >= 0.0f) {
            const float t = (-b - std::sqrt(disc)) / a;
            const float y = eye.y + direction.y * t;
            if (y >= post.base.y && y <= top) {
                keep_nearest(best, t);
            }
        }
    }
    if (direction.y < -tiny) {
        const float t = (top - eye.y) / direction.y;
        const glm::vec3 at = eye + direction * t;
        if (glm::length(horizontal(at - post.base)) <= post.radius) {
            keep_nearest(best, t);
        }
    }
    return best;
}

std::optional<float> box_hit(const sight_box& box, const glm::vec3& eye, const glm::vec3& direction) {
    const glm::vec3 across(box.along.z, 0.0f, -box.along.x);
    const glm::vec3 axes[3] = {box.along, world_up, across};
    const glm::vec3 from = eye - box.center;
    float near = -std::numeric_limits<float>::max();
    float far = std::numeric_limits<float>::max();
    for (int i = 0; i < 3; ++i) {
        const float origin = glm::dot(from, axes[i]);
        const float along = glm::dot(direction, axes[i]);
        const float half = box.half[i];
        if (std::abs(along) < tiny) {
            if (std::abs(origin) > half) {
                return std::nullopt;
            }
            continue;
        }
        float t0 = (-half - origin) / along;
        float t1 = (half - origin) / along;
        if (t0 > t1) {
            std::swap(t0, t1);
        }
        near = std::max(near, t0);
        far = std::min(far, t1);
        if (near > far) {
            return std::nullopt;
        }
    }
    return near > 0.0f ? std::optional<float>(near) : std::nullopt;
}

std::optional<float> standing_hit(const sight_scene& scene, const glm::vec3& eye, const glm::vec3& direction,
                                  const float max_distance) {
    std::optional<float> best;
    if (scene.trees != nullptr) {
        hit_trees(best, *scene.trees, eye, direction, max_distance);
    }
    if (scene.area != nullptr) {
        const play_area& area = *scene.area;
        hit_trees(best, area.sign_posts, eye, direction, max_distance);
        hit_trees(best, area.fence_poles, eye, direction, max_distance);
        hit_trees(best, area.floodlight_poles, eye, direction, max_distance);
        for (const hole_sign& sign : area.signs) {
            keep_nearest(best, box_hit(board_box(sign), eye, direction));
        }
        for (const fence_panel& panel : area.fence_panels) {
            keep_nearest(best, net_hit(panel, eye, direction));
        }
    }
    for (const sight_post& post : scene.posts) {
        keep_nearest(best, post_hit(post, eye, direction));
    }
    for (const sight_box& box : scene.boxes) {
        keep_nearest(best, box_hit(box, eye, direction));
    }
    if (best && *best > max_distance) {
        return std::nullopt;
    }
    return best;
}
