#include "crowd_review_check.h"

#include "core/render_frame.h"
#include "game/sky_clock.h"
#include "picture_rig.h"
#include "physics/vector_math.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>

#include <glm/geometric.hpp>

namespace {
constexpr int review_lines = 360;
constexpr int review_window_height = 1080;
constexpr float fov_degrees = 60.0f;

struct review_case {
    const char* name;
    float hour;
    bool winter;
};

struct review_view {
    const char* name;
    rig_camera camera;
};

const std::array<review_case, 3> cases{{{"noon", 13.0f, false}, {"low_sun", 19.5f, false}, {"night", 24.0f, true}}};

// `count` golfers in rows of five from `first` metres ahead of `eye`: every fifth one drives, one in
// seven crouches, they carry bags and clubs, and each has a cart and a ball.
void add_crowd(rig_camera& camera, const game_state& game, const glm::vec3& eye, const glm::vec3& forward, const int count,
               const float first) {
    const glm::vec3 left = yaw_left(forward);
    const float forward_yaw = yaw_towards(glm::vec3(0.0f), forward);
    for (int i = 0; i < count; ++i) {
        const auto id = static_cast<std::uint64_t>(i + 1);
        const float yaw = forward_yaw + 0.4f * static_cast<float>(i % 7) - 1.2f;
        const glm::vec3 point = eye + forward * (first + 4.0f * static_cast<float>(i / 5)) + left * (static_cast<float>(i % 5) - 2.0f) * 3.0f;
        cart_in_world cart;
        cart.owner = id;
        cart.pose.position = anchor_on_terrain(game.area, point + forward * 1.6f);
        cart.pose.yaw = yaw;
        cart.item = game.default_loadout.cart;
        const bool drives = i % 5 == 0;
        cart.driven = drives;
        cart.steer = drives ? 0.3f * std::sin(static_cast<float>(i)) : 0.0f;
        camera.carts.push_back(cart);
        render_remote_avatar figure;
        figure.player_id = id;
        figure.group_id = static_cast<std::uint64_t>(i % 3);
        figure.yaw = yaw;
        figure.seated = drives;
        figure.at_wheel = drives;
        figure.position = drives ? cart.pose.position : anchor_on_terrain(game.area, point);
        figure.crouch = i % 7 == 3 ? 0.7f : 0.0f;
        if (!drives) {
            figure.bag_model = "golf_bag";
            figure.carried_club_model = i % 2 == 0 ? "club_iron" : "club_wood";
        }
        camera.figures.push_back(figure);
        camera.balls.push_back({anchor_on_terrain(game.area, point - forward) + world_up * game.tuning.scale.ball_visual_radius_meters, id});
    }
}

rig_camera looking(const glm::vec3& eye, const glm::vec3& target) {
    rig_camera camera;
    camera.eye = eye;
    camera.target = target;
    camera.fov_degrees = fov_degrees;
    camera.clock_seconds = 3.0f;
    camera.trees = true;
    return camera;
}

std::vector<review_view> views_of(const game_state& game, const render_water& water) {
    std::vector<review_view> views;
    const glm::vec3 up(0.0f, 1.0f, 0.0f);
    const glm::vec3 forward = yaw_direction(game.player.yaw);
    const glm::vec3 eye = game.player.position + up * 1.7f;
    {
        rig_camera crowd = looking(eye, eye + forward * 10.0f - up * 0.5f);
        add_crowd(crowd, game, game.player.position, forward, 40, 10.0f);
        views.push_back({"crowd", crowd});
    }
    {
        rig_camera close = looking(eye, eye + forward * 6.0f - up * 0.6f);
        add_crowd(close, game, game.player.position, forward, 5, 4.0f);
        views.push_back({"close", close});
    }
    if (!water.triangles.empty()) {
        // Standing on the near bank of the largest pond, the crowd beyond its far side.
        glm::vec3 pond(0.0f);
        float largest = 0.0f;
        for (std::size_t i = 2; i < water.triangles.size(); i += 3) {
            const glm::vec3 a = water.triangles[i - 2].position;
            const glm::vec3 b = water.triangles[i - 1].position;
            const glm::vec3 c = water.triangles[i].position;
            const float area = std::abs(glm::cross(b - a, c - a).y);
            if (area > largest) {
                largest = area;
                pond = (a + b + c) / 3.0f;
            }
        }
        float near_side = 0.0f;
        for (const render_water_vertex& vertex : water.triangles) {
            if (std::abs(vertex.position.y - pond.y) < 0.01f) { near_side = std::min(near_side, glm::dot(vertex.position - pond, forward)); }
        }
        const glm::vec3 bank = anchor_on_terrain(game.area, pond + forward * (near_side - 3.0f));
        // The crowd on the shore at the water's edge nearest the bank, at least 10 m off.
        glm::vec3 edge = pond;
        float nearest = 1.0e9f;
        for (const render_water_vertex& vertex : water.triangles) {
            const float apart = glm::length(horizontal(vertex.position - bank));
            if (std::abs(vertex.position.y - pond.y) < 0.01f && apart >= 10.0f && apart < nearest) {
                nearest = apart;
                edge = vertex.position;
            }
        }
        const glm::vec3 outward = safe_normalize(horizontal(edge - pond), forward);
        rig_camera mirror = looking(bank + up * 1.7f, edge + up * 0.5f);
        mirror.ponds = true;
        add_crowd(mirror, game, edge, outward, 15, 2.0f);
        views.push_back({"pond", mirror});
    }
    return views;
}

std::vector<float> floats_of(const std::vector<glm::vec4>& pixels) {
    std::vector<float> values;
    values.reserve(pixels.size() * 3);
    for (const glm::vec4& pixel : pixels) {
        values.insert(values.end(), {pixel.r, pixel.g, pixel.b});
    }
    return values;
}

bool write_floats(const std::filesystem::path& path, const std::vector<float>& values) {
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(values.data()), static_cast<std::streamsize>(values.size() * sizeof(float)));
    return static_cast<bool>(file);
}

std::vector<float> read_floats(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    std::vector<float> values(bytes.size() / sizeof(float));
    std::copy(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(values.size() * sizeof(float)),
              reinterpret_cast<char*>(values.data()));
    return values;
}

bool write_bmp(const std::filesystem::path& path, const std::vector<glm::vec4>& pixels, const int width, const int height) {
    std::vector<std::array<std::uint8_t, 3>> rgb;
    rgb.reserve(pixels.size());
    const auto byte_of = [](const float value) {
        return static_cast<std::uint8_t>(std::lround(255.0f * std::pow(std::clamp(value, 0.0f, 1.0f), 1.0f / 2.2f)));
    };
    for (const glm::vec4& pixel : pixels) { rgb.push_back({byte_of(pixel.r), byte_of(pixel.g), byte_of(pixel.b)}); }
    return write_check_bmp(path, width, height, rgb);
}

// How many pixels differ and by how much at most, in the scene's linear values.
bool compare(const std::string& name, const std::vector<float>& now, const std::vector<float>& before) {
    if (before.size() != now.size() || now.empty()) {
        std::printf("    %-18s no matching picture to compare\n", name.c_str());
        return false;
    }
    std::size_t differing = 0;
    float largest = 0.0f;
    for (std::size_t i = 0; i < now.size(); i += 3) {
        const float apart = std::max({std::abs(now[i] - before[i]), std::abs(now[i + 1] - before[i + 1]), std::abs(now[i + 2] - before[i + 2])});
        differing += apart > 0.0f ? 1U : 0U;
        largest = std::max(largest, apart);
    }
    std::printf("    %-18s %zu of %zu pixels differ, largest %.6f\n", name.c_str(), differing, now.size() / 3, largest);
    return true;
}
}

bool run_crowd_review(const check_course& course, const game_content& content, const lighting_palette& palette,
                      const std::string& asset_root, SDL_Window* window, const crowd_review_options& options) {
    std::error_code made;
    std::filesystem::create_directories(options.out, made);
    picture_rig rig;
    if (!rig.init(window, course, content, palette, asset_root, review_window_height, review_lines)) { return false; }
    const std::vector<review_view> views = views_of(course.game, rig.water());
    bool ok = true;
    std::printf("  crowd review %s%s%s\n", options.label.c_str(), options.against.empty() ? "" : " against ", options.against.c_str());
    for (const review_case& weather : cases) {
        rig.set_conditions(weather.hour, weather.winter, shown_target(course.game.weather));
        for (const review_view& view : views) {
            rig_capture capture;
            // A new pose's grass tiles are fetched over the first frames.
            for (int frame = 0; frame < 4; ++frame) { capture = rig.capture(view.camera); }
            if (capture.scene.empty()) {
                ok = false;
                continue;
            }
            const std::string name = std::string(view.name) + "_" + weather.name;
            const std::vector<float> values = floats_of(capture.scene);
            ok = write_floats(options.out / (options.label + "_" + name + ".f32"), values) && ok;
            ok = write_bmp(options.out / (options.label + "_" + name + ".bmp"), capture.scene, capture.scene_width, capture.scene_height) && ok;
            if (!options.against.empty()) {
                ok = compare(name, values, read_floats(options.out / (options.against + "_" + name + ".f32"))) && ok;
            }
        }
    }
    rig.shutdown();
    return ok;
}
