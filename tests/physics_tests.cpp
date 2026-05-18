#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "game/game_tuning.h"
#include "physics/ball_physics.h"
#include "physics/collision.h"
#include "physics/wind.h"

#include <glm/geometric.hpp>

namespace {
bool near_vec3(const glm::vec3& a, const glm::vec3& b, const float eps = 1e-5f) {
    return glm::length(a - b) <= eps;
}
}

TEST_CASE("ball decelerates under aerodynamic drag") {
    ball_state b;
    b.velocity = glm::vec3(50.0f, 0.0f, 0.0f);
    b.spin = glm::vec3(0.0f, 0.0f, 0.0f);
    b.position = glm::vec3(0.0f, 0.0f, 0.0f);

    wind_state w;
    w.velocity = glm::vec3(0.0f, 0.0f, 0.0f);

    const game_tuning tuning = default_game_tuning();
    const ball_state b2 = step(b, w, 0.016f, tuning.physics);
    CHECK(glm::length(b2.velocity) < glm::length(b.velocity));
}

TEST_CASE("step is deterministic") {
    ball_state b;
    b.velocity = glm::vec3(10.0f, 5.0f, -3.0f);
    b.spin = glm::vec3(0.2f, 0.1f, 0.0f);
    b.position = glm::vec3(1.0f, 2.0f, 3.0f);

    wind_state w;
    w.velocity = glm::vec3(0.0f, 0.0f, 0.0f);

    const game_tuning tuning = default_game_tuning();
    const ball_state r1 = step(b, w, 0.016f, tuning.physics);
    const ball_state r2 = step(b, w, 0.016f, tuning.physics);

    CHECK(near_vec3(r1.position, r2.position));
    CHECK(near_vec3(r1.velocity, r2.velocity));
}

TEST_CASE("wind is deterministic") {
    const game_tuning tuning = default_game_tuning();
    const wind_state w1 = sample_wind(42U, 10.0f, tuning.wind);
    const wind_state w2 = sample_wind(42U, 10.0f, tuning.wind);
    CHECK(near_vec3(w1.velocity, w2.velocity));
}

TEST_CASE("wind varies with time and seed") {
    const game_tuning tuning = default_game_tuning();
    const wind_state w_time_a = sample_wind(42U, 0.0f, tuning.wind);
    const wind_state w_time_b = sample_wind(42U, 3.0f, tuning.wind);
    CHECK(!near_vec3(w_time_a.velocity, w_time_b.velocity));

    const wind_state w_seed_a = sample_wind(1U, 0.0f, tuning.wind);
    const wind_state w_seed_b = sample_wind(2U, 0.0f, tuning.wind);
    CHECK(!near_vec3(w_seed_a.velocity, w_seed_b.velocity));
}

TEST_CASE("ground collision bounces and applies friction") {
    ball_state b;
    b.position = glm::vec3(0.0f, -1.0f, 0.0f);
    b.velocity = glm::vec3(2.0f, -5.0f, 3.0f);

    const ball_state r = resolve_ground_collision(b, 0.0f, 0.8f, 0.3f);
    CHECK(r.position.y == 0.0f);
    CHECK(r.velocity.y > 0.0f);
    CHECK(r.velocity.x < 2.0f);
    CHECK(r.velocity.z < 3.0f);
}
