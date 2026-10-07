#include "physics/tree_collision.h"

#include "physics/collision.h"
#include "physics/vector_math.h"

#include <algorithm>
#include <cmath>

namespace {
// Leaves push the ball slightly upwards so it drops out of the canopy.
constexpr float leaf_lift = 0.18f;

glm::vec3 horizontal_push_direction(const ball_state& ball, const glm::vec3& base) {
    const glm::vec3 away = horizontal(ball.position - base);
    const glm::vec3 backwards = horizontal(-ball.velocity);
    return safe_normalize(away, safe_normalize(backwards, glm::vec3(1.0f, 0.0f, 0.0f)));
}

ball_state resolve_trunk_collision(const ball_state& in,
                                   const tree_body& tree,
                                   const float restitution,
                                   const float friction) {
    const float ball_radius = std::max(0.0f, in.radius);
    const float reach = std::max(0.0f, tree.shape.trunk_radius) + ball_radius;
    const float min_y = tree.base.y - ball_radius;
    const float max_y = tree.base.y + std::max(0.0f, tree.shape.trunk_height) + ball_radius;
    if (in.position.y < min_y || in.position.y > max_y) {
        return in;
    }

    const float distance = horizontal_distance(in.position, tree.base);
    if (distance >= reach) {
        return in;
    }
    return resolve_contact(in, horizontal_push_direction(in, tree.base), reach - distance, restitution, friction);
}

ball_state resolve_leaf_collision(const ball_state& in,
                                  const tree_body& tree,
                                  const float restitution,
                                  const float friction) {
    const float leaf_height = std::max(0.0f, tree.shape.leaf_height);
    if (leaf_height <= 0.00001f) {
        return in;
    }

    const float ball_radius = std::max(0.0f, in.radius);
    const float leaf_base_y = tree.base.y + std::max(0.0f, tree.shape.trunk_height);
    const float leaf_top_y = leaf_base_y + leaf_height;
    if (in.position.y < leaf_base_y - ball_radius || in.position.y > leaf_top_y + ball_radius) {
        return in;
    }

    // The canopy is a cone: widest at its base, a point at the top.
    const float height_in_cone = std::clamp(in.position.y - leaf_base_y, 0.0f, leaf_height);
    const float cone_radius = std::max(0.0f, tree.shape.leaf_radius) * (1.0f - height_in_cone / leaf_height);
    const float reach = cone_radius + ball_radius;

    const float distance = horizontal_distance(in.position, tree.base);
    if (distance >= reach) {
        return in;
    }

    const glm::vec3 push = horizontal_push_direction(in, tree.base);
    const glm::vec3 normal = safe_normalize(push + glm::vec3(0.0f, leaf_lift, 0.0f), push);
    return resolve_contact(in, normal, reach - distance, restitution, friction);
}
}

ball_state resolve_tree_collision(const ball_state& in,
                                  const tree_body& tree,
                                  const float restitution,
                                  const float friction) {
    return resolve_leaf_collision(resolve_trunk_collision(in, tree, restitution, friction),
                                  tree,
                                  restitution,
                                  friction);
}

ball_state resolve_tree_collisions(const ball_state& in,
                                   const std::vector<tree_body>& trees,
                                   const float restitution,
                                   const float friction) {
    ball_state out = in;
    for (const tree_body& tree : trees) {
        out = resolve_tree_collision(out, tree, restitution, friction);
    }
    return out;
}

tree_grid build_tree_grid(const std::vector<tree_body>& trees, const float cell_size) {
    tree_grid grid;
    if (trees.empty() || cell_size <= 0.0f) {
        return grid;
    }
    glm::vec2 low(trees.front().base.x, trees.front().base.z);
    glm::vec2 high = low;
    for (const tree_body& tree : trees) {
        low = glm::min(low, glm::vec2(tree.base.x, tree.base.z));
        high = glm::max(high, glm::vec2(tree.base.x, tree.base.z));
        grid.reach = std::max({grid.reach, tree.shape.trunk_radius, tree.shape.leaf_radius});
    }
    grid.cell_size = cell_size;
    grid.origin = low;
    grid.columns = static_cast<int>((high.x - low.x) / cell_size) + 1;
    grid.rows = static_cast<int>((high.y - low.y) / cell_size) + 1;
    grid.cells.resize(static_cast<std::size_t>(grid.columns) * static_cast<std::size_t>(grid.rows));
    for (std::size_t i = 0; i < trees.size(); ++i) {
        const int column = static_cast<int>((trees[i].base.x - low.x) / cell_size);
        const int row = static_cast<int>((trees[i].base.z - low.y) / cell_size);
        grid.cells[static_cast<std::size_t>(row) * static_cast<std::size_t>(grid.columns) +
                   static_cast<std::size_t>(column)].push_back(static_cast<std::uint32_t>(i));
    }
    return grid;
}

ball_state resolve_tree_collisions(const ball_state& in,
                                   const std::vector<tree_body>& trees,
                                   const tree_grid& grid,
                                   const float restitution,
                                   const float friction) {
    if (grid.cells.empty()) {
        return in;
    }
    // Twice the reach: a tree that pushes the ball moves it at most its own
    // reach, so a tree beyond that could not be touched after the push either.
    const float search = 2.0f * (grid.reach + std::max(0.0f, in.radius));
    const auto cell_of = [&grid](const float offset, const int count) {
        return std::clamp(static_cast<int>(std::floor(offset / grid.cell_size)), -1, count);
    };
    const int first_column = cell_of(in.position.x - search - grid.origin.x, grid.columns);
    const int last_column = cell_of(in.position.x + search - grid.origin.x, grid.columns);
    const int first_row = cell_of(in.position.z - search - grid.origin.y, grid.rows);
    const int last_row = cell_of(in.position.z + search - grid.origin.y, grid.rows);
    std::vector<std::uint32_t> near;
    for (int row = std::max(first_row, 0); row <= std::min(last_row, grid.rows - 1); ++row) {
        for (int column = std::max(first_column, 0); column <= std::min(last_column, grid.columns - 1); ++column) {
            const std::vector<std::uint32_t>& cell =
                grid.cells[static_cast<std::size_t>(row) * static_cast<std::size_t>(grid.columns) +
                           static_cast<std::size_t>(column)];
            near.insert(near.end(), cell.begin(), cell.end());
        }
    }
    std::sort(near.begin(), near.end());
    ball_state out = in;
    for (const std::uint32_t index : near) {
        out = resolve_tree_collision(out, trees[index], restitution, friction);
    }
    return out;
}
