#pragma once

// How far the trees' sway has moved since a shadow image of them was drawn
// (tree_pose.glsl: wind * 0.004 * scale.y * local.y^2 * gust, with gust
// 0.6 + 0.4 sin(1.3 seconds + place)). The image stays good while no crown
// can have moved by more than a tolerance, so a gentle wind redraws it
// every so often and a calm day never. GL-free.

#include <cmath>

#include <glm/geometric.hpp>
#include <glm/vec2.hpp>

// The gust's fastest change per second: 0.4 * 1.3.
inline constexpr float tree_gust_rate = 0.52f;

class tree_pose_drift {
public:
    // True when no tree's leaf, whose pose moves `reach_per_speed` metres per
    // m/s of wind, has moved more than `tolerance` metres since remember().
    // The move is at most reach * (change in wind + wind * gust rate * seconds).
    bool within(const float seconds, const glm::vec2& wind, const float reach_per_speed, const float tolerance) const {
        if (!known_) {
            return false;
        }
        const float change = glm::length(wind - wind_) + glm::length(wind_) * tree_gust_rate * std::fabs(seconds - seconds_);
        return reach_per_speed * change <= tolerance;
    }
    void remember(const float seconds, const glm::vec2& wind) {
        seconds_ = seconds;
        wind_ = wind;
        known_ = true;
    }
    void forget() { known_ = false; }

private:
    float seconds_ = 0.0f;
    glm::vec2 wind_{0.0f};
    bool known_ = false;
};
