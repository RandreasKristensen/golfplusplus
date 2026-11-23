#include "grass_motion_check.h"

#include "picture_rig.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

namespace {
constexpr float eye_height = 1.7f;
constexpr float pitch_degrees = -3.0f;
constexpr float fov_degrees = 60.0f;
constexpr float turn_step_degrees = 0.05f;
constexpr float walk_step_meters = 0.05f;
constexpr float wind_speed = 6.0f;
constexpr float frozen_clock = 3.0f;
constexpr float frame_seconds = 1.0f / 60.0f;
// Standing still changes nothing. Beyond 8 m the picture changes at most 1.3
// times as much as the unfiltered reference of twice the lines does (blades
// finer than a pixel changed 1.5 to 2.1 times as much). Mean brightness stays
// within 12% of it: the filtered blades cover more than a point-sampled
// reference, up to about 8%, never less. Within 8 m blades are resolved and
// aliasing of their edges is inherent.
constexpr double still_change_limit = 2.0e-4;
constexpr double far_change_limit = 1.3;
constexpr double brightness_limit = 0.12;
constexpr float clearance_step = grass_field_distance_cap / 65535.0f;
constexpr float band_edges[5] = {2.5f, 8.0f, 22.0f, 44.0f, 88.0f};

// The longest straight line of rough the field offers, so the same patch is
// in view for a walk through every density ring.
motion_run longest_rough_run(const grass_field& field) {
    motion_run best;
    const std::size_t stride = std::max<std::size_t>(1, static_cast<std::size_t>(field.columns) * static_cast<std::size_t>(field.rows) / 600);
    for_each_grass_node(field, stride, [&](const glm::vec2& at, const glm::vec3& node) {
        // Stored clearance is the exact one rounded down by up to a step.
        if (node.y < 1.0f - clearance_step) { return false; }
        for (int heading = 0; heading < 8; ++heading) {
            const float angle = glm::radians(45.0f * static_cast<float>(heading));
            const glm::vec2 way(std::cos(angle), std::sin(angle));
            float length = 0.0f;
            while (length < 100.0f && sample_grass_field(field, at + way * (length + 1.0f)).y >= 0.5f - clearance_step) { length += 1.0f; }
            if (length > best.length) { best = {at, way, length, false}; }
        }
        return false;
    });
    return best;
}

struct luma_image {
    int width = 0;
    int height = 0;
    std::vector<float> values;
};

luma_image luma_of(const std::vector<glm::vec4>& pixels, const int width, const int height) {
    luma_image image{width, height, std::vector<float>(pixels.size())};
    for (std::size_t i = 0; i < pixels.size(); ++i) {
        image.values[i] = 0.2126f * pixels[i].r + 0.7152f * pixels[i].g + 0.0722f * pixels[i].b;
    }
    return image;
}

luma_image box_down(const luma_image& in, const int factor) {
    luma_image out{in.width / factor, in.height / factor, {}};
    out.values.assign(static_cast<std::size_t>(out.width) * static_cast<std::size_t>(out.height), 0.0f);
    for (int y = 0; y < out.height; ++y) {
        for (int x = 0; x < out.width; ++x) {
            float sum = 0.0f;
            for (int dy = 0; dy < factor; ++dy) {
                for (int dx = 0; dx < factor; ++dx) {
                    sum += in.values[static_cast<std::size_t>((y * factor + dy) * in.width + x * factor + dx)];
                }
            }
            out.values[static_cast<std::size_t>(y * out.width + x)] = sum / static_cast<float>(factor * factor);
        }
    }
    return out;
}

// The pixel row (from the bottom) of level ground `distance` away for an eye
// `pitch_degrees` below the horizon: a diagnostic layout, not exact on slopes.
int row_at(const float distance, const int height) {
    const float down = -std::atan(eye_height / distance);
    const float rel = down - glm::radians(pitch_degrees);
    const float ndc = std::tan(rel) / std::tan(glm::radians(fov_degrees) * 0.5f);
    return std::clamp(static_cast<int>(std::lround((ndc + 1.0f) * 0.5f * static_cast<float>(height))), 0, height);
}

struct band_sums {
    double scene_change[4] = {0, 0, 0, 0};
    double reference_change[4] = {0, 0, 0, 0};
    double square_error[4] = {0, 0, 0, 0};
    double luma[4] = {0, 0, 0, 0};
    double scene_luma[4] = {0, 0, 0, 0};
    double pixels[4] = {0, 0, 0, 0};
};

double mean_change(const luma_image& a, const luma_image& b, const int row_low, const int row_high, const int left, const int right) {
    double sum = 0.0;
    long long count = 0;
    for (int y = row_low; y < row_high; ++y) {
        for (int x = left; x < right; ++x) {
            const auto i = static_cast<std::size_t>(y * a.width + x);
            sum += std::abs(a.values[i] - b.values[i]);
            ++count;
        }
    }
    return count ? sum / static_cast<double>(count) : 0.0;
}

std::vector<rig_camera> sequence_of(const motion_run& run, const check_course& course, const motion_kind kind, const bool wind) {
    const int frames = kind == motion_kind::still || kind == motion_kind::still_clock ? 6 : 40;
    const glm::vec2 across(-run.heading.y, run.heading.x);
    std::vector<rig_camera> cameras;
    for (int frame = 0; frame < frames; ++frame) {
        const float step = static_cast<float>(frame);
        glm::vec2 at = run.start;
        float yaw = 0.0f;
        if (kind == motion_kind::walk_toward) { at += run.heading * walk_step_meters * step; }
        if (kind == motion_kind::walk_across) { at += across * walk_step_meters * step; }
        if (kind == motion_kind::turn) { yaw = glm::radians(turn_step_degrees) * step; }
        const glm::vec2 way(run.heading.x * std::cos(yaw) - run.heading.y * std::sin(yaw),
                            run.heading.x * std::sin(yaw) + run.heading.y * std::cos(yaw));
        rig_camera camera;
        camera.eye = anchor_on_terrain(course.game.area, glm::vec3(at.x, 0.0f, at.y)) + glm::vec3(0.0f, eye_height, 0.0f);
        const float pitch = glm::radians(pitch_degrees);
        camera.target = camera.eye + glm::vec3(way.x * std::cos(pitch), std::sin(pitch), way.y * std::cos(pitch));
        camera.fov_degrees = fov_degrees;
        camera.clock_seconds = kind == motion_kind::still ? frozen_clock : frozen_clock + step * frame_seconds;
        camera.wind = wind ? glm::vec2(wind_speed, 0.0f) : glm::vec2(0.0f);
        camera.ponds = run.ponds;
        cameras.push_back(camera);
    }
    return cameras;
}

const char* kind_name(const motion_kind kind) {
    switch (kind) {
        case motion_kind::still: return "still";
        case motion_kind::still_clock: return "still_clock";
        case motion_kind::turn: return "turn";
        case motion_kind::walk_toward: return "walk_toward";
        case motion_kind::walk_across: return "walk_across";
    }
    return "";
}

void save_picture(const luma_image& image, const std::filesystem::path& path) {
    std::vector<std::array<std::uint8_t, 3>> rgb;
    for (const float value : image.values) {
        const auto grey = static_cast<std::uint8_t>(255.0f * std::sqrt(std::clamp(value, 0.0f, 1.0f)));
        rgb.push_back({grey, grey, grey});
    }
    write_check_bmp(path, image.width, image.height, rgb);
}

motion_measure measure_sequence(picture_rig& shown, picture_rig& reference, const std::vector<rig_camera>& cameras,
                                const int lines, const motion_kind kind, const bool wind, const std::filesystem::path& out) {
    motion_measure measure;
    measure.target_lines = lines;
    measure.kind = kind;
    measure.wind = wind;
    luma_image scene_before;
    luma_image reference_before;
    luma_image final_before;
    band_sums sums;
    double scene_change = 0.0;
    double reference_change = 0.0;
    double final_change = 0.0;
    int changes = 0;
    for (std::size_t frame = 0; frame < cameras.size(); ++frame) {
        const rig_capture one = shown.capture(cameras[frame]);
        const rig_capture two = reference.capture(cameras[frame]);
        if (one.scene.empty() || two.scene.empty() || two.scene_width != 2 * one.scene_width) { break; }
        const luma_image scene = luma_of(one.scene, one.scene_width, one.scene_height);
        const luma_image truth = box_down(luma_of(two.scene, two.scene_width, two.scene_height), 2);
        const luma_image final_picture = luma_of(one.final_picture, one.final_width, one.final_height);
        const int left = scene.width / 5;
        const int right = scene.width - left;
        // Rows rise with distance: band k spans edge_rows[k] to edge_rows[k + 1].
        int edge_rows[5];
        for (int i = 0; i < 5; ++i) { edge_rows[i] = row_at(band_edges[i], scene.height); }
        for (int band = 0; band < 4; ++band) {
            for (int y = edge_rows[band]; y < edge_rows[band + 1]; ++y) {
                for (int x = left; x < right; ++x) {
                    const auto i = static_cast<std::size_t>(y * scene.width + x);
                    const double error = scene.values[i] - truth.values[i];
                    sums.square_error[band] += error * error;
                    sums.luma[band] += truth.values[i];
                    sums.scene_luma[band] += scene.values[i];
                    sums.pixels[band] += 1.0;
                }
            }
        }
        const int low = edge_rows[0];
        const int high = edge_rows[4];
        if (frame > 0) {
            const int enlarge = final_picture.width / scene.width;
            scene_change += mean_change(scene, scene_before, low, high, left, right);
            reference_change += mean_change(truth, reference_before, low, high, left, right);
            final_change += mean_change(final_picture, final_before, low * enlarge, high * enlarge, left * enlarge, right * enlarge);
            ++changes;
            for (int band = 0; band < 4; ++band) {
                sums.scene_change[band] += mean_change(scene, scene_before, edge_rows[band], edge_rows[band + 1], left, right);
                sums.reference_change[band] += mean_change(truth, reference_before, edge_rows[band], edge_rows[band + 1], left, right);
            }
        }
        if (frame + 1 == cameras.size() && !wind && kind == motion_kind::turn) {
            save_picture(scene, out / ("grass_motion_scene_" + std::to_string(lines) + ".bmp"));
            save_picture(truth, out / ("grass_motion_reference_" + std::to_string(lines) + ".bmp"));
        }
        scene_before = scene;
        reference_before = truth;
        final_before = final_picture;
        measure.frames = static_cast<int>(frame) + 1;
    }
    double square = 0.0;
    double luma = 0.0;
    double pixels = 0.0;
    for (int band = 0; band < 4; ++band) {
        square += sums.square_error[band];
        luma += sums.luma[band];
        pixels += sums.pixels[band];
        if (sums.pixels[band] > 0.0 && sums.luma[band] > 0.0) {
            measure.band_bias[band] = sums.scene_luma[band] / sums.luma[band] - 1.0;
            measure.band_alias[band] = std::sqrt(sums.square_error[band] / sums.pixels[band]) / (sums.luma[band] / sums.pixels[band]);
        }
    }
    if (pixels > 0.0) {
        measure.mean_luma = luma / pixels;
        measure.alias = std::sqrt(square / pixels) / std::max(measure.mean_luma, 1.0e-6);
    }
    for (int band = 0; band < 4; ++band) {
        measure.band_ratio[band] = sums.reference_change[band] > 0.0 ? sums.scene_change[band] / sums.reference_change[band] : 0.0;
    }
    if (changes > 0) {
        measure.scene_change = scene_change / changes;
        measure.reference_change = reference_change / changes;
        measure.final_change = final_change / changes;
    }
    return measure;
}
}

grass_motion_result run_motion_along(const motion_run& run, const check_course& course, const game_content& content,
                                     const lighting_palette& palette, const std::string& asset_root, SDL_Window* window,
                                     const std::vector<int>& scene_lines, const std::filesystem::path& out,
                                     const std::optional<float> blade_min_pixels, const int tile_layers) {
    grass_motion_result result;
    for (const int lines : scene_lines) {
        const int factor = std::max(1, (1080 + lines / 2) / lines);
        picture_rig shown;
        picture_rig reference;
        if (!shown.init(window, course, content, palette, asset_root, lines * factor, lines, blade_min_pixels)) { continue; }
        if (tile_layers > 0 && !shown.limit_grass_tiles(tile_layers)) {
            shown.shutdown();
            continue;
        }
        if (!reference.init(window, course, content, palette, asset_root, lines * 2, lines * 2, 0.0f)) {
            std::printf("  grass motion: %d lines: no %d-line reference window\n", lines, lines * 2);
            shown.shutdown();
            continue;
        }
        // The first frames after creating a renderer are not steady.
        for (int warm = 0; warm < 3; ++warm) {
            const rig_camera pose = sequence_of(run, course, motion_kind::still, false).front();
            shown.capture(pose);
            reference.capture(pose);
        }
        for (const bool wind : {false, true}) {
            for (const motion_kind kind : {motion_kind::still, motion_kind::still_clock, motion_kind::turn,
                                           motion_kind::walk_toward, motion_kind::walk_across}) {
                const motion_measure measure = measure_sequence(shown, reference, sequence_of(run, course, kind, wind), lines, kind, wind, out);
                std::printf("  %3d lines %-11s wind %d: alias %.3f, change scene %.5f ref %.5f final %.5f\n", lines, kind_name(kind),
                            wind ? 1 : 0, measure.alias, measure.scene_change, measure.reference_change, measure.final_change);
                std::printf("      bands 2.5-8, 8-22, 22-44, 44-88 m: change x%.2f %.2f %.2f %.2f, brightness %+.3f %+.3f %+.3f %+.3f\n",
                            measure.band_ratio[0], measure.band_ratio[1], measure.band_ratio[2], measure.band_ratio[3],
                            measure.band_bias[0], measure.band_bias[1], measure.band_bias[2], measure.band_bias[3]);
                result.measures.push_back(measure);
            }
        }
        shown.shutdown();
        reference.shutdown();
    }
    result.ran = !result.measures.empty();
    return result;
}

grass_motion_result run_grass_motion(const check_course& course, const game_content& content, const lighting_palette& palette,
                                     const std::string& asset_root, SDL_Window* window, const std::vector<int>& scene_lines,
                                     const std::filesystem::path& out,
                                     const std::optional<float> blade_min_pixels, const int tile_layers) {
    const motion_run run = longest_rough_run(course.grass);
    if (run.length < 20.0f) {
        std::printf("  grass motion: no rough run of 20 m (best %.0f m)\n", run.length);
        return {};
    }
    std::printf("  grass motion: rough run %.0f m from (%.1f, %.1f)\n", run.length, run.start.x, run.start.y);
    return run_motion_along(run, course, content, palette, asset_root, window, scene_lines, out, blade_min_pixels, tile_layers);
}

bool grass_motion_passes(const grass_motion_result& result) {
    bool passed = result.ran;
    for (const motion_measure& measure : result.measures) {
        const bool still = measure.kind == motion_kind::still;
        const bool moving = measure.kind == motion_kind::turn || measure.kind == motion_kind::walk_toward ||
                            measure.kind == motion_kind::walk_across;
        bool ok = !still || measure.scene_change <= still_change_limit;
        for (int band = 0; band < 4; ++band) {
            ok = ok && std::abs(measure.band_bias[band]) <= brightness_limit;
            // Beyond 8 m, blades finer than a pixel must not change the picture
            // more than the world itself does.
            ok = ok && (!moving || band == 0 || measure.band_ratio[band] <= far_change_limit);
        }
        if (!ok) {
            std::printf("  grass motion FAILED: %d lines %s wind %d: change x%.2f %.2f %.2f %.2f, brightness %+.3f %+.3f %+.3f %+.3f\n",
                        measure.target_lines, kind_name(measure.kind), measure.wind ? 1 : 0, measure.band_ratio[0],
                        measure.band_ratio[1], measure.band_ratio[2], measure.band_ratio[3], measure.band_bias[0],
                        measure.band_bias[1], measure.band_bias[2], measure.band_bias[3]);
        }
        passed = passed && ok;
    }
    return passed;
}
