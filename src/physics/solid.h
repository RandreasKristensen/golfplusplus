#pragma once

// Anything that stands on a course and stops the ball, people or carts: one
// shape with a height, a kind and what it stops. A course's static solids are
// built once with its area (play_area::solids) on a grid; what moves (carts,
// people) is made into solids of the same kinds from game state when needed.
// The ball meets them in flight (resolve_solid_contacts, then
// resolve_swept_walls), a body moving over the ground meets them seen from
// above (physics/body_collision.h). Pure.

#include "physics/ball_state.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

enum class solid_shape {
    cylinder,  // upright and round: `radius` across, from `base` up `height`
    cone,      // upright, `radius` across at `base`, narrowing to its tip `height` up
    wall,      // upright and thin: from `base` to `end` (their feet), `radius` either side, `height` up
    box        // upright: `half_length` along `along` and `half_width` across either side of `base`, `height` up
};

enum class solid_kind {
    trunk,
    leaves,
    pole,       // a sign's, a fence's or a floodlight's
    net,        // a fence's, between two poles
    board,      // a hole sign's
    lamp,       // a floodlight's head
    flagstick,
    tee_box,
    cart,
    person,
    ball  // another's at rest, as of a shot: it never moves
};

// Which mover a solid stops.
enum class solid_mover {
    ball,
    walker,
    cart
};

struct solid {
    solid_shape shape = solid_shape::cylinder;
    solid_kind kind = solid_kind::trunk;
    glm::vec3 base{0.0f};
    glm::vec3 end{0.0f};                  // wall only
    glm::vec3 along{0.0f, 0.0f, 1.0f};   // box only: horizontal unit
    float radius = 0.0f;
    float height = 0.0f;
    float half_length = 0.0f;  // box only
    float half_width = 0.0f;   // box only
    bool stops_ball = false;
    bool stops_walker = false;
    bool stops_cart = false;
    bool walkable = false;  // its top can be stood on
    // The ball flies through it, slowed and turned (a tree's canopy), rather
    // than off it.
    bool porous = false;
    // The ball off it: reflected speed into it kept, sideways speed lost.
    float restitution = 0.0f;
    float friction = 0.0f;
};

bool stops(const solid& thing, solid_mover mover);

// The height of its top.
float solid_top(const solid& thing);

// The indices of solids bucketed by the grid cells their footprints cover,
// so a mover is tested only against those near it.
struct solid_grid {
    float cell_size = 0.0f;
    // The furthest any solid pushes a mover past its own footprint, less the
    // mover's own size: how far beyond a query box a solid can still matter.
    float reach = 0.0f;
    glm::vec2 origin{0.0f};  // the x/z corner of cell (0, 0)
    int columns = 0;
    int rows = 0;
    std::vector<std::vector<std::uint32_t>> cells;  // row-major; each ascending
};

solid_grid build_solid_grid(const std::vector<solid>& solids, float cell_size);

// The highest walkable top over `point` (XZ) among `solids` (`grid` built
// from them), if any. Walkable things stand on the ground with nothing under
// them, so whatever the point's height, their top is the surface there.
std::optional<float> walkable_top_at(const std::vector<solid>& solids, const solid_grid& grid, const glm::vec3& point);

// Every solid whose footprint may reach the XZ box from `low` to `high`
// grown by `margin`, ascending and once each.
std::vector<std::uint32_t> solids_near(const solid_grid& grid, const glm::vec3& low, const glm::vec3& high, float margin);

// The ball after meeting `thing` where it stands (cylinders, cones, boxes):
// pushed out along the contact normal and bounced by its restitution and
// friction. Unchanged when they do not touch.
ball_state resolve_solid_contact(const ball_state& in, const solid& thing);

// The ball after a step from `previous_position` to `in.position` against a
// wall, swept so a fast ball cannot pass through it: stopped where its path
// first comes within a ball radius of it. Unchanged if it never reaches it.
ball_state resolve_swept_wall(const glm::vec3& previous_position, const ball_state& in, const solid& wall);

// What a ball stepping from `previous_position` to `in.position` meets among
// the `solids` that stop it (`grid` built from them), then among the first
// `moving_count` of `moving` (each tested, no grid), in their order: first
// everything standing, then walls swept from where the step began. Porous
// solids it passes through (porous_path).
struct ball_contacts {
    ball_state ball;
    std::optional<solid_kind> hit;  // the first thing that moved it
    std::optional<std::size_t> moving_hit;  // which of `moving` that was, when one of them
};
ball_contacts resolve_ball_contacts(const glm::vec3& previous_position, const ball_state& in,
                                    const std::vector<solid>& solids, const solid_grid& grid,
                                    const std::vector<solid>& moving, std::size_t moving_count);

// How far the path from `from` to `to` runs inside the porous solids among
// `solids` (`grid` built from them): metres through foliage.
float porous_path(const glm::vec3& from, const glm::vec3& to, const std::vector<solid>& solids, const solid_grid& grid);
