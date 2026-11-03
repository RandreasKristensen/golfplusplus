#include "physics/solid.h"

#include "physics/collision.h"
#include "physics/vector_math.h"

#include <algorithm>
#include <cmath>

#include <glm/common.hpp>
#include <glm/geometric.hpp>

namespace {
// Leaves push the ball slightly upwards so it drops out of the canopy.
constexpr float leaf_lift = 0.18f;
// Pushed this little past touching, so a swept bounce still registers when
// the contact lands exactly a radius off the wall.
constexpr float contact_slop = 0.0001f;

glm::vec3 horizontal_push_direction(const ball_state& ball, const glm::vec3& base) {
    const glm::vec3 away = horizontal(ball.position - base);
    const glm::vec3 backwards = horizontal(-ball.velocity);
    return safe_normalize(away, safe_normalize(backwards, glm::vec3(1.0f, 0.0f, 0.0f)));
}

glm::vec3 across_of(const glm::vec3& along) {
    return glm::vec3(along.z, 0.0f, -along.x);
}

ball_state cylinder_contact(const ball_state& in, const solid& post) {
    const float ball_radius = std::max(0.0f, in.radius);
    const float reach = std::max(0.0f, post.radius) + ball_radius;
    const float min_y = post.base.y - ball_radius;
    const float max_y = post.base.y + std::max(0.0f, post.height) + ball_radius;
    if (in.position.y < min_y || in.position.y > max_y) {
        return in;
    }
    const float distance = horizontal_distance(in.position, post.base);
    if (distance >= reach) {
        return in;
    }
    return resolve_contact(in, horizontal_push_direction(in, post.base), reach - distance, post.restitution, post.friction);
}

ball_state cone_contact(const ball_state& in, const solid& cone) {
    const float height = std::max(0.0f, cone.height);
    if (height <= 0.00001f) {
        return in;
    }
    const float ball_radius = std::max(0.0f, in.radius);
    const float bottom = cone.base.y;
    const float top = bottom + height;
    if (in.position.y < bottom - ball_radius || in.position.y > top + ball_radius) {
        return in;
    }
    // Widest at its base, a point at the top.
    const float height_in_cone = std::clamp(in.position.y - bottom, 0.0f, height);
    const float cone_radius = std::max(0.0f, cone.radius) * (1.0f - height_in_cone / height);
    const float reach = cone_radius + ball_radius;
    const float distance = horizontal_distance(in.position, cone.base);
    if (distance >= reach) {
        return in;
    }
    const glm::vec3 push = horizontal_push_direction(in, cone.base);
    const glm::vec3 normal = safe_normalize(push + glm::vec3(0.0f, leaf_lift, 0.0f), push);
    return resolve_contact(in, normal, reach - distance, cone.restitution, cone.friction);
}

ball_state box_contact(const ball_state& in, const solid& box) {
    const float ball_radius = std::max(0.0f, in.radius);
    const glm::vec3 across = across_of(box.along);
    const float half_height = 0.5f * box.height;
    const glm::vec3 middle = box.base + glm::vec3(0.0f, half_height, 0.0f);
    const glm::vec3 axes[3] = {box.along, world_up, across};
    const float halves[3] = {box.half_length, half_height, box.half_width};
    const glm::vec3 offset = in.position - middle;
    glm::vec3 nearest = middle;
    bool inside = true;
    for (int i = 0; i < 3; ++i) {
        const float along = glm::dot(offset, axes[i]);
        inside = inside && std::abs(along) < halves[i];
        nearest += axes[i] * std::clamp(along, -halves[i], halves[i]);
    }
    if (!inside) {
        const glm::vec3 out = in.position - nearest;
        const float distance = glm::length(out);
        if (distance >= ball_radius || distance <= 0.0f) {
            return in;
        }
        return resolve_contact(in, out / distance, ball_radius - distance, box.restitution, box.friction);
    }
    // Inside: out through the nearest face.
    int face = 0;
    float least = halves[0] - std::abs(glm::dot(offset, axes[0]));
    for (int i = 1; i < 3; ++i) {
        const float room = halves[i] - std::abs(glm::dot(offset, axes[i]));
        if (room < least) {
            least = room;
            face = i;
        }
    }
    const glm::vec3 normal = axes[face] * (glm::dot(offset, axes[face]) < 0.0f ? -1.0f : 1.0f);
    return resolve_contact(in, normal, least + ball_radius, box.restitution, box.friction);
}

// The XZ box a solid stands on.
void footprint(const solid& thing, glm::vec2& low, glm::vec2& high) {
    const glm::vec2 base(thing.base.x, thing.base.z);
    switch (thing.shape) {
    case solid_shape::cylinder:
    case solid_shape::cone:
        low = base - glm::vec2(thing.radius);
        high = base + glm::vec2(thing.radius);
        return;
    case solid_shape::wall: {
        const glm::vec2 end(thing.end.x, thing.end.z);
        low = glm::min(base, end) - glm::vec2(thing.radius);
        high = glm::max(base, end) + glm::vec2(thing.radius);
        return;
    }
    case solid_shape::box: {
        const glm::vec3 across = across_of(thing.along);
        const glm::vec2 half(std::abs(thing.along.x) * thing.half_length + std::abs(across.x) * thing.half_width,
                             std::abs(thing.along.z) * thing.half_length + std::abs(across.z) * thing.half_width);
        low = base - half;
        high = base + half;
        return;
    }
    }
}

float push_reach(const solid& thing) {
    return thing.shape == solid_shape::box ? std::max(thing.half_length, thing.half_width) : thing.radius;
}
}

bool stops(const solid& thing, const solid_mover mover) {
    switch (mover) {
    case solid_mover::ball:
        return thing.stops_ball;
    case solid_mover::walker:
        return thing.stops_walker;
    case solid_mover::cart:
        return thing.stops_cart;
    }
    return false;
}

float solid_top(const solid& thing) {
    return thing.base.y + thing.height;
}

solid_grid build_solid_grid(const std::vector<solid>& solids, const float cell_size) {
    solid_grid grid;
    if (solids.empty() || cell_size <= 0.0f) {
        return grid;
    }
    std::vector<glm::vec2> lows(solids.size());
    std::vector<glm::vec2> highs(solids.size());
    glm::vec2 low(0.0f);
    glm::vec2 high(0.0f);
    for (std::size_t i = 0; i < solids.size(); ++i) {
        footprint(solids[i], lows[i], highs[i]);
        low = i == 0 ? lows[i] : glm::min(low, lows[i]);
        high = i == 0 ? highs[i] : glm::max(high, highs[i]);
        grid.reach = std::max(grid.reach, push_reach(solids[i]));
    }
    grid.cell_size = cell_size;
    grid.origin = low;
    grid.columns = static_cast<int>((high.x - low.x) / cell_size) + 1;
    grid.rows = static_cast<int>((high.y - low.y) / cell_size) + 1;
    grid.cells.resize(static_cast<std::size_t>(grid.columns) * static_cast<std::size_t>(grid.rows));
    const auto cell_of = [&grid](const float offset, const int count) {
        return std::clamp(static_cast<int>(std::floor(offset / grid.cell_size)), 0, count - 1);
    };
    for (std::size_t i = 0; i < solids.size(); ++i) {
        for (int row = cell_of(lows[i].y - low.y, grid.rows); row <= cell_of(highs[i].y - low.y, grid.rows); ++row) {
            for (int column = cell_of(lows[i].x - low.x, grid.columns); column <= cell_of(highs[i].x - low.x, grid.columns);
                 ++column) {
                grid.cells[static_cast<std::size_t>(row) * static_cast<std::size_t>(grid.columns) +
                           static_cast<std::size_t>(column)]
                    .push_back(static_cast<std::uint32_t>(i));
            }
        }
    }
    return grid;
}

std::vector<std::uint32_t> solids_near(const solid_grid& grid, const glm::vec3& low, const glm::vec3& high,
                                       const float margin) {
    std::vector<std::uint32_t> near;
    if (grid.cells.empty()) {
        return near;
    }
    const auto cell_of = [&grid](const float offset, const int count) {
        return std::clamp(static_cast<int>(std::floor(offset / grid.cell_size)), -1, count);
    };
    const int first_column = std::max(cell_of(low.x - margin - grid.origin.x, grid.columns), 0);
    const int last_column = std::min(cell_of(high.x + margin - grid.origin.x, grid.columns), grid.columns - 1);
    const int first_row = std::max(cell_of(low.z - margin - grid.origin.y, grid.rows), 0);
    const int last_row = std::min(cell_of(high.z + margin - grid.origin.y, grid.rows), grid.rows - 1);
    for (int row = first_row; row <= last_row; ++row) {
        for (int column = first_column; column <= last_column; ++column) {
            const std::vector<std::uint32_t>& cell =
                grid.cells[static_cast<std::size_t>(row) * static_cast<std::size_t>(grid.columns) +
                           static_cast<std::size_t>(column)];
            near.insert(near.end(), cell.begin(), cell.end());
        }
    }
    std::sort(near.begin(), near.end());
    near.erase(std::unique(near.begin(), near.end()), near.end());
    return near;
}

std::optional<float> walkable_top_at(const std::vector<solid>& solids, const solid_grid& grid, const glm::vec3& point) {
    if (grid.cells.empty()) {
        return std::nullopt;
    }
    const int column = static_cast<int>(std::floor((point.x - grid.origin.x) / grid.cell_size));
    const int row = static_cast<int>(std::floor((point.z - grid.origin.y) / grid.cell_size));
    if (column < 0 || row < 0 || column >= grid.columns || row >= grid.rows) {
        return std::nullopt;
    }
    std::optional<float> top;
    for (const std::uint32_t index :
         grid.cells[static_cast<std::size_t>(row) * static_cast<std::size_t>(grid.columns) + static_cast<std::size_t>(column)]) {
        const solid& thing = solids[index];
        if (!thing.walkable) {
            continue;
        }
        const glm::vec3 offset = horizontal(point - thing.base);
        const bool over = thing.shape == solid_shape::box
            ? std::abs(glm::dot(offset, thing.along)) <= thing.half_length &&
                std::abs(glm::dot(offset, across_of(thing.along))) <= thing.half_width
            : glm::length(offset) <= thing.radius;
        if (over && (!top || solid_top(thing) > *top)) {
            top = solid_top(thing);
        }
    }
    return top;
}

ball_state resolve_solid_contact(const ball_state& in, const solid& thing) {
    switch (thing.shape) {
    case solid_shape::cylinder:
        return cylinder_contact(in, thing);
    case solid_shape::cone:
        return cone_contact(in, thing);
    case solid_shape::box:
        return box_contact(in, thing);
    case solid_shape::wall:
        break;
    }
    return in;
}

ball_state resolve_swept_wall(const glm::vec3& previous_position, const ball_state& in, const solid& wall) {
    const glm::vec3 span = horizontal(wall.end - wall.base);
    const float length = glm::length(span);
    if (length < 0.0001f || wall.height <= 0.0f) {
        return in;
    }
    const glm::vec3 u = span / length;
    const glm::vec3 n(-u.z, 0.0f, u.x);
    const float radius = std::max(0.0f, in.radius);

    // Signed distances from the wall's line before and after the step, on the
    // side the ball came from.
    const float before = glm::dot(horizontal(previous_position - wall.base), n);
    const float side = before > 0.0f ? 1.0f : (before < 0.0f ? -1.0f : (glm::dot(in.velocity, n) > 0.0f ? -1.0f : 1.0f));
    const float from = side * before;
    const float to = side * glm::dot(horizontal(in.position - wall.base), n);
    if (to >= radius) {
        return in;  // ends the step clear of the wall, on its own side
    }

    // Where the path first comes within a radius of the wall (where the step
    // starts, when it already touches it).
    const float t = from > radius ? (from - radius) / (from - to) : 1.0f;
    const glm::vec3 contact = previous_position + (in.position - previous_position) * clamp01(t);
    const float across = glm::dot(horizontal(contact - wall.base), u);
    if (across < 0.0f || across > length) {
        return in;  // past its ends (its poles are solids of their own)
    }
    const float foot = wall.base.y + (wall.end.y - wall.base.y) * (across / length);
    if (contact.y < foot - radius || contact.y > foot + wall.height + radius) {
        return in;  // under the ground or over the top
    }

    ball_state out = in;
    out.position = contact;
    const float distance = side * glm::dot(horizontal(contact - wall.base), n);
    return resolve_contact(out, n * side, radius - distance + contact_slop, wall.restitution, wall.friction);
}

namespace {
bool moved(const ball_state& before, const ball_state& after) {
    return glm::length(after.velocity - before.velocity) > 0.01f || glm::length(after.position - before.position) > 0.001f;
}

bool meets_ball(const solid& thing) {
    return thing.stops_ball && !thing.porous;
}

// Inside a porous solid's shape (a cone's: widest at its base, a point at its top).
bool inside(const solid& thing, const glm::vec3& point) {
    const float height = std::max(0.0f, thing.height);
    const float up = point.y - thing.base.y;
    if (height <= 0.0f || up < 0.0f || up > height) {
        return false;
    }
    const float reach = thing.shape == solid_shape::cone ? thing.radius * (1.0f - up / height) : thing.radius;
    return horizontal_distance(point, thing.base) < reach;
}
}

ball_contacts resolve_ball_contacts(const glm::vec3& previous_position, const ball_state& in,
                                    const std::vector<solid>& solids, const solid_grid& grid,
                                    const std::vector<solid>& moving, const std::size_t moving_count) {
    // A solid that pushes the ball moves it at most its own reach, so one
    // further than twice that could not be touched after a push either.
    const float radius = std::max(0.0f, in.radius);
    const std::vector<std::uint32_t> near =
        solids_near(grid, glm::min(previous_position, in.position), glm::max(previous_position, in.position),
                    2.0f * (grid.reach + radius) + radius);
    ball_contacts out{in, std::nullopt, std::nullopt};
    const auto meet = [&out](const solid& thing, const bool swept_walls, const glm::vec3& from,
                             const std::optional<std::size_t> moving_index) {
        if (!meets_ball(thing) || (thing.shape == solid_shape::wall) != swept_walls) {
            return;
        }
        const ball_state before = out.ball;
        out.ball = swept_walls ? resolve_swept_wall(from, out.ball, thing) : resolve_solid_contact(out.ball, thing);
        if (!out.hit && moved(before, out.ball)) {
            out.hit = thing.kind;
            out.moving_hit = moving_index;
        }
    };
    // Each wall is swept from the step's start to wherever the ball ends by
    // then, so a wall the path reaches sooner than an earlier hit wins.
    for (const bool walls : {false, true}) {
        for (const std::uint32_t index : near) {
            meet(solids[index], walls, previous_position, std::nullopt);
        }
        for (std::size_t i = 0; i < std::min(moving_count, moving.size()); ++i) {
            meet(moving[i], walls, previous_position, i);
        }
    }
    return out;
}

float porous_path(const glm::vec3& from, const glm::vec3& to, const std::vector<solid>& solids, const solid_grid& grid) {
    constexpr int samples = 8;
    const float length = glm::length(to - from);
    if (length <= 0.0f) {
        return 0.0f;
    }
    const std::vector<std::uint32_t> near = solids_near(grid, glm::min(from, to), glm::max(from, to), 0.0f);
    int within = 0;
    for (int i = 0; i < samples; ++i) {
        const glm::vec3 point = from + (to - from) * ((static_cast<float>(i) + 0.5f) / static_cast<float>(samples));
        for (const std::uint32_t index : near) {
            if (solids[index].porous && inside(solids[index], point)) {
                ++within;
                break;
            }
        }
    }
    return length * static_cast<float>(within) / static_cast<float>(samples);
}
