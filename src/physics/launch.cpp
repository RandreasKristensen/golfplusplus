#include "physics/launch.h"

#include "physics/det_math.h"
#include "physics/vector_math.h"

#include <glm/geometric.hpp>

ball_state launched_ball(const glm::vec3& position, const float radius, const launch_conditions& launch) {
    const glm::vec3 forward = yaw_direction(launch.direction);
    const glm::vec3 left = yaw_left(forward);
    const glm::vec3 along = forward * det_cos(launch.launch_angle) + world_up * det_sin(launch.launch_angle);
    // Backspin turns about the axis to the right of the flight, so it lifts
    // (spin x velocity); tilted towards `upward` about the flight, its lift
    // leans left.
    const glm::vec3 upward = glm::cross(-left, along);
    const glm::vec3 axis = -left * det_cos(launch.spin_axis_tilt) + upward * det_sin(launch.spin_axis_tilt);

    ball_state ball;
    ball.position = position;
    ball.radius = radius;
    ball.velocity = along * launch.ball_speed;
    ball.spin = axis * launch.spin;
    return ball;
}
