#include "core/app.h"

#include "core/event_loop.h"
#include "game/hole_data.h"
#include "physics/terrain.h"

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

namespace {
constexpr float pi = 3.14159265358979323846f;

glm::vec3 aim_direction(const float aim_angle) {
    return glm::normalize(glm::vec3(std::sin(aim_angle), 0.0f, std::cos(aim_angle)));
}

float radians(const float degrees) {
    return degrees * pi / 180.0f;
}

float terrain_height_at(const game_tuning& tuning, const glm::vec3& position) {
    return sample_terrain_mesh(tuning.terrain_mesh_data, position, tuning.ground_y).point.y;
}

render_material_type render_type_for_material(const material_zone_type type) {
    switch (type) {
    case material_zone_type::green:
        return render_material_type::green;
    case material_zone_type::bunker:
        return render_material_type::bunker;
    case material_zone_type::water:
        return render_material_type::water;
    default:
        return render_material_type::unknown;
    }
}

glm::vec3 render_color_for_terrain_material(const terrain_material material, const float distance_from_center, const float width) {
    const float half_width = std::max(0.001f, width * 0.5f);
    const float edge_amount = std::max(0.0f, std::min(1.0f, std::abs(distance_from_center) / half_width));

    switch (material) {
    case terrain_material::fairway:
        return glm::vec3(0.17f + edge_amount * 0.04f,
                         0.44f - edge_amount * 0.08f,
                         0.17f + edge_amount * 0.02f);
    case terrain_material::rough:
        return glm::vec3(0.12f, 0.27f, 0.12f);
    default:
        return glm::vec3(0.16f, 0.38f, 0.16f);
    }
}

std::vector<render_terrain_vertex> make_render_terrain_vertices(const terrain_mesh& mesh) {
    std::vector<render_terrain_vertex> vertices;
    vertices.reserve(mesh.vertices.size());

    for (const terrain_vertex& vertex : mesh.vertices) {
        render_terrain_vertex render_vertex;
        render_vertex.position = vertex.position;
        render_vertex.normal = vertex.normal;
        render_vertex.color = render_color_for_terrain_material(vertex.material, vertex.distance_from_center, mesh.width);
        vertices.push_back(render_vertex);
    }

    return vertices;
}

std::vector<render_material_zone> make_render_zones(const std::vector<material_zone>& zones) {
    std::vector<render_material_zone> render_zones;
    render_zones.reserve(zones.size());

    for (const material_zone& zone : zones) {
        render_material_zone render_zone;
        render_zone.type = render_type_for_material(zone.type);
        render_zone.center = zone.center;
        render_zone.radius = zone.radius;
        render_zone.bounds_min = zone.bounds_min;
        render_zone.bounds_max = zone.bounds_max;
        render_zone.has_radius = zone.has_radius;
        render_zone.has_bounds = zone.has_bounds;
        render_zones.push_back(render_zone);
    }

    return render_zones;
}

void set_walking_camera(render_data& data, const game_state& game) {
    const glm::vec3 forward = aim_direction(game.player.yaw);
    data.camera_position = game.player.position + glm::vec3(0.0f, 1.65f, 0.0f);
    data.camera_target = data.camera_position + forward * 10.0f;
}

void set_aiming_camera(render_data& data, const game_state& game) {
    const glm::vec3 forward = aim_direction(game.aim_angle);
    data.camera_position = game.ball.position - forward * 2.0f + glm::vec3(0.0f, 1.55f, 0.0f);
    data.camera_target = game.ball.position + forward * 12.0f + glm::vec3(0.0f, 1.05f, 0.0f);
}

void set_address_camera(render_data& data, const game_state& game) {
    const glm::vec3 forward = aim_direction(game.aim_angle);
    const glm::vec3 left = glm::normalize(glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), forward));
    data.camera_position = game.ball.position + left * 2.4f - forward * 0.7f + glm::vec3(0.0f, 2.0f, 0.0f);
    data.camera_target = game.ball.position + forward * 0.7f + glm::vec3(0.0f, 0.25f, 0.0f);
}

void set_follow_camera(render_data& data, const game_state& game) {
    const glm::vec3 target(game.ball.position.x, std::max(0.5f, game.ball.position.y), game.ball.position.z);
    data.camera_position = game.shot_camera_position;
    data.camera_target = target;
}

std::vector<glm::vec3> estimate_aim_arc(const game_state& game) {
    std::vector<glm::vec3> points;
    if (game.selected_club >= game.tuning.clubs.size()) {
        return points;
    }

    const club_stats& club = game.tuning.clubs[game.selected_club].stats;
    const glm::vec3 forward = aim_direction(game.aim_angle);
    const float loft = radians(club.loft_degrees);
    const glm::vec3 launch_dir = glm::normalize(forward * std::cos(loft) + glm::vec3(0.0f, std::sin(loft), 0.0f));
    const glm::vec3 velocity = launch_dir * club.power;

    constexpr float gravity = -9.81f;
    constexpr float step_seconds = 0.18f;
    constexpr int max_points = 28;
    for (int i = 1; i <= max_points; ++i) {
        const float t = static_cast<float>(i) * step_seconds;
        glm::vec3 point = game.ball.position + velocity * t + glm::vec3(0.0f, 0.5f * gravity * t * t, 0.0f);
        const float terrain_height = terrain_height_at(game.tuning, point);
        if (point.y < terrain_height) {
            point.y = terrain_height;
            points.push_back(point);
            break;
        }
        points.push_back(point);
    }

    return points;
}

render_data make_render_data(const game_state& game) {
    render_data data;
    data.ball_position = game.ball.position;
    data.tee_position = game.tuning.course.tee_position;
    data.pin_position = game.tuning.course.pin_position;
    data.cup_radius = game.tuning.course.cup_radius;
    data.ball_visual_radius_meters = game.tuning.scale.ball_visual_radius_meters;
    data.cup_visual_radius_meters = game.tuning.scale.cup_visual_radius_meters;
    data.pin_visual_height_meters = game.tuning.scale.pin_visual_height_meters;
    data.course_extent = game.tuning.course.extent;
    data.terrain_vertices = make_render_terrain_vertices(game.tuning.terrain_mesh_data);
    data.terrain_indices = game.tuning.terrain_mesh_data.indices;
    data.material_zones = make_render_zones(game.tuning.course.material_zones);
    data.aim_angle = game.aim_angle;
    if (game.mode == game_mode::aiming) {
        data.aim_arc_points = estimate_aim_arc(game);
    }
    data.ball_moving = ball_is_moving(game.ball, game.tuning);
    data.show_interact_prompt = game.mode == game_mode::walking && can_interact_with_ball(game);
    data.show_aim_indicator = game.mode == game_mode::aiming || game.mode == game_mode::addressing;
    data.swing_timing = game.swing.phase == swing_phase::timing;
    data.swing_power = game.swing.power;
    data.stroke_count = game.stroke_count;
    if (game.selected_club < game.tuning.clubs.size()) {
        data.selected_club_label = game.tuning.clubs[game.selected_club].label;
    }

    if (game.mode == game_mode::walking) {
        set_walking_camera(data, game);
    } else if (game.mode == game_mode::aiming) {
        set_aiming_camera(data, game);
    } else if (game.mode == game_mode::addressing) {
        set_address_camera(data, game);
    } else {
        set_follow_camera(data, game);
    }

    return data;
}
}

bool app::init() {
    if (!window_.init("vcr-golf", 1280, 720)) {
        return false;
    }

    if (!renderer_.init(window_.sdl_window())) {
        window_.shutdown();
        return false;
    }

    game_ = make_initial_game_state();
    running_ = true;
    return true;
}

void app::run() {
    Uint64 previous_counter = SDL_GetPerformanceCounter();
    const double performance_frequency = static_cast<double>(SDL_GetPerformanceFrequency());

    while (running_) {
        const Uint64 current_counter = SDL_GetPerformanceCounter();
        const double elapsed = static_cast<double>(current_counter - previous_counter) / performance_frequency;
        previous_counter = current_counter;
        const float dt = std::min(static_cast<float>(elapsed), 0.05f);

        input_.reset_frame();
        poll_events(input_);

        if (input_.quit_requested) {
            running_ = false;
        }

        update_game(game_, input_, dt);
        renderer_.render(make_render_data(game_));
        window_.swap();
    }
}

void app::shutdown() {
    renderer_.shutdown();
    window_.shutdown();
}
