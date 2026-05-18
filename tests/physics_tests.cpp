#include "physics/ball_physics.h"
#include "physics/wind.h"

#include <cassert>
#include <cmath>

#include <glm/geometric.hpp>

namespace {
bool near_vec3(const glm::vec3& a, const glm::vec3& b, const float eps = 1e-5f) {
    return glm::length(a - b) <= eps;
}
}

int main() {
    {
        ball_state b;
        b.velocity = glm::vec3(50.0f, 0.0f, 0.0f);
        b.spin = glm::vec3(0.0f, 0.0f, 0.0f);
        b.position = glm::vec3(0.0f, 0.0f, 0.0f);

        wind_state w;
        w.velocity = glm::vec3(0.0f, 0.0f, 0.0f);

        const ball_state b2 = step(b, w, 0.016f);
        assert(glm::length(b2.velocity) < glm::length(b.velocity));
    }

    {
        ball_state b;
        b.velocity = glm::vec3(10.0f, 5.0f, -3.0f);
        b.spin = glm::vec3(0.2f, 0.1f, 0.0f);
        b.position = glm::vec3(1.0f, 2.0f, 3.0f);

        wind_state w;
        w.velocity = glm::vec3(0.0f, 0.0f, 0.0f);

        const ball_state r1 = step(b, w, 0.016f);
        const ball_state r2 = step(b, w, 0.016f);

        assert(near_vec3(r1.position, r2.position));
        assert(near_vec3(r1.velocity, r2.velocity));
    }

    {
        const wind_state w1 = sample_wind(42U, 10.0f);
        const wind_state w2 = sample_wind(42U, 10.0f);
        assert(near_vec3(w1.velocity, w2.velocity));
    }

    return 0;
}
