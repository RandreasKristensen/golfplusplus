#include "physics/ground_contact.h"

#include "physics/vector_math.h"

#include <algorithm>

namespace {
constexpr float grounded_tolerance = 0.001f;
}

float ball_support_distance(const ball_state& ball, const terrain_sample& terrain) {
    return glm::dot(ball.position - terrain.point, ground_normal(terrain.normal));
}

bool ball_is_grounded(const ball_state& ball, const terrain_sample& terrain) {
    return ball_support_distance(ball, terrain) <= ball.radius + grounded_tolerance;
}

bool ball_in_water(const ball_state& ball, const terrain_sample& terrain) {
    if (terrain.material != terrain_material::water) {
        return false;
    }
    return ball.position.y - ball.radius <= terrain.water_level;
}
