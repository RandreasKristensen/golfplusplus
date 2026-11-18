#include "grass_check.h"
#include "grass_ground_check.h"

#include "core/sky_frame.h"
#include "renderer/gl_loader.h"
#include "renderer/gl_timer.h"
#include "renderer/grass_renderer.h"
#include "renderer/lighting_uniforms.h"
#include "renderer/shadow_map_pass.h"
#include "renderer/terrain_renderer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>
#include <set>
#include <array>

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace {
constexpr int shadow_unit = 4;
constexpr float pixel_meters = 0.02f;
constexpr int most_pixels = 1536;
constexpr float eye_height = 1.7f;
// Eyes this far from a place put it in the first ring, the second and the third.
constexpr float eye_distances[] = {0.0f, 9.0f, 31.0f, 64.0f};
constexpr std::size_t most_of_a_kind = 10;
// The wind a blade leans furthest in, m/s.
constexpr float strong_wind = 12.0f;

struct place {
    glm::vec2 centre{0.0f};
    float size = 0.0f;
};

// The places checked: every kind of bare ground, a few of each.
std::vector<place> places_of(const check_course& course) {
    std::vector<place> places;
    const play_area& area = course.game.area;
    std::size_t bunkers = 0;
    std::size_t greens = 0;
    std::size_t fairways = 0;
    for (const material_zone& zone : area.zones) {
        const glm::vec2 centre(zone.center.x, zone.center.z);
        const float size = std::min(2.0f * std::max(zone.half_size.x, zone.half_size.y) + 6.0f, most_pixels * pixel_meters);
        if (zone.type == material_zone_type::bunker && bunkers++ < most_of_a_kind) {
            places.push_back({centre, size});
        } else if (zone.type == material_zone_type::green && greens++ < most_of_a_kind / 2) {
            places.push_back({centre, size});
        } else if (zone.type == material_zone_type::fairway && fairways++ < most_of_a_kind / 2 && !zone.outline.empty()) {
            // A fairway is long: its edges, here and there.
            for (std::size_t i = 0; i < zone.outline.size(); i += std::max<std::size_t>(1, zone.outline.size() / 3)) {
                places.push_back({zone.outline[i], 12.0f});
            }
        }
    }
    for (std::size_t i = 0; i < std::min(area.tee_boxes.size(), most_of_a_kind); ++i) {
        const tee_box& box = area.tee_boxes[i];
        places.push_back({glm::vec2(box.center.x, box.center.z), 2.0f * std::max(box.half_width, box.half_length) + 6.0f});
    }
    std::size_t paths = 0;
    for (std::size_t i = 0; i < course.overlay.vertices.size() && paths < most_of_a_kind; i += 997) {
        const render_terrain_vertex& vertex = course.overlay.vertices[i];
        if (vertex.paved > 0.5f) {
            places.push_back({glm::vec2(vertex.position.x, vertex.position.z), 10.0f});
            ++paths;
        }
    }
    return places;
}

std::vector<render_terrain_vertex> tee_tops(const std::vector<tee_box>& boxes) {
    std::vector<render_terrain_vertex> vertices;
    for (const auto& box : boxes) {
        const auto along=box.down_hole*box.half_length;
        const glm::vec3 across(-box.down_hole.z*box.half_width,0,box.down_hole.x*box.half_width);
        const glm::vec3 corners[]={box.center-along-across,box.center+along-across,box.center+along+across,box.center-along+across};
        for (const int i : {0,1,2,0,2,3}) { render_terrain_vertex v;v.position=corners[i];vertices.push_back(v); }
    }
    return vertices;
}

// Draws `mesh` (non-indexed render vertices) with the bound shader.
void draw_vertices(const std::vector<render_terrain_vertex>& mesh) {
    if (mesh.empty()) {
        return;
    }
    GLuint vao = 0;
    GLuint vbo = 0;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(mesh.size() * sizeof(render_terrain_vertex)), mesh.data(), GL_STREAM_DRAW);
    describe_render_terrain_vertex();
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(mesh.size()));
    glBindVertexArray(0);
    glDeleteBuffers(1, &vbo);
    glDeleteVertexArrays(1, &vao);
}
}

grass_check_result check_grass(const check_course& course, const lighting_palette& palette, const std::string& asset_root,
                               const std::filesystem::path& out) {
    grass_check_result result;
    const auto path = [&](const char* name) { return check_shader_path(asset_root, name); };
    terrain_renderer ground;
    grass_renderer grass;
    grass_renderer roots;
    shadow_map_pass shadows;
    lighting_uniforms lighting;
    shader_program truth;
    framebuffer picture;
    gl_timer_pool timers;
    timers.init();
    if (!ground.init() || !grass.init(path("grass_blade.vert"), path("grass_blade.frag")) ||
        !roots.init(path("grass_blade.vert"), path("grass_blade.frag"), "#define GRASS_DEBUG_ROOTS\n") ||
        !shadows.init(path("shadow_map.vert"), path("shadow_map.frag"), palette.shadows) || !lighting.init() ||
        !truth.load_from_files(asset_root + "/../tooling/picture_check/ground_truth.vert",
                               asset_root + "/../tooling/picture_check/ground_truth.frag") ||
        !picture.init(most_pixels, most_pixels, framebuffer_format{framebuffer_color::exact, framebuffer_depth::drawn, false, false})) {
        std::fprintf(stderr, "grass check: cannot set up GL\n");
        return result;
    }
    result.ran = true;
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    const grass_style& style = palette.grass;
    // A blade leans this far over the ground at most, in the widest ring.
    const float reach = grass_blade_reach(style, std::ldexp(1.0f, grass_max_rings - 1)) + 1.5f * pixel_meters;
    const int reach_pixels = static_cast<int>(std::ceil(reach / pixel_meters));
    sky_controls sky;
    sky.pinned_hour = 12.0f;
    const render_lighting light = make_render_lighting(course.game, palette, sky, 0);
    const play_area& area = course.game.area;
    const std::vector<place> places = places_of(course);
    const auto ground_views=check_grass_ground_views(course,palette,asset_root,out);
    result.ran = result.ran && ground_views.ran && ground_views.pictures > 0;
    result.pictures+=ground_views.pictures;
    result.wrong_pixels+=ground_views.wrong_pixels;
    long long worst = 0;
    int shot = 0;
    for (std::size_t i = 0; i < places.size(); ++i) {
        std::set<std::array<int,3>> initial_roots;
        const place& at = places[i];
        const int side = std::min(most_pixels, static_cast<int>(std::ceil(at.size / pixel_meters)));
        const float size = static_cast<float>(side) * pixel_meters;
        for (const float distance : eye_distances) {
            // Round the place by the golden angle, so the eyes' texels fall every way.
            const float turn = 2.39996323f * static_cast<float>(i * 4U + static_cast<std::size_t>(shot++ % 4));
            const glm::vec2 flat = at.centre + glm::vec2(std::cos(turn), std::sin(turn)) * distance;
            const glm::vec3 eye = anchor_on_terrain(area, glm::vec3(flat.x, 0.0f, flat.y)) + glm::vec3(0.0f, eye_height, 0.0f);
            const float base = std::round(eye.y);
            glm::mat4 view_proj(0);
            view_proj[0][0] = 2.0f/size; view_proj[2][1] = 2.0f/size;
            view_proj[1][2] = -1.0f/200.0f;
            view_proj[3] = glm::vec4(-2.0f*at.centre.x/size,-2.0f*at.centre.y/size,base/200.0f,1);
            scene_haze haze;
            haze.eye = eye;
            haze.full_distance = 1.0e5f;
            lighting.upload(light, haze, {}, nullptr);

            timers.begin_frame(false);
            picture.bind();
            glViewport(0, 0, side, side);
            glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            const grass_frame frame{&course.terrain, &course.overlay, &course.grass, view_proj, eye,
                                    palette.colors.ground.rough, glm::vec2(strong_wind, 0.0f), 7.0f};
            grass.draw(frame, style, shadows, shadow_unit, timers, nullptr);
            const std::vector<glm::vec4> blades = read_colors(picture, side, side);
            picture.bind(); glViewport(0,0,side,side);
            glClearColor(0,0,0,0); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            roots.draw(frame, style, shadows, shadow_unit, timers, nullptr);
            const auto root_pixels = read_colors(picture,side,side);
            std::set<std::array<int,3>> selected;
            for (const auto& root : root_pixels) {
                if (root.a > 0.5f) { selected.insert({static_cast<int>(std::round(root.x*1000)),
                    static_cast<int>(std::round(root.y*1000)),static_cast<int>(std::round(root.z*1000))}); }
            }
            if (distance == 0) { initial_roots=selected; }
            else {
                for (const auto& root : selected) {
                    ++result.checked_roots;
                    result.changed_roots += initial_roots.count(root) == 0 ? 1 : 0;
                }
            }

            // The ground exactly, its blades in g; then the tee boxes' tops in b.
            picture.bind();
            glViewport(0, 0, side, side);
            glClearColor(-1000.0f, 1.0f, 0.0f, 0.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            truth.use();
            ground.draw(truth, &course.terrain, &course.overlay, view_proj, make_view_frustum(view_proj), overlay_depth::over_all, nullptr);
            const std::vector<glm::vec4> surface = read_colors(picture, side, side);
            picture.bind();
            glClearColor(0.0f, 1.0f, 0.0f, 0.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            truth.use();
            truth.set_mat4("u_mvp", view_proj);
            draw_vertices(tee_tops(area.tee_boxes));
            const std::vector<glm::vec4> tees = read_colors(picture, side, side);

            const int width = side;
            const auto at_pixel = [&](const std::vector<glm::vec4>& pixels, const int x, const int y) -> const glm::vec4& {
                return pixels[static_cast<std::size_t>(y * width + x)];
            };
            const auto bare = [&](const int x, const int y) { return at_pixel(surface, x, y).y < 0.5f; };
            std::vector<std::array<std::uint8_t, 3>> look(static_cast<std::size_t>(side * side));
            long long wrong = 0;
            for (int y = 0; y < side; ++y) {
                for (int x = 0; x < side; ++x) {
                    const bool blade = at_pixel(blades, x, y).a > 0.5f;
                    const bool tee = at_pixel(tees, x, y).y < 0.5f;
                    bool deep = false;
                    if (blade && !tee && bare(x, y)) {
                        // Further into bare ground than any blade leans: its root is on it.
                        deep = true;
                        for (int dy = -reach_pixels; dy <= reach_pixels && deep; ++dy) {
                            for (int dx = -reach_pixels; dx <= reach_pixels && deep; ++dx) {
                                const int nx = x + dx;
                                const int ny = y + dy;
                                if (dx * dx + dy * dy <= reach_pixels * reach_pixels && nx >= 0 && ny >= 0 && nx < side &&
                                    ny < side && !bare(nx, ny)) {
                                    deep = false;
                                }
                            }
                        }
                    }
                    const bool bad = blade && (tee || deep);
                    result.blade_pixels += blade ? 1 : 0;
                    wrong += bad ? 1 : 0;
                    look[static_cast<std::size_t>(y * side + x)] = bad   ? std::array<std::uint8_t, 3>{255, 0, 0}
                        : blade                                          ? std::array<std::uint8_t, 3>{40, 160, 40}
                        : tee                                            ? std::array<std::uint8_t, 3>{60, 90, 220}
                        : bare(x, y)                                     ? std::array<std::uint8_t, 3>{210, 190, 120}
                                                                         : std::array<std::uint8_t, 3>{20, 50, 20};
                }
            }
            ++result.pictures;
            result.wrong_pixels += wrong;
            if (wrong > worst) {
                worst = wrong;
                write_check_bmp(out / "grass_worst.bmp", side, side, look);
                std::printf("  %lld wrong blade pixels round (%.1f, %.1f), eye %.0f m away\n", wrong, at.centre.x, at.centre.y,
                            distance);
            }
        }
    }
    framebuffer::bind_default();
    picture.shutdown();
    truth.shutdown();
    lighting.shutdown();
    shadows.shutdown();
    grass.shutdown();
    roots.shutdown();
    ground.shutdown();
    timers.shutdown();
    return result;
}
