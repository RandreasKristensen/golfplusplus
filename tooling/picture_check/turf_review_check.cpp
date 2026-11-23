#include "turf_review_check.h"

#include "core/render_frame.h"
#include "grass_motion_check.h"
#include "picture_rig.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <functional>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

namespace {
constexpr int review_lines = 360;
constexpr int review_window_height = 1080;
constexpr float fov_degrees = 60.0f;
constexpr float standing_eye = 1.7f;
constexpr float crouched_eye = 0.45f;
constexpr float pitch_degrees = -3.0f;

struct review_case {
    const char* name;
    float hour;
    bool winter;
    shown_weather weather;
};

struct review_view {
    const char* name;
    rig_camera camera;
};

std::vector<review_case> cases() {
    shown_weather overcast;
    overcast.cloud = 1.0f;
    shown_weather wet;
    wet.wetness = 1.0f;
    return {{"noon", 13.0f, false, {}},   {"low_sun", 19.5f, false, {}}, {"overcast", 13.0f, false, overcast},
            {"wet", 13.0f, false, wet},   {"night", 24.0f, true, {}},    {"wet_night", 24.0f, true, wet}};
}

// Which of these are also written as final TV pictures.
bool saved_as_tv(const std::string& view, const std::string& name) {
    return (view == "putting" && (name == "noon" || name == "wet")) ||
           (view == "fairway" && (name == "noon" || name == "low_sun")) || (view == "approach" && name == "night");
}

glm::vec2 hole_middle(const terrain_mesh& hole, const int row) {
    const int across = hole.cross_section_count;
    const glm::vec3& first = hole.vertices[static_cast<std::size_t>(row * across)].position;
    const glm::vec3& last = hole.vertices[static_cast<std::size_t>(row * across + across - 1)].position;
    return glm::vec2(first.x + last.x, first.z + last.z) * 0.5f;
}

glm::vec2 unit(const glm::vec2& v) { return v / std::max(glm::length(v), 1.0e-6f); }

glm::vec3 on_ground(const play_area& area, const glm::vec2& at) { return anchor_on_terrain(area, glm::vec3(at.x, 0.0f, at.y)); }

rig_camera look_from(const glm::vec3& eye, const glm::vec3& target) {
    rig_camera camera;
    camera.eye = eye;
    camera.target = target;
    camera.fov_degrees = fov_degrees;
    camera.clock_seconds = 3.0f;
    return camera;
}

struct hole_pick {
    std::size_t hole = 0;
    bool found = false;
};

// The first hole with a green floodlight, else the first with a cup.
hole_pick pick_hole(const play_area& area) {
    for (const floodlight& lamp : area.floodlights) {
        if (lamp.target == floodlight_target::green && lamp.area_hole < area.cups.size() && lamp.area_hole < area.holes.size() &&
            area.holes[lamp.area_hole].section_count >= 4) {
            return {lamp.area_hole, true};
        }
    }
    for (std::size_t hole = 0; hole < std::min(area.cups.size(), area.holes.size()); ++hole) {
        if (area.holes[hole].section_count >= 4) { return {hole, true}; }
    }
    return {};
}

std::vector<review_view> views_of(const game_state& game, const hole_pick pick) {
    const play_area& area = game.area;
    const terrain_mesh& hole = area.holes[pick.hole];
    const cup& hole_cup = area.cups[pick.hole];
    const glm::vec2 cup_at(hole_cup.centre.x, hole_cup.centre.z);
    const glm::vec2 back = unit(hole_middle(hole, 0) - cup_at);
    const world_scale_tuning& scale = game.tuning.scale;
    const float rest = scale.ball_physics_radius_meters / scale.meters_per_world_unit;
    const glm::vec3 up(0.0f, 1.0f, 0.0f);
    const auto ball_at = [&](const glm::vec2& at) {
        return drawn_ball_center(on_ground(area, at) + up * rest, scale, area.cups);
    };
    std::vector<review_view> views;
    {
        rig_camera putting = look_from(on_ground(area, cup_at + back * 3.5f) + up * crouched_eye, hole_cup.centre + up * 0.03f);
        putting.ball = ball_at(cup_at + back * 1.4f);
        views.push_back({"putting", putting});
    }
    {
        const glm::vec2 ball = cup_at + back * 10.0f;
        rig_camera address = look_from(on_ground(area, ball + back * 0.9f) + up * 1.55f, on_ground(area, ball));
        address.ball = ball_at(ball);
        views.push_back({"address", address});
    }
    {
        rig_camera in_cup = look_from(on_ground(area, cup_at + back * 0.7f) + up * 0.28f, hole_cup.centre);
        in_cup.ball = drawn_ball_center(hole_cup.centre + up * rest, scale, area.cups);
        views.push_back({"cup", in_cup});
    }
    views.push_back({"approach", look_from(on_ground(area, cup_at + back * 16.0f) + up * standing_eye, hole_cup.centre + up * 0.3f)});
    {
        const int last = hole.section_count - 1;
        const glm::vec2 from = hole_middle(hole, last / 5);
        const glm::vec2 way = unit(hole_middle(hole, last * 4 / 5) - from);
        const glm::vec3 eye = on_ground(area, from) + up * standing_eye;
        const float pitch = glm::radians(pitch_degrees);
        views.push_back({"fairway", look_from(eye, eye + glm::vec3(way.x * std::cos(pitch), std::sin(pitch), way.y * std::cos(pitch)))});
    }
    return views;
}

float luma(const glm::vec4& c) { return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b; }

struct brightness {
    float mean = 0.0f;
    float p99 = 0.0f;
    float clipped = 0.0f;  // share of pixels at or above 0.9 luma
};

// Over the lower half of the scene, where the ground is.
brightness measure_ground(const rig_capture& capture) {
    const std::size_t count = static_cast<std::size_t>(capture.scene_width) * static_cast<std::size_t>(capture.scene_height / 2);
    std::vector<float> values;
    values.reserve(count);
    for (std::size_t i = 0; i < count; ++i) { values.push_back(luma(capture.scene[i])); }
    brightness result;
    if (values.empty()) { return result; }
    double sum = 0.0;
    std::size_t clipped = 0;
    for (const float value : values) {
        sum += value;
        clipped += value >= 0.9f ? 1U : 0U;
    }
    std::sort(values.begin(), values.end());
    result.mean = static_cast<float>(sum / static_cast<double>(values.size()));
    result.p99 = values[std::min(values.size() - 1, values.size() * 99 / 100)];
    result.clipped = static_cast<float>(clipped) / static_cast<float>(values.size());
    return result;
}

std::uint8_t byte_of(const float value, const float gamma) {
    return static_cast<std::uint8_t>(std::lround(255.0f * std::pow(std::clamp(value, 0.0f, 1.0f), gamma)));
}

bool write_picture(const std::filesystem::path& path, const std::vector<glm::vec4>& pixels, const int width, const int height,
                   const float gamma) {
    std::vector<std::array<std::uint8_t, 3>> rgb;
    rgb.reserve(pixels.size());
    for (const glm::vec4& pixel : pixels) { rgb.push_back({byte_of(pixel.r, gamma), byte_of(pixel.g, gamma), byte_of(pixel.b, gamma)}); }
    return write_check_bmp(path, width, height, rgb);
}

using picture_visit = std::function<void(const review_view&, const review_case&, const rig_capture&, const render_lighting&)>;

// Every view under every case on a rig built from `palette`.
bool for_each_picture(const check_course& course, const game_content& content, const lighting_palette& palette,
                      const std::string& root, SDL_Window* window, const std::vector<review_view>& views,
                      const std::vector<review_case>& weathers, const picture_visit& visit) {
    picture_rig rig;
    if (!rig.init(window, course, content, palette, root, review_window_height, review_lines)) { return false; }
    for (const review_case& weather : weathers) {
        rig.set_conditions(weather.hour, weather.winter, weather.weather);
        for (const review_view& view : views) {
            // A new pose's grass tiles are fetched over the first frames.
            rig_capture capture;
            for (int frame = 0; frame < 4; ++frame) { capture = rig.capture(view.camera); }
            if (capture.scene.empty()) {
                rig.shutdown();
                return false;
            }
            visit(view, weather, capture, rig.lighting());
        }
    }
    rig.shutdown();
    return true;
}

// Each part of the night's light taken out in turn, to see which the greens'
// brightness comes from.
struct diagnosis_variant {
    const char* name;
    std::function<void(lighting_palette&)> change;
};

std::vector<diagnosis_variant> diagnosis_variants() {
    return {{"as shipped", [](lighting_palette&) {}},
            {"no wet gloss", [](lighting_palette& p) { p.weather.wet_gloss = 0.0f; }},
            {"floods x0.6", [](lighting_palette& p) { p.night_lights.floodlight.intensity *= 0.6f; }},
            {"floods x0.4", [](lighting_palette& p) { p.night_lights.floodlight.intensity *= 0.4f; }},
            {"no floodlights", [](lighting_palette& p) { p.night_lights.floodlight.intensity = 0.0f; }}};
}

// The floodlights' diffuse light on level ground at `at` (lighting.glsl's
// local_light, level ground), as a luminance: how many lamps reach it and the
// sum of what they give. Printed beside the pictures to show the sum is
// what is bright, not one lamp.
void print_lamp_sum(const play_area& area, const night_light_style& style, const glm::vec3& at, const char* name) {
    const float edge = std::cos(glm::radians(style.floodlight_cone_deg));
    const float full = std::cos(glm::radians(style.floodlight_cone_deg - style.floodlight_soft_deg));
    float sum = 0.0f;
    float strongest = 0.0f;
    int reaching = 0;
    for (const floodlight& lamp : area.floodlights) {
        const glm::vec3 to = lamp.lamp - at;
        const float distance = glm::length(to);
        const glm::vec3 towards = to / std::max(distance, 0.001f);
        const float reach = std::clamp(1.0f - distance * distance / (style.floodlight.radius * style.floodlight.radius), 0.0f, 1.0f);
        const float t = std::clamp((glm::dot(-towards, lamp.aim) - edge) / (full - edge), 0.0f, 1.0f);
        const float cone = t * t * (3.0f - 2.0f * t);
        const float light = style.floodlight.intensity * reach * reach * cone * std::max((towards.y + 0.25f) / 1.25f, 0.0f);
        sum += light;
        strongest = std::max(strongest, light);
        reaching += light > 0.01f ? 1 : 0;
    }
    std::printf("    floodlights on %s: %d reach it, strongest %.2f, sum %.2f (a noon surface gets about 1.0)\n", name, reaching,
                strongest, sum);
}

void diagnose(const check_course& course, const game_content& content, const lighting_palette& palette, const std::string& root,
              SDL_Window* window, const std::vector<review_view>& views) {
    std::vector<review_case> night;
    for (const review_case& weather : cases()) {
        if (weather.winter) { night.push_back(weather); }
    }
    {
        const play_area& area = course.game.area;
        const glm::vec3 centre = area.cups.front().centre;
        print_lamp_sum(area, palette.night_lights, centre, "the first cup");
        for (const review_view& view : views) {
            if (std::string(view.name) == "putting") { print_lamp_sum(area, palette.night_lights, view.camera.target, "the putting view's cup"); }
        }
    }
    std::printf("  night brightness of the lower half of the scene (mean / p99 / share >= 0.9):\n");
    for (const diagnosis_variant& variant : diagnosis_variants()) {
        lighting_palette changed = palette;
        variant.change(changed);
        for_each_picture(course, content, changed, root, window, views, night,
                         [&](const review_view& view, const review_case& weather, const rig_capture& capture, const render_lighting&) {
                             if (std::string(view.name) != "putting" && std::string(view.name) != "approach") { return; }
                             const brightness b = measure_ground(capture);
                             std::printf("    %-15s %-9s %-10s %.3f / %.3f / %.4f\n", variant.name, view.name, weather.name, b.mean,
                                         b.p99, b.clipped);
                         });
    }
}

std::vector<std::pair<std::string, motion_run>> motion_runs(const game_state& game, const hole_pick pick) {
    const play_area& area = game.area;
    const terrain_mesh& hole = area.holes[pick.hole];
    const int last = hole.section_count - 1;
    std::vector<std::pair<std::string, motion_run>> runs;
    const glm::vec2 from = hole_middle(hole, last / 10);
    const glm::vec2 to = hole_middle(hole, last * 9 / 10);
    runs.push_back({"fairway", {from, unit(to - from), glm::length(to - from), false}});
    const cup& hole_cup = area.cups[pick.hole];
    const glm::vec2 cup_at(hole_cup.centre.x, hole_cup.centre.z);
    const glm::vec2 back = unit(hole_middle(hole, 0) - cup_at);
    runs.push_back({"green", {cup_at + back * 40.0f, -back, 40.0f, false}});
    // The nearest point of any hole to a pond, looking over it.
    float best = std::numeric_limits<float>::max();
    std::optional<motion_run> over_pond;
    for (const material_zone& zone : area.zones) {
        if (zone.type != material_zone_type::water) { continue; }
        const glm::vec2 pond(zone.center.x, zone.center.z);
        for (const terrain_mesh& other : area.holes) {
            for (int row = 0; row < other.section_count; row += 4) {
                const glm::vec2 at = hole_middle(other, row);
                const float distance = glm::length(pond - at);
                if (distance >= 15.0f && distance < best) {
                    best = distance;
                    over_pond = motion_run{at, unit(pond - at), distance, true};
                }
            }
        }
    }
    if (over_pond) { runs.push_back({"pond", *over_pond}); }
    return runs;
}
}

bool run_turf_review(const check_course& course, const game_content& content, const lighting_palette& palette,
                     const std::string& asset_root, SDL_Window* window, const turf_review_options& options) {
    const hole_pick pick = pick_hole(course.game.area);
    if (!pick.found) {
        std::printf("  turf review: no hole with a cup\n");
        return false;
    }
    std::error_code made;
    std::filesystem::create_directories(options.out, made);
    const std::vector<review_view> views = views_of(course.game, pick);
    std::ofstream metrics(options.out / (options.label + "_metrics.csv"));
    metrics << "view,case,sun_up,mean_luma,p99_luma,share_over_0.9\n";
    bool written = true;
    std::printf("  turf review %s: hole %zu\n", options.label.c_str(), pick.hole + 1);
    const bool drawn = for_each_picture(
        course, content, palette, asset_root, window, views, cases(),
        [&](const review_view& view, const review_case& weather, const rig_capture& capture, const render_lighting& light) {
            const brightness b = measure_ground(capture);
            metrics << view.name << ',' << weather.name << ',' << light.sun_direction.y << ',' << b.mean << ',' << b.p99 << ','
                    << b.clipped << '\n';
            const std::string stem = options.label + "_" + view.name + "_" + weather.name;
            written = write_picture(options.out / (stem + "_scene.bmp"), capture.scene, capture.scene_width, capture.scene_height,
                                    1.0f / 2.2f) && written;
            if (saved_as_tv(view.name, weather.name)) {
                written = write_picture(options.out / (stem + "_tv.bmp"), capture.final_picture, capture.final_width,
                                        capture.final_height, 1.0f) && written;
            }
        });
    if (options.diagnose) { diagnose(course, content, palette, asset_root, window, views); }
    for (const auto& [name, run] : motion_runs(course.game, pick)) {
        std::printf("  turf motion %s: %.0f m from (%.1f, %.1f)\n", name.c_str(), run.length, run.start.x, run.start.y);
        std::filesystem::create_directories(options.out / ("motion_" + name), made);
        const grass_motion_result motion =
            run_motion_along(run, course, content, palette, asset_root, window, {review_lines}, options.out / ("motion_" + name));
        // Reported, not gated: the limits were set for the rough, and a far band
        // can fail from the course's own relief whatever the turf looks like.
        std::printf("  turf motion %s: %s the rough's limits\n", name.c_str(), grass_motion_passes(motion) ? "within" : "outside");
    }
    return drawn && written;
}
