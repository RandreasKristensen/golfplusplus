#include "tree_image_check.h"
#include "renderer/gl_loader.h"
#include "renderer/lighting_uniforms.h"
#include "renderer/render_tree.h"
#include "renderer/tree_renderer.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <glm/gtc/matrix_transform.hpp>

namespace {
constexpr int small_side = 128;
constexpr int oversample = 8;
constexpr int big_side = small_side * oversample;
constexpr float eye_distance = 60.0f;

// One look and its season, a tree of one variant, a light and the eye's direction.
struct image_case {
    std::size_t look = 0;
    int variant = 0;
    int shape = 0;
    render_tree_species season;
    bool night = false;
    float azimuth_degrees = 0.0f;
    float elevation_degrees = 0.0f;
    float wind = 0.0f;
    float seconds = 0.0f;
};

struct image_measure {
    float displacement = 0.0f;  // pixels of silhouette, mean over the boundary
    float area = 1.0f;          // image coverage over the model's
    float luminance = 1.0f;     // image's mean over the model's, where both cover
    float depth = 0.0f;         // mean eye-depth difference over where both cover, in crown radii
    float crown_pixels = 0.0f;
};

// A tall crown, a wide one and a low one: the unit frame's stretch changes what elevation the model sees.
constexpr std::array<tree_shape, 3> shapes{tree_shape{0.4f, 5.0f, 4.0f, 9.0f}, tree_shape{0.5f, 3.0f, 6.0f, 7.0f},
                                            tree_shape{0.15f, 0.0f, 1.6f, 1.8f}};

glm::vec3 spot_for_variant(const int variant) {
    for (int step = 0; step < 4000; ++step) {
        tree_body probe;
        probe.base = glm::vec3(3.7f * static_cast<float>(step % 61), 0.0f, 2.9f * static_cast<float>(step / 61));
        if (tree_variant_for(probe) == variant) { return probe.base; }
    }
    return glm::vec3(0.0f);
}

std::vector<float> box_average(const std::vector<float>& values, const int side, const int radius) {
    std::vector<float> out(values.size(), 0.0f);
    for (int y = 0; y < side; ++y) {
        for (int x = 0; x < side; ++x) {
            float sum = 0.0f;
            int count = 0;
            for (int dy = -radius; dy <= radius; ++dy) {
                for (int dx = -radius; dx <= radius; ++dx) {
                    const int px = std::clamp(x + dx, 0, side - 1);
                    const int py = std::clamp(y + dy, 0, side - 1);
                    sum += values[static_cast<std::size_t>(py * side + px)];
                    ++count;
                }
            }
            out[static_cast<std::size_t>(y * side + x)] = sum / static_cast<float>(count);
        }
    }
    return out;
}

float total_variation(const std::vector<float>& values, const int side) {
    float sum = 0.0f;
    for (int y = 0; y + 1 < side; ++y) {
        for (int x = 0; x + 1 < side; ++x) {
            const float here = values[static_cast<std::size_t>(y * side + x)];
            sum += std::abs(values[static_cast<std::size_t>(y * side + x + 1)] - here) +
                   std::abs(values[static_cast<std::size_t>((y + 1) * side + x)] - here);
        }
    }
    return sum;
}

float luminance_of(const glm::vec4& colour) { return 0.2126f * colour.r + 0.7152f * colour.g + 0.0722f * colour.b; }

float linear_depth(const float stored, const float near_plane, const float far_plane) {
    const float ndc = stored * 2.0f - 1.0f;
    return 2.0f * near_plane * far_plane / (far_plane + near_plane - ndc * (far_plane - near_plane));
}

std::vector<float> read_depth(const framebuffer& target, const int side) {
    std::vector<float> depth(static_cast<std::size_t>(side) * static_cast<std::size_t>(side));
    target.bind();
    glReadPixels(0, 0, side, side, GL_DEPTH_COMPONENT, GL_FLOAT, depth.data());
    return depth;
}

class image_rig {
public:
    tree_renderer& trees() { return trees_; }
    bool init(const lighting_palette& palette, const std::string& root) {
        palette_ = &palette;
        return trees_.init(check_shader_path(root, "tree.vert"), check_shader_path(root, "tree.frag"), palette.trees,
                           palette.tree_impostor) &&
               big_.init(big_side, big_side, {framebuffer_color::signed_hdr, framebuffer_depth::drawn, false, false}) &&
               small_.init(small_side, small_side, {framebuffer_color::signed_hdr, framebuffer_depth::drawn, false, false}) &&
               lighting_.init();
    }
    void shutdown() {
        trees_.shutdown();
        big_.shutdown();
        small_.shutdown();
        lighting_.shutdown();
    }
    // Whether one tree `distance` metres ahead is drawn whole or as its image, seen with `focal` pixels
    // to the radian, in `wind`: the pass's own projection and viewport at work.
    bool drawn_as_image(const float distance, const float focal, const float wind) {
        tree_body tree;
        tree.base = glm::vec3(0.0f, 0.0f, distance);
        tree.shape = shapes[0];
        trees_.set_seasons({{palette_->trees.front().name, 1.0f, 0.0f, 0.0f}});
        trees_.set_trees({tree}, ++revision_, nullptr);
        const glm::vec3 eye(0.0f, 5.0f, 0.0f);
        const float fov = 2.0f * std::atan(0.5f * static_cast<float>(small_side) / focal);
        const glm::mat4 view_proj = glm::perspective(fov, 1.0f, 1.0f, 2000.0f) *
                                    glm::lookAt(eye, glm::vec3(0.0f, 5.0f, distance), glm::vec3(0.0f, 1.0f, 0.0f));
        scene_haze haze;
        haze.eye = eye;
        haze.full_distance = 100000.0f;
        lighting_.upload(render_lighting{}, haze, {}, nullptr);
        small_.bind();
        glViewport(0, 0, small_side, small_side);
        glEnable(GL_DEPTH_TEST);
        frame_profile profile;
        trees_.draw(view_proj, make_view_frustum(view_proj), 0.0f, glm::vec2(wind, 0.0f), &profile, tree_eye{eye, focal});
        const pass_work& work = profile.work.passes[static_cast<std::size_t>(profile_pass::main)];
        return work.tree_images == 1 && work.tree_instances == 0;
    }

    // A crown `pixels` radii wide on the small picture, as the model and as its image.
    image_measure measure(const image_case& spec, const float pixels) {
        tree_body tree;
        tree.base = spot_for_variant(spec.variant);
        tree.shape = shapes[static_cast<std::size_t>(spec.shape)];
        const float crown_radius = tree.shape.leaf_radius;
        trees_.set_seasons({spec.season});
        // The look is the species' by name: seasons name it, the tree's species index is the season's first.
        trees_.set_trees({tree}, ++revision_, nullptr);
        const render_tree_instance instance = tree_instance_for(tree);
        const glm::vec3 centre = tree_world_point(instance, glm::vec3(0.0f, 0.5f, 0.0f));
        const float azimuth = glm::radians(spec.azimuth_degrees);
        const float elevation = glm::radians(spec.elevation_degrees);
        const glm::vec3 toward(std::cos(elevation) * std::cos(azimuth), std::sin(elevation), std::cos(elevation) * std::sin(azimuth));
        const glm::vec3 eye = centre + toward * eye_distance;
        const float focal = pixels * eye_distance / crown_radius;
        const float fov = 2.0f * std::atan(0.5f * static_cast<float>(small_side) / focal);
        constexpr float near_plane = 5.0f;
        constexpr float far_plane = 200.0f;
        const glm::mat4 view_proj = glm::perspective(fov, 1.0f, near_plane, far_plane) *
                                    glm::lookAt(eye, centre, glm::vec3(0.0f, 1.0f, 0.0f));
        render_lighting light;
        light.ambient_sky = glm::vec3(0.5f, 0.6f, 0.7f) * (spec.night ? 0.05f : 1.0f);
        light.ambient_ground = glm::vec3(0.2f, 0.25f, 0.15f) * (spec.night ? 0.05f : 1.0f);
        light.sun_direction = glm::normalize(glm::vec3(0.3f, 0.8f, 0.5f));
        light.sun_color = spec.night ? glm::vec3(0.0f) : glm::vec3(2.6f, 2.4f, 2.0f);
        scene_haze haze;
        haze.eye = eye;
        haze.full_distance = 100000.0f;
        std::vector<render_light> lamps;
        if (spec.night) {
            render_light lamp;
            lamp.position = centre + glm::vec3(14.0f, 6.0f, -3.0f);
            lamp.color = glm::vec3(3.0f, 2.7f, 2.0f);
            lamp.radius = 80.0f;
            lamps.push_back(lamp);
        }
        lighting_.upload(light, haze, lamps, nullptr);
        const glm::vec2 wind(spec.wind, 0.0f);
        const auto draw = [&](framebuffer& target, const int side, const bool image) {
            target.bind();
            glViewport(0, 0, side, side);
            glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            glEnable(GL_DEPTH_TEST);
            glDisable(GL_CULL_FACE);
            trees_.draw(view_proj, make_view_frustum(view_proj), spec.seconds, wind, nullptr,
                        tree_eye{eye, image ? 1.0e-3f : 0.0f});
        };
        draw(big_, big_side, false);
        const std::vector<glm::vec4> model_big = read_colors(big_, big_side, big_side);
        const std::vector<float> model_depth = read_depth(big_, big_side);
        // The image at the same eight times the pixels: its expected coverage, free of the hash's noise.
        draw(big_, big_side, true);
        const std::vector<glm::vec4> image_big = read_colors(big_, big_side, big_side);
        const std::vector<float> image_big_depth = read_depth(big_, big_side);

        const std::size_t count = static_cast<std::size_t>(small_side) * small_side;
        struct reduced { std::vector<float> cover, luma, depth; };
        const auto reduce = [&](const std::vector<glm::vec4>& colours, const std::vector<float>& depths) {
            reduced out{std::vector<float>(count, 0.0f), std::vector<float>(count, 0.0f), std::vector<float>(count, 0.0f)};
            for (int y = 0; y < small_side; ++y) {
                for (int x = 0; x < small_side; ++x) {
                    float covered = 0.0f;
                    float luma = 0.0f;
                    float depth = 0.0f;
                    for (int dy = 0; dy < oversample; ++dy) {
                        for (int dx = 0; dx < oversample; ++dx) {
                            const std::size_t at = static_cast<std::size_t>((y * oversample + dy) * big_side + x * oversample + dx);
                            if (colours[at].a > 0.5f) {
                                covered += 1.0f;
                                luma += luminance_of(colours[at]);
                                depth += linear_depth(depths[at], near_plane, far_plane);
                            }
                        }
                    }
                    const std::size_t pixel = static_cast<std::size_t>(y * small_side + x);
                    out.cover[pixel] = covered / static_cast<float>(oversample * oversample);
                    out.luma[pixel] = covered > 0.0f ? luma / covered : 0.0f;
                    out.depth[pixel] = covered > 0.0f ? depth / covered : -1.0f;
                }
            }
            return out;
        };
        const reduced model = reduce(model_big, model_depth);
        const reduced image = reduce(image_big, image_big_depth);
        const std::vector<float>& model_cover = model.cover;
        const std::vector<float>& image_cover = image.cover;
        image_measure out;
        out.crown_pixels = pixels;
        const std::vector<float> blurred_model = box_average(model_cover, small_side, 2);
        const std::vector<float> blurred_image = box_average(image_cover, small_side, 2);
        float difference = 0.0f;
        float model_area = 0.0f;
        float image_area = 0.0f;
        for (std::size_t i = 0; i < count; ++i) {
            difference += std::abs(blurred_model[i] - blurred_image[i]);
            model_area += model_cover[i];
            image_area += image_cover[i];
        }
        out.displacement = difference / std::max(total_variation(blurred_model, small_side), 1.0f);
        out.area = model_area > 0.0f ? image_area / model_area : 0.0f;
        float model_luma_sum = 0.0f;
        float image_luma_sum = 0.0f;
        float depth_sum = 0.0f;
        int both = 0;
        for (std::size_t i = 0; i < count; ++i) {
            if (image_cover[i] > 0.9f && model_cover[i] > 0.9f) {
                model_luma_sum += model.luma[i];
                image_luma_sum += image.luma[i];
                depth_sum += std::abs(image.depth[i] - model.depth[i]);
                ++both;
            }
        }
        out.luminance = model_luma_sum > 0.0f ? image_luma_sum / model_luma_sum : 1.0f;
        out.depth = both > 0 ? depth_sum / static_cast<float>(both) / crown_radius : 0.0f;
        return out;
    }

private:
    const lighting_palette* palette_ = nullptr;
    tree_renderer trees_;
    framebuffer big_;
    framebuffer small_;
    lighting_uniforms lighting_;
    std::uint64_t revision_ = 0;
};
}

bool check_tree_images(const lighting_palette& palette_in, const std::string& root, const tree_image_scope scope) {
    const lighting_palette& palette = palette_in;
    image_rig rig;
    if (!rig.init(palette, root)) { return false; }
    const bool full = scope == tree_image_scope::full;
    bool passed_cache = true;
    const tree_impostor_style& style = palette.tree_impostor;
    // The crown radius, in pixels, from which an image is allowed. Its error is a share of the radius at any size
    // (the sweep below shows it), measured on a crown large enough to resolve.
    const float threshold = style.max_error_pixels / std::max(style.radius_error, 1.0e-4f);
    constexpr float measured_size = 12.0f;
    std::printf("tree images: grid %d x %zu views of %d px, error %.3f of the crown radius: images from a crown %.1f px in radius\n",
                style.azimuths, style.elevations_degrees.size(), style.tile_pixels, style.radius_error, threshold);
    {
        // Baking every look's variants for a season, and what the cache holds (one allocation for all looks).
        std::vector<render_tree_species> all;
        for (const tree_look& look : palette.trees) { all.push_back({look.name, 1.0f, 0.0f, 0.0f}); }
        rig.trees().set_seasons({{palette.trees.front().name, 0.5f, 0.0f, 0.0f}});
        glFinish();
        const auto begun = std::chrono::steady_clock::now();
        rig.trees().set_seasons(all);
        glFinish();
        const double milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begun).count();
        const double mebibytes = static_cast<double>(rig.trees().images().texture_bytes()) / (1024.0 * 1024.0);
        std::printf("  baking %zu models: %.1f ms; cache %.1f MiB (limit 24): %s\n", all.size() * static_cast<std::size_t>(tree_variants),
                    milliseconds, mebibytes, mebibytes <= 24.0 ? "ok" : "FAILED");
        passed_cache = mebibytes <= 24.0;
    }
    const std::vector<float> elevations = full ? std::vector<float>{-15, 5, 30, 60} : std::vector<float>{-10, 20, 50};
    std::vector<float> azimuths;
    for (float degrees = 3.0f; degrees < 360.0f; degrees += full ? 30.0f : 41.0f) { azimuths.push_back(degrees); }
    struct season_case { const char* label; float foliage; float turned; float fresh; float withered; };
    const std::vector<season_case> seasons = full
        ? std::vector<season_case>{{"summer", 1, 0, 0, 0}, {"spring", 1, 0, 1, 0}, {"autumn", 0.7f, 1, 0, 0.1f}, {"bare", 0, 0, 0, 0},
                                   {"winter", 0, 1, 0, 0.25f}}
        : std::vector<season_case>{{"summer", 1, 0, 0, 0}, {"bare", 0, 0, 0, 0}};
    bool passed = passed_cache;
    std::ofstream csv;
    if (full) {
        csv.open(std::filesystem::path(GOLFPP_BUILD_DIR) / "tree_images.csv");
        csv << "look,variant,shape,season,night,wind,az,el,pixels,displacement,area,luminance,depth\n";
    }
    const auto record = [&](const image_case& spec, const image_measure& m, const char* season) {
        if (full) {
            csv << spec.look << ',' << spec.variant << ',' << spec.shape << ',' << season << ',' << spec.night << ',' << spec.wind << ','
                << spec.azimuth_degrees << ',' << spec.elevation_degrees << ',' << m.crown_pixels << ',' << m.displacement << ','
                << m.area << ',' << m.luminance << ',' << m.depth << '\n';
        }
    };
    float worst_fraction = 0.0f;
    for (std::size_t look = 0; look < palette.trees.size(); ++look) {
        float worst = 0.0f, worst_area = 0.0f, area_sum = 0.0f, luma_sum = 0.0f, luma_low = 10.0f, luma_high = 0.0f, depth_sum = 0.0f;
        int count = 0;
        for (int variant = 0; variant < (full ? tree_variants : 1); ++variant) {
            for (const season_case& season : seasons) {
                for (const bool night : {false, true}) {
                    if (night && (!full || season.foliage < 1.0f || season.turned > 0.0f || season.fresh > 0.0f)) { continue; }
                    for (const float el : elevations) {
                        for (const float az : azimuths) {
                            for (int shape = 0; shape < static_cast<int>(shapes.size()); ++shape) {
                                image_case spec;
                                spec.look = look;
                                spec.variant = variant;
                                spec.shape = shape;
                                spec.season = {palette.trees[look].name, season.foliage, season.turned, season.fresh, season.withered};
                                spec.night = night;
                                spec.azimuth_degrees = az;
                                spec.elevation_degrees = el;
                                const image_measure m = rig.measure(spec, measured_size);
                                record(spec, m, season.label);
                                worst = std::max(worst, m.displacement);
                                worst_area = std::max(worst_area, std::abs(m.area - 1.0f));
                                area_sum += m.area;
                                luma_sum += m.luminance;
                                luma_low = std::min(luma_low, m.luminance);
                                luma_high = std::max(luma_high, m.luminance);
                                depth_sum += m.depth;
                                ++count;
                            }
                        }
                    }
                }
            }
        }
        const float n = static_cast<float>(std::max(count, 1));
        const bool ok = worst / measured_size <= style.radius_error && std::abs(area_sum / n - 1.0f) <= 0.02f &&
                        std::abs(luma_sum / n - 1.0f) <= 0.07f && depth_sum / n <= 0.5f;
        worst_fraction = std::max(worst_fraction, worst / measured_size);
        std::printf("  %-7s %d views: silhouette %.3f of the radius worst, coverage %+.1f%% mean (%.0f%% worst), luminance %.2f mean (%.2f..%.2f), depth %.2f radii: %s\n",
                    palette.trees[look].name.c_str(), count, worst / measured_size, 100.0f * (area_sum / n - 1.0f), 100.0f * worst_area, luma_sum / n,
                    luma_low, luma_high, depth_sum / n, ok ? "ok" : "FAILED");
        passed = passed && ok;
    }
    if (full) {
        // The error is a share of the crown radius at any size; the pose warp's, a share of the height per m/s of wind.
        std::printf("  by crown radius, worst silhouette share of it:");
        for (const float size : {6.0f, 12.0f, 24.0f, 48.0f}) {
            float worst = 0.0f;
            for (std::size_t look = 0; look < palette.trees.size(); ++look) {
                for (const float el : {-15.0f, 20.0f, 60.0f}) {
                    for (const float az : {5.0f, 100.0f, 200.0f}) {
                        image_case spec;
                        spec.look = look;
                        spec.season = {palette.trees[look].name, 1, 0, 0};
                        spec.azimuth_degrees = az;
                        spec.elevation_degrees = el;
                        worst = std::max(worst, rig.measure(spec, size).displacement / size);
                    }
                }
            }
            std::printf(" %.1f px: %.3f", size, worst);
        }
        std::printf("\n  wind error, share of the height per m/s beyond the calm silhouette:");
        float worst_wind = 0.0f;
        for (const float wind : {6.0f, 20.0f, 60.0f}) {
            float worst = 0.0f;
            for (std::size_t look = 0; look < palette.trees.size(); ++look) {
                for (const float el : {-15.0f, 20.0f, 60.0f}) {
                    for (const float az : {5.0f, 100.0f, 200.0f}) {
                        for (const float seconds : {0.0f, 2.1f, 4.6f}) {
                            image_case spec;
                            spec.look = look;
                            spec.season = {palette.trees[look].name, 1, 0, 0};
                            spec.azimuth_degrees = az;
                            spec.elevation_degrees = el;
                            spec.seconds = seconds;
                            const float calm = rig.measure(spec, measured_size).displacement;
                            spec.wind = wind;
                            const float windy = rig.measure(spec, measured_size).displacement;
                            // Metres of the first shape per pixel at this size, over its height.
                            worst = std::max(worst, (windy - calm) * (shapes[0].leaf_radius / measured_size) /
                                                        (wind * (shapes[0].trunk_height + shapes[0].leaf_height)));
                        }
                    }
                }
            }
            worst_wind = std::max(worst_wind, worst);
            std::printf(" %.0f m/s: %.5f", wind, worst);
        }
        std::printf(" (shipped %.5f): %s\n", style.wind_error, worst_wind <= style.wind_error ? "ok" : "FAILED");
        passed = passed && worst_wind <= style.wind_error;
    }
    {
        // Each pass judges the error in its own picture: the same tree is whole or an image by lens, size and wind.
        constexpr float main_focal = 386.0f;  // 360 lines, 50 degrees
        struct pass_case { const char* label; float distance; float focal; float wind; bool image; };
        bool selected = true;
        for (const pass_case& c : {pass_case{"main, far", 400.0f, main_focal, 0.0f, true}, pass_case{"main, near", 150.0f, main_focal, 0.0f, false},
                                   pass_case{"zoomed 3x, far", 400.0f, 3.0f * main_focal, 0.0f, false},
                                   pass_case{"mirror at a quarter of the lines", 150.0f, 0.25f * main_focal, 0.0f, true},
                                   pass_case{"main, far, 30 m/s", 400.0f, main_focal, 30.0f, false}}) {
            const bool image = rig.drawn_as_image(c.distance, c.focal, c.wind);
            selected = selected && image == c.image;
            std::printf("  %-34s %.0f m: %s%s\n", c.label, c.distance, image ? "image" : "whole model", image == c.image ? "" : " WRONG");
        }
        passed = passed && selected;
    }
    std::printf("tree images: worst silhouette error %.3f of the crown radius (shipped %.3f): %s\n", worst_fraction,
                style.radius_error, passed && worst_fraction <= style.radius_error ? "ok" : "FAILED");
    rig.shutdown();
    return passed && worst_fraction <= style.radius_error;
}
