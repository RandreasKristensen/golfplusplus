#include "physics/solid.h"

#include "physics/collision.h"
#include "physics/vector_math.h"

#include <algorithm>
#include <cmath>

#include <glm/common.hpp>
#include <glm/geometric.hpp>

namespace {
// Numerical allowance at a slab's offset outline, shared by its grid bounds
// and top query so rounded edge vertices remain on the raised surface.
constexpr float slab_edge_roundoff = 0.0005f;

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

// A box's middle, its axes (along, up, across) and its half extents along them.
struct box_frame {
    glm::vec3 middle;
    glm::vec3 axes[3];
    float halves[3];
};

box_frame frame_of(const solid& box) {
    const float half_height = 0.5f * box.height;
    return box_frame{box.base + glm::vec3(0.0f, half_height, 0.0f),
                     {box.along, world_up, across_of(box.along)},
                     {box.half_length, half_height, box.half_width}};
}

// The point of the box nearest `point`, and whether `point` is inside it.
struct box_nearest {
    glm::vec3 point;
    bool inside;
};

box_nearest nearest_on_box(const box_frame& box, const glm::vec3& point) {
    const glm::vec3 offset = point - box.middle;
    box_nearest nearest{box.middle, true};
    for (int i = 0; i < 3; ++i) {
        const float along = glm::dot(offset, box.axes[i]);
        nearest.inside = nearest.inside && std::abs(along) < box.halves[i];
        nearest.point += box.axes[i] * std::clamp(along, -box.halves[i], box.halves[i]);
    }
    return nearest;
}

ball_state box_contact(const ball_state& in, const solid& box) {
    const float ball_radius = std::max(0.0f, in.radius);
    const box_frame frame = frame_of(box);
    const box_nearest nearest = nearest_on_box(frame, in.position);
    if (!nearest.inside) {
        const glm::vec3 out = in.position - nearest.point;
        const float distance = glm::length(out);
        if (distance >= ball_radius || distance <= 0.0f) {
            return in;
        }
        return resolve_contact(in, out / distance, ball_radius - distance, box.restitution, box.friction);
    }
    // Inside: out through the nearest face.
    const glm::vec3 offset = in.position - frame.middle;
    int face = 0;
    float least = frame.halves[0] - std::abs(glm::dot(offset, frame.axes[0]));
    for (int i = 1; i < 3; ++i) {
        const float room = frame.halves[i] - std::abs(glm::dot(offset, frame.axes[i]));
        if (room < least) {
            least = room;
            face = i;
        }
    }
    const glm::vec3 normal = frame.axes[face] * (glm::dot(offset, frame.axes[face]) < 0.0f ? -1.0f : 1.0f);
    return resolve_contact(in, normal, least + ball_radius, box.restitution, box.friction);
}

// Where a ball's step from `from` to `in.position` first meets a solid it
// began clear of: the share of the step travelled, and the ball met there.
struct swept_meeting {
    float share = 1.0f;
    ball_state ball;
};

// Its range of `share` (0..1 along `path` from `start`) between `low` and
// `high` on one axis, narrowed into [enter, leave]; false once it is empty.
bool narrow_to_slab(const float start, const float path, const float low, const float high, float& enter, float& leave) {
    if (std::abs(path) <= 1.0e-12f) {
        return start >= low && start <= high;
    }
    float near_share = (low - start) / path;
    float far_share = (high - start) / path;
    if (near_share > far_share) {
        std::swap(near_share, far_share);
    }
    enter = std::max(enter, near_share);
    leave = std::min(leave, far_share);
    return enter <= leave;
}

// The cylinder met as cylinder_contact meets it, at the first point of the
// step where the ball's centre comes within that contact's reach (round
// within its side, level at its ends) rather than only where the step ends.
std::optional<swept_meeting> sweep_cylinder(const glm::vec3& from, const ball_state& in, const solid& post) {
    const float ball_radius = std::max(0.0f, in.radius);
    const float reach = std::max(0.0f, post.radius) + ball_radius;
    const glm::vec3 path = in.position - from;
    const glm::vec3 start = horizontal(from - post.base);
    const glm::vec3 level = horizontal(path);
    float enter = -1.0f;
    float leave = 2.0f;
    const float a = glm::dot(level, level);
    const float c = glm::dot(start, start) - reach * reach;
    if (a <= 1.0e-12f) {
        if (c >= 0.0f) {
            return std::nullopt;
        }
    } else {
        const float b = glm::dot(start, level);
        const float discriminant = b * b - a * c;
        if (discriminant <= 0.0f) {
            return std::nullopt;
        }
        const float root = std::sqrt(discriminant);
        enter = (-b - root) / a;
        leave = (-b + root) / a;
    }
    const float bottom = post.base.y - ball_radius;
    const float top = post.base.y + std::max(0.0f, post.height) + ball_radius;
    if (!narrow_to_slab(from.y, path.y, bottom, top, enter, leave) || enter <= 0.0f || enter > 1.0f) {
        return std::nullopt;  // never in reach, already there at the start, or not before the step ends
    }
    ball_state at = in;
    at.position = from + path * enter;
    const float distance = horizontal_distance(at.position, post.base);
    return swept_meeting{enter, resolve_contact(at, horizontal_push_direction(at, post.base),
                                                std::max(0.0f, reach - distance) + contact_slop, post.restitution,
                                                post.friction)};
}

// Steps of the march into a box's rounded corners, after the first.
constexpr int box_sweep_steps = 32;

// The box met as box_contact meets it (within a ball radius of it), at the
// first point of the step where it does: into the box grown by a radius
// each way, then on along the path by the remaining gap, which never
// overshoots the rounded edges and corners (the box is convex).
std::optional<swept_meeting> sweep_box(const glm::vec3& from, const ball_state& in, const solid& box) {
    const float ball_radius = std::max(0.0f, in.radius);
    const box_frame frame = frame_of(box);
    const glm::vec3 path = in.position - from;
    const float length = glm::length(path);
    if (length <= 0.0f) {
        return std::nullopt;
    }
    float enter = -1.0f;
    float leave = 2.0f;
    for (int i = 0; i < 3; ++i) {
        const float start = glm::dot(from - frame.middle, frame.axes[i]);
        const float grown = frame.halves[i] + ball_radius;
        if (!narrow_to_slab(start, glm::dot(path, frame.axes[i]), -grown, grown, enter, leave)) {
            return std::nullopt;
        }
    }
    if (leave < 0.0f || enter > 1.0f) {
        return std::nullopt;
    }
    float share = std::max(0.0f, enter);
    for (int step = 0; step <= box_sweep_steps; ++step) {
        ball_state at = in;
        at.position = from + path * share;
        const box_nearest nearest = nearest_on_box(frame, at.position);
        const glm::vec3 out = at.position - nearest.point;
        const float distance = nearest.inside ? 0.0f : glm::length(out);
        const float gap = distance - ball_radius;
        if (share <= 0.0f && gap < 0.0f) {
            return std::nullopt;  // already in contact: met where the step ends (box_contact)
        }
        if (distance <= 0.0f || glm::dot(out, path) >= 0.0f) {
            return std::nullopt;  // on its way out, or past it
        }
        if (gap <= contact_slop) {
            return swept_meeting{share, resolve_contact(at, out / distance, std::max(0.0f, -gap) + contact_slop,
                                                        box.restitution, box.friction)};
        }
        share += gap / length;
        if (share > 1.0f) {
            return std::nullopt;
        }
    }
    return std::nullopt;  // only grazing it, still a hair away
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
    case solid_shape::wall:
    case solid_shape::slab: {
        const glm::vec2 end(thing.end.x, thing.end.z);
        const float radius = thing.radius + (thing.shape == solid_shape::slab ? slab_edge_roundoff : 0.0f);
        low = glm::min(base, end) - glm::vec2(radius);
        high = glm::max(base, end) + glm::vec2(radius);
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
    switch (thing.shape) {
    case solid_shape::box:
        return std::max(thing.half_length, thing.half_width);
    case solid_shape::slab:
        return 0.0f;  // never pushes
    case solid_shape::cylinder:
    case solid_shape::cone:
    case solid_shape::wall:
        break;
    }
    return thing.radius;
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

namespace {
// `thing`'s top over `point` (XZ), if it is over it.
std::optional<walkable_top> top_over(const solid& thing, const glm::vec3& point) {
    const glm::vec3 offset = horizontal(point - thing.base);
    if (thing.shape == solid_shape::slab) {
        const glm::vec3 level = horizontal(thing.end - thing.base);
        const float length_squared = glm::dot(level, level);
        const float t = length_squared <= 1.0e-8f ? 0.0f : clamp01(glm::dot(offset, level) / length_squared);
        // Rounded offset vertices at course coordinates can land a fraction
        // of a millimetre outside the exact capsule. Keep its boundary on the
        // same raised top as its interior, including where it is drawn.
        if (glm::length(offset - level * t) > thing.radius + slab_edge_roundoff) {
            return std::nullopt;
        }
        const glm::vec3 across = safe_normalize(glm::cross(level, world_up), glm::vec3(1.0f, 0.0f, 0.0f));
        return walkable_top{thing.height, world_up, 0, true, across};
    }
    const bool over = thing.shape == solid_shape::box
        ? std::abs(glm::dot(offset, thing.along)) <= thing.half_length &&
            std::abs(glm::dot(offset, across_of(thing.along))) <= thing.half_width
        : glm::length(offset) <= thing.radius;
    if (!over) {
        return std::nullopt;
    }
    return walkable_top{solid_top(thing), world_up, 0};
}
}

std::optional<walkable_top> walkable_top_at(const std::vector<solid>& solids, const solid_grid& grid, const glm::vec3& point) {
    if (grid.cells.empty()) {
        return std::nullopt;
    }
    const int column = static_cast<int>(std::floor((point.x - grid.origin.x) / grid.cell_size));
    const int row = static_cast<int>(std::floor((point.z - grid.origin.y) / grid.cell_size));
    if (column < 0 || row < 0 || column >= grid.columns || row >= grid.rows) {
        return std::nullopt;
    }
    std::optional<walkable_top> top;
    for (const std::uint32_t index :
         grid.cells[static_cast<std::size_t>(row) * static_cast<std::size_t>(grid.columns) + static_cast<std::size_t>(column)]) {
        if (!solids[index].walkable) {
            continue;
        }
        std::optional<walkable_top> here = top_over(solids[index], point);
        const bool better = here && (!top || (here->over_surface && !top->over_surface) ||
                                     (here->over_surface == top->over_surface && here->height > top->height));
        if (better) {
            here->solid = index;
            top = here;
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
    case solid_shape::slab:
        break;
    }
    return in;
}

namespace {
// The wall, met where the path first comes within a radius of it.
std::optional<swept_meeting> sweep_wall(const glm::vec3& previous_position, const ball_state& in, const solid& wall) {
    const glm::vec3 span = horizontal(wall.end - wall.base);
    const float length = glm::length(span);
    if (length < 0.0001f || wall.height <= 0.0f) {
        return std::nullopt;
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
        return std::nullopt;  // ends the step clear of the wall, on its own side
    }

    // Where the path first comes within a radius of the wall (where the step
    // ends, when it already touches it as the step starts).
    const float t = from > radius ? (from - radius) / (from - to) : 1.0f;
    const glm::vec3 contact = previous_position + (in.position - previous_position) * clamp01(t);
    const float across = glm::dot(horizontal(contact - wall.base), u);
    if (across < 0.0f || across > length) {
        return std::nullopt;  // past its ends (its poles are solids of their own)
    }
    const float foot = wall.base.y + (wall.end.y - wall.base.y) * (across / length);
    if (contact.y < foot - radius || contact.y > foot + wall.height + radius) {
        return std::nullopt;  // under the ground or over the top
    }

    ball_state out = in;
    out.position = contact;
    const float distance = side * glm::dot(horizontal(contact - wall.base), n);
    return swept_meeting{clamp01(t), resolve_contact(out, n * side, radius - distance + contact_slop, wall.restitution,
                                                     wall.friction)};
}

// The first point of the step where the ball meets `thing` (walls,
// cylinders and boxes), if it began the step clear of it.
std::optional<swept_meeting> sweep(const glm::vec3& from, const ball_state& in, const solid& thing) {
    switch (thing.shape) {
    case solid_shape::wall:
        return sweep_wall(from, in, thing);
    case solid_shape::cylinder:
        return sweep_cylinder(from, in, thing);
    case solid_shape::box:
        return sweep_box(from, in, thing);
    case solid_shape::cone:
    case solid_shape::slab:
        break;
    }
    return std::nullopt;
}
}

ball_state resolve_swept_wall(const glm::vec3& previous_position, const ball_state& in, const solid& wall) {
    const std::optional<swept_meeting> met = sweep_wall(previous_position, in, wall);
    return met ? met->ball : in;
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
    const std::size_t moving_met = std::min(moving_count, moving.size());
    ball_contacts out{in, std::nullopt, std::nullopt};

    // The first wall, cylinder or box the step's path meets: the ball stops
    // there, turned, and the rest of the step is not travelled, so nothing
    // further along it is met. Equal shares go to the earlier solid (the
    // area's by index, then the moving in their order).
    std::optional<swept_meeting> first;
    const solid* first_solid = nullptr;
    std::optional<std::size_t> first_moving;
    const auto sweep_to = [&](const solid& thing, const std::optional<std::size_t> moving_index) {
        if (!meets_ball(thing)) {
            return;
        }
        const std::optional<swept_meeting> met = sweep(previous_position, in, thing);
        if (met && (!first || met->share < first->share)) {
            first = met;
            first_solid = &thing;
            first_moving = moving_index;
        }
    };
    for (const std::uint32_t index : near) {
        sweep_to(solids[index], std::nullopt);
    }
    for (std::size_t i = 0; i < moving_met; ++i) {
        sweep_to(moving[i], i);
    }
    if (first && moved(in, first->ball)) {
        out.ball = first->ball;
        out.hit = first_solid->kind;
        out.moving_hit = first_moving;
    }

    // Then, where it is, whatever it is in: a solid it began the step in
    // contact with, or a cone. Each pushes it out in turn.
    const auto push_out = [&out](const solid& thing, const std::optional<std::size_t> moving_index) {
        if (!meets_ball(thing) || thing.shape == solid_shape::wall) {
            return;
        }
        const ball_state before = out.ball;
        out.ball = resolve_solid_contact(out.ball, thing);
        if (!out.hit && moved(before, out.ball)) {
            out.hit = thing.kind;
            out.moving_hit = moving_index;
        }
    };
    for (const std::uint32_t index : near) {
        push_out(solids[index], std::nullopt);
    }
    for (std::size_t i = 0; i < moving_met; ++i) {
        push_out(moving[i], i);
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
    float within = 0.0f;
    for (int i = 0; i < samples; ++i) {
        const glm::vec3 point = from + (to - from) * ((static_cast<float>(i) + 0.5f) / static_cast<float>(samples));
        float densest = 0.0f;
        for (const std::uint32_t index : near) {
            if (solids[index].porous && inside(solids[index], point)) {
                densest = std::max(densest, solids[index].density);
            }
        }
        within += densest;
    }
    return length * within / static_cast<float>(samples);
}
