#pragma once

// A shot's flight line, mine or another player's, drawn alike for everyone:
// it follows the ball, keeps its last flight_path.max_points points, and
// fades over flight_path.fade_seconds once the ball rests.

#include "game/game_tuning.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <cstddef>
#include <vector>

struct flight_line {
    std::vector<glm::vec3> points;
    float rested_seconds = 0.0f;  // since the ball came to rest
};

inline void extend_flight_line(flight_line& line, const glm::vec3& point, const flight_path_tuning& tuning) {
    std::vector<glm::vec3>& points = line.points;
    if (!points.empty() && glm::length(point - points.back()) < tuning.min_point_spacing) {
        return;
    }
    points.push_back(point);
    const std::size_t max_points = static_cast<std::size_t>(std::max(2, tuning.max_points));
    if (points.size() > max_points) {
        points.erase(points.begin(), points.begin() + static_cast<std::ptrdiff_t>(points.size() - max_points));
    }
}

inline bool flight_line_faded(const flight_line& line, const flight_path_tuning& tuning) {
    return line.rested_seconds >= tuning.fade_seconds;
}

inline float flight_line_alpha(const flight_line& line, const flight_path_tuning& tuning) {
    if (line.rested_seconds <= 0.0f) {
        return tuning.alpha;
    }
    return tuning.fade_seconds > 0.0f ?tuning.alpha * std::max(0.0f, 1.0f - line.rested_seconds / tuning.fade_seconds) : 0.0f;
}
