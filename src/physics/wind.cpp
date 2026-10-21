#include "physics/wind.h"

#include <cstddef>
#include <vector>

wind_state wind_at_height(const glm::vec3& reference_wind, const float height, const wind_profile& profile) {
    const std::vector<wind_profile_point>& points = profile.points;
    float scale = points.empty() || height <= points.front().height ? 0.0f : points.back().scale;
    for (std::size_t i = 1; i < points.size(); ++i) {
        const wind_profile_point& low = points[i - 1];
        const wind_profile_point& high = points[i];
        if (height > low.height && height <= high.height) {
            const float span = high.height - low.height;
            const float t = span > 0.0f ? (height - low.height) / span : 1.0f;
            scale = low.scale + (high.scale - low.scale) * t;
            break;
        }
    }
    return wind_state{reference_wind * scale};
}
