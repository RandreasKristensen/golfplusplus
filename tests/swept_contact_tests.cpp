#include "doctest.h"

#include "game/shot_simulation.h"
#include "physics/solid.h"

#include "test_support.h"

#include <optional>
#include <vector>

#include <glm/geometric.hpp>

namespace {
// The shipped ball and flagstick (assets/play/tuning/ball.json).
constexpr float ball_radius = 0.021335f;
constexpr float pin_radius = 0.00635f;

solid flagstick_at(const glm::vec3& base) {
    solid pin = test_solid(solid_shape::cylinder, solid_kind::flagstick, base, pin_radius, 2.13f);
    pin.restitution = 0.3f;
    pin.friction = 0.1f;
    return pin;
}

ball_state ball_at(const glm::vec3& position, const glm::vec3& velocity) {
    ball_state ball;
    ball.radius = ball_radius;
    ball.position = position;
    ball.velocity = velocity;
    return ball;
}

// A ball flown in straight fixed steps from `start` at `velocity` among
// `solids` (and `moving`), until something turns it or it has gone `metres`.
struct flown {
    ball_contacts met;
    glm::vec3 from{0.0f};  // where the step that met it began
    int steps = 0;
};

flown fly(const std::vector<solid>& solids, const std::vector<solid>& moving, const glm::vec3& start,
          const glm::vec3& velocity, const float metres) {
    const solid_grid grid = build_solid_grid(solids, 8.0f);
    ball_state ball = ball_at(start, velocity);
    flown out{ball_contacts{ball, std::nullopt, std::nullopt}};
    const int steps = static_cast<int>(metres / (glm::length(velocity) * shot_step_seconds)) + 1;
    for (int i = 0; i < steps; ++i) {
        out.from = ball.position;
        ball_state stepped = ball;
        stepped.position += velocity * shot_step_seconds;
        out.met = resolve_ball_contacts(out.from, stepped, solids, grid, moving, moving.size());
        out.steps = i + 1;
        if (out.met.hit) {
            return out;
        }
        ball = out.met.ball;
    }
    return out;
}
}

TEST_CASE("a real flagstick turns a ball at 8, 12 and 50 m/s whatever the step it arrives on") {
    const std::vector<solid> pin{flagstick_at(glm::vec3(0.0f))};
    for (const float speed : {8.0f, 12.0f, 50.0f}) {
        const float step = speed * shot_step_seconds;
        for (int phase = 0; phase < 16; ++phase) {
            // Head on and a little off centre, a metre up.
            for (const float off : {0.0f, 0.015f, -0.024f}) {
                const glm::vec3 start(off, 1.0f, -1.0f - step * static_cast<float>(phase) / 16.0f);
                const flown hit = fly(pin, {}, start, glm::vec3(0.0f, 0.0f, speed), 2.0f);
                REQUIRE(hit.met.hit == solid_kind::flagstick);
                // Stopped on its own side, never through it, and turned.
                CHECK(hit.met.ball.position.z < 0.0f);
                CHECK(glm::length(horizontal(hit.met.ball.position)) >= pin_radius + ball_radius);
                CHECK(hit.met.ball.velocity.z < 0.8f * speed);
                CHECK(hit.met.ball.position.z >= hit.from.z);
                if (off == 0.0f) {
                    CHECK(hit.met.ball.velocity.z < 0.0f);  // straight back
                } else {
                    CHECK(hit.met.ball.velocity.x * off > 0.0f);  // off to its side
                }
            }
        }
    }
}

TEST_CASE("a ball that only comes close to a flagstick, or passes over it, is left alone") {
    const std::vector<solid> pin{flagstick_at(glm::vec3(0.0f))};
    const float reach = pin_radius + ball_radius;
    for (const float speed : {8.0f, 12.0f, 50.0f}) {
        for (int phase = 0; phase < 16; ++phase) {
            const float shift = speed * shot_step_seconds * static_cast<float>(phase) / 16.0f;
            // Glancing past, a millimetre clear.
            const glm::vec3 beside(reach + 0.001f, 1.0f, -1.0f - shift);
            CHECK(!fly(pin, {}, beside, glm::vec3(0.0f, 0.0f, speed), 2.0f).met.hit);
            // Over its top, falling a little but never to it.
            const glm::vec3 over(0.0f, 2.13f + ball_radius + 0.02f, -1.0f - shift);
            CHECK(!fly(pin, {}, over, glm::vec3(0.0f, -0.1f, speed), 2.0f).met.hit);
        }
    }
}

TEST_CASE("a fast ball cannot pass through a thin trunk or a thin board between steps") {
    solid trunk = test_post(glm::vec3(0.0f), 0.12f, 4.0f);
    trunk.kind = solid_kind::trunk;
    trunk.restitution = 0.25f;
    trunk.friction = 0.35f;
    // A sign's board 2 cm thick, 1 m wide along x, across the ball's way.
    solid board = test_box(glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f), 0.5f, 0.01f, 1.5f);
    board.kind = solid_kind::board;
    board.restitution = 0.4f;
    board.friction = 0.2f;
    const glm::vec3 square(0.0f, 0.0f, 50.0f);
    const glm::vec3 slanting(20.0f, 3.0f, 45.0f);
    // Where each way crosses z = 0: through the trunk, through the board's
    // face, and just past the board's end, where only its rounded edge is.
    const struct {
        const solid& thing;
        glm::vec3 velocity;
        float cross_x;
    } ways[] = {{trunk, square, 0.03f}, {trunk, slanting, 0.03f}, {board, square, 0.1f}, {board, slanting, 0.51f}};
    for (const auto& way : ways) {
        for (int phase = 0; phase < 16; ++phase) {
            const float speed = glm::length(way.velocity);
            const glm::vec3 aim = way.velocity / speed;
            const glm::vec3 start = glm::vec3(way.cross_x, 1.0f, 0.0f) -
                aim * (2.0f + speed * shot_step_seconds * static_cast<float>(phase) / 16.0f);
            const flown hit = fly({way.thing}, {}, start, way.velocity, 4.0f);
            REQUIRE(hit.met.hit == way.thing.kind);
            CHECK(hit.met.ball.position.z < 0.0f);  // on the side it came from
            CHECK(glm::dot(hit.met.ball.velocity, aim) < 0.9f * speed);  // turned
            // Left touching it, not in it.
            const ball_state touching = resolve_solid_contact(hit.met.ball, way.thing);
            CHECK(touching.position == hit.met.ball.position);
        }
    }
}

TEST_CASE("a solid among the area's and the same solid moving meet the ball alike") {
    solid pin = flagstick_at(glm::vec3(0.4f, 0.0f, 3.0f));
    solid board = test_box(glm::vec3(-0.6f, 0.0f, 6.0f), glm::normalize(glm::vec3(1.0f, 0.0f, 0.3f)), 0.4f, 0.02f, 1.2f);
    board.kind = solid_kind::board;
    board.restitution = 0.4f;
    board.friction = 0.2f;
    for (const solid& thing : {pin, board}) {
        for (int i = 0; i < 24; ++i) {
            const glm::vec3 velocity(static_cast<float>(i % 5) - 2.0f, 0.3f, 12.0f + 2.0f * static_cast<float>(i));
            const glm::vec3 start(thing.base.x - velocity.x * 0.25f + 0.004f * static_cast<float>(i), 1.0f,
                                  thing.base.z - velocity.z * 0.25f);
            const flown standing = fly({thing}, {}, start, velocity, 8.0f);
            const flown moving = fly({}, {thing}, start, velocity, 8.0f);
            CHECK(standing.steps == moving.steps);
            CHECK(standing.met.hit == moving.met.hit);
            CHECK(standing.met.ball.position == moving.met.ball.position);
            CHECK(standing.met.ball.velocity == moving.met.ball.velocity);
            CHECK(!standing.met.moving_hit);
            CHECK(moving.met.moving_hit == (moving.met.hit ? std::optional<std::size_t>(0) : std::nullopt));
        }
    }
}

TEST_CASE("the first solid along the step meets the ball, whatever their order; on a tie, the earlier listed") {
    // Two flagsticks a step apart across a 50 m/s ball's path: the nearer, from either order.
    const solid near_pin = flagstick_at(glm::vec3(0.0f, 0.0f, 0.1f));
    solid far_pin = flagstick_at(glm::vec3(0.01f, 0.0f, 0.3f));
    far_pin.kind = solid_kind::pole;
    const glm::vec3 from(0.0f, 1.0f, -0.05f);
    ball_state after = ball_at(from + glm::vec3(0.0f, 0.0f, 0.42f), glm::vec3(0.0f, 0.0f, 50.0f));
    for (const std::vector<solid>& solids : {std::vector<solid>{near_pin, far_pin}, std::vector<solid>{far_pin, near_pin}}) {
        const ball_contacts met = resolve_ball_contacts(from, after, solids, build_solid_grid(solids, 8.0f), {}, 0);
        CHECK(met.hit == solid_kind::flagstick);
        CHECK(met.ball.position.z < 0.1f);
        // Both ways round, and moving rather than standing, the same.
        const ball_contacts moving = resolve_ball_contacts(from, after, {}, build_solid_grid({}, 8.0f), solids, 2);
        CHECK(moving.hit == solid_kind::flagstick);
        CHECK(moving.ball.position == met.ball.position);
        CHECK(moving.ball.velocity == met.ball.velocity);
    }

    // The same flagstick twice, standing and moving: met at once, the area's first.
    solid twin = near_pin;
    twin.kind = solid_kind::pole;
    const std::vector<solid> standing{twin};
    const ball_contacts tie = resolve_ball_contacts(from, after, standing, build_solid_grid(standing, 8.0f), {near_pin}, 1);
    CHECK(tie.hit == solid_kind::pole);
    CHECK(!tie.moving_hit);
    // Among the moving, the earlier.
    const ball_contacts moving_tie =
        resolve_ball_contacts(from, after, {}, build_solid_grid({}, 8.0f), {near_pin, twin}, 2);
    CHECK(moving_tie.hit == solid_kind::flagstick);
    CHECK(moving_tie.moving_hit == std::optional<std::size_t>(0));
}
