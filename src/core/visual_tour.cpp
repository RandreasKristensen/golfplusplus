#include "core/visual_tour.h"

#include "game/hole_data.h"
#include "game/hole_sign.h"
#include "game/play_area.h"
#include "physics/vector_math.h"

#include <algorithm>
#include <cmath>

#include <glm/common.hpp>

namespace {
const tour_stop& stop_of(const tour_state& state, const visual_tour& tour) {
    return tour.stops[state.stop % tour.stops.size()];
}

// The ground at `point`'s place.
float ground_at(const play_area& area, const glm::vec3& point) {
    return sample_area(area, point).point.y;
}

// Every hole's line of play in order, closed back to the first tee.
std::vector<glm::vec3> course_route(const game_state& game) {
    std::vector<glm::vec3> route;
    const std::size_t holes = std::min(game.course_holes.size(), game.world.hole_starts.size());
    for (std::size_t i = 0; i < holes; ++i) {
        const std::vector<glm::vec3> line = line_of_play(place_hole(game.course_holes[i], game.world.hole_starts[i]));
        route.insert(route.end(), line.begin(), line.end());
    }
    if (!route.empty()) {
        route.push_back(route.front());
    }
    return route;
}

// `values` taken in a ring, each the largest within `reach` of it.
std::vector<float> ring_max(const std::vector<float>& values, const int reach) {
    const int count = static_cast<int>(values.size());
    std::vector<float> out(values.size());
    for (int i = 0; i < count; ++i) {
        float most = values[static_cast<std::size_t>(i)];
        for (int k = -reach; k <= reach; ++k) {
            most = std::max(most, values[static_cast<std::size_t>(((i + k) % count + count) % count)]);
        }
        out[static_cast<std::size_t>(i)] = most;
    }
    return out;
}

// `values` taken in a ring, each the mean of those within `reach` of it.
std::vector<float> ring_mean(const std::vector<float>& values, const int reach) {
    const int count = static_cast<int>(values.size());
    std::vector<float> out(values.size());
    for (int i = 0; i < count; ++i) {
        float sum = 0.0f;
        for (int k = -reach; k <= reach; ++k) {
            sum += values[static_cast<std::size_t>(((i + k) % count + count) % count)];
        }
        out[static_cast<std::size_t>(i)] = sum / static_cast<float>(2 * reach + 1);
    }
    return out;
}

// `points` (evenly spaced round a ring `length` long) at `distance` along it.
glm::vec3 along_ring(const std::vector<glm::vec3>& points, const float length, const float distance) {
    const float spacing = length / static_cast<float>(points.size());
    const float wrapped = std::fmod(std::fmod(distance, length) + length, length);
    const float steps = wrapped / spacing;
    const std::size_t index = static_cast<std::size_t>(steps) % points.size();
    return glm::mix(points[index], points[(index + 1) % points.size()], steps - std::floor(steps));
}
}

tour_path make_tour_path(const game_state& game, const tour_flight& flight) {
    tour_path path;
    const std::vector<glm::vec3> route = course_route(game);
    const float length = polyline_length(route);
    if (length <= 0.0f) {
        return path;
    }
    const std::size_t count = std::max<std::size_t>(1, static_cast<std::size_t>(length / tour_path_spacing));
    const float spacing = length / static_cast<float>(count);
    std::vector<float> lowest(count);
    for (std::size_t i = 0; i < count; ++i) {
        glm::vec3 point = point_along(route, spacing * static_cast<float>(i));
        point.y = ground_at(game.area, point);
        path.ground.push_back(point);
        lowest[i] = point.y + flight.height_meters;
    }
    // Over every tree top the flight passes over, by its crown's reach.
    for (const tree_instance& tree : game.area.trees) {
        const float top = ground_at(game.area, tree.position) + tree.shape.trunk_height + tree.shape.leaf_height +
            flight.tree_clearance_meters;
        const float reach = tree.shape.leaf_radius + spacing;
        for (std::size_t i = 0; i < count; ++i) {
            if (horizontal_distance(path.ground[i], tree.position) < reach) {
                lowest[i] = std::max(lowest[i], top);
            }
        }
    }
    const int ease = static_cast<int>(std::ceil(flight.ease_meters / spacing));
    const std::vector<float> heights = ring_mean(ring_max(lowest, ease), ease);
    for (std::size_t i = 0; i < count; ++i) {
        path.eye.push_back(glm::vec3(path.ground[i].x, heights[i], path.ground[i].z));
    }
    path.length = length;
    return path;
}

tour_state start_tour(const game_state& game, const visual_tour& tour, const std::size_t first) {
    tour_state state;
    state.path = make_tour_path(game, tour.flight);
    state.stop = first % tour.stops.size();
    state.from_hour = tour.stops[state.stop].hour;
    return state;
}

tour_state advance_tour(tour_state state, const visual_tour& tour, const float dt, const bool next) {
    state.stop_seconds += dt;
    if (next || state.stop_seconds >= tour.stop_seconds) {
        state.from_hour = *tour_sky(state, tour).pinned_hour;
        state.stop = (state.stop + 1) % tour.stops.size();
        state.stop_seconds = 0.0f;
    }
    if (state.path.length > 0.0f) {
        state.travelled = std::fmod(state.travelled + tour.flight.speed_meters_per_second * dt, state.path.length);
    }
    return state;
}

camera_view tour_camera(const tour_state& state, const tour_flight& flight) {
    camera_view view;
    if (state.path.eye.empty()) {
        return view;
    }
    view.position = along_ring(state.path.eye, state.path.length, state.travelled);
    view.target = along_ring(state.path.ground, state.path.length, state.travelled + flight.look_ahead_meters) +
        glm::vec3(0.0f, flight.look_height_meters, 0.0f);
    return view;
}

sky_controls tour_sky(const tour_state& state, const visual_tour& tour) {
    constexpr float day_hours = 24.0f;
    const float ahead = std::fmod(stop_of(state, tour).hour - state.from_hour + day_hours, day_hours);
    const float t = tour.transition_seconds > 0.0f ? std::min(state.stop_seconds / tour.transition_seconds, 1.0f) : 1.0f;
    sky_controls sky;
    sky.pinned_hour = std::fmod(state.from_hour + ahead * t * t * (3.0f - 2.0f * t), day_hours);
    sky.pinned_date = tour.date;
    return sky;
}

weather_state tour_weather(const tour_state& state, const visual_tour& tour, const weather_state& live,
                           const weather_tuning& tuning) {
    const tour_stop& stop = stop_of(state, tour);
    weather_state held = coupled_weather(with_weather_kind(live, stop.weather, tuning), tuning);
    held.wetness = stop.wetness;
    return held;
}

std::string tour_title(const tour_state& state, const visual_tour& tour, const text_assets& text) {
    return state.stop_seconds < tour.title_seconds ? lookup_text(text, stop_of(state, tour).title.c_str()) : std::string();
}
