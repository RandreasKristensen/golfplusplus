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

physics_tuning with_water_drag(const physics_tuning& tuning) {
    physics_tuning wet = tuning;
    wet.drag_coeff += tuning.water_drag_coeff;
    wet.spin_decay += tuning.water_spin_decay;
    return wet;
}

ball_state apply_rolling_friction(const ball_state& ball,
                                  const terrain_sample& terrain,
                                  const float deceleration,
                                  const float settle_speed,
                                  const float dt) {
    if (terrain.material == terrain_material::water || !ball_is_grounded(ball, terrain)) {
        return ball;
    }

    const glm::vec3 normal = ground_normal(terrain.normal);
    const float normal_speed = glm::dot(ball.velocity, normal);
    if (normal_speed > settle_speed) {
        return ball;
    }

    ball_state out = ball;
    const glm::vec3 tangent_velocity = ball.velocity - normal * normal_speed;
    const float speed = glm::length(tangent_velocity);
    if (speed <= 0.0f) {
        out.velocity = glm::vec3(0.0f);
        return out;
    }

    const float slowed = std::max(0.0f, speed - std::max(0.0f, deceleration) * dt);
    out.velocity = tangent_velocity * (slowed / speed);
    return out;
}

float path_cup_offset(const glm::vec3& start, const glm::vec3& end, const glm::vec3& cup_center) {
    const glm::vec3 segment = horizontal(end - start);
    const float length_squared = glm::dot(segment, segment);
    const float t = length_squared <= 0.000001f
        ? 1.0f
        : clamp01(glm::dot(horizontal(cup_center - start), segment) / length_squared);
    return horizontal_distance(start + (end - start) * t, cup_center);
}
