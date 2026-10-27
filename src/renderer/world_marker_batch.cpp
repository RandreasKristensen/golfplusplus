#include "renderer/world_marker_batch.h"

#include "physics/vector_math.h"
#include "renderer/camera_local.h"
#include "renderer/primitive_mesh.h"
#include "renderer/remote_avatar_batch.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <cmath>

namespace {
// A tee box: grey tiles with grout between them, a mat of fake turf on top.
// Tiles are about this many metres square, with this much grout between them.
constexpr float tee_tile_size = 1.5f;
constexpr float tee_grout_gap = 0.08f;
// Over the box's top: the tiles, then the mat. The sides reach this far
// under the lowest ground, so they never stop short of it.
constexpr float tee_tile_lift = 0.01f;
constexpr float tee_mat_lift = 0.02f;
constexpr float tee_side_bury = 0.2f;
// The mat's share of the box's top, across and along the hole.
constexpr float tee_mat_share_across = 0.7f;
constexpr float tee_mat_share_along = 0.75f;
constexpr float collectible_scale = 1.2f;
// The flagstick as drawn: thicker than a real one so it reads at a distance.
constexpr float pole_half_width = 0.028f;
// A flag: its length out from the pole and half its height; in calm it
// hangs this far from straight down; how much of a wave runs along it, and
// how far its wave's phase moves per metre across the course.
constexpr float flag_length = 0.72f;
constexpr float flag_half_height = 0.24f;
// A laid-down flagstick: a flat strip seen from above, drawn this far over
// the green so it never sinks into it.
constexpr float lying_half_width = 0.018f;
constexpr float lying_lift = 0.01f;
constexpr float flag_hang_radians = 0.35f;
constexpr float flag_waves = 0.75f;
constexpr float flag_phase_per_metre = 0.37f;
constexpr float no_glow = 0.0f;

float axis_x_yaw_radians(const glm::vec3& axis) {
    glm::vec3 flat(axis.x, 0.0f, axis.z);
    if (glm::length(flat) <= 0.0001f) {
        flat = glm::vec3(1.0f, 0.0f, 0.0f);
    } else {
        flat = glm::normalize(flat);
    }
    return std::atan2(-flat.z, flat.x);
}
}

std::vector<glm::vec3> make_unit_disc_positions(const int segments) {
    std::vector<glm::vec3> positions;
    if (segments <= 0) {
        return positions;
    }
    positions.reserve(static_cast<std::size_t>(segments) * 3U);

    constexpr float radius = 0.5f;
    for (int i = 0; i < segments; ++i) {
        const float a0 = glm::two_pi<float>() * static_cast<float>(i) / static_cast<float>(segments);
        const float a1 = glm::two_pi<float>() * static_cast<float>(i + 1) / static_cast<float>(segments);
        positions.emplace_back(0.0f, 0.0f, 0.0f);
        positions.emplace_back(std::cos(a0) * radius, 0.0f, std::sin(a0) * radius);
        positions.emplace_back(std::cos(a1) * radius, 0.0f, std::sin(a1) * radius);
    }
    return positions;
}

std::vector<glm::vec3> make_unit_quad_positions() {
    return {
        glm::vec3(-1.0f, -1.0f, 0.0f),
        glm::vec3(1.0f, -1.0f, 0.0f),
        glm::vec3(1.0f, 1.0f, 0.0f),
        glm::vec3(-1.0f, -1.0f, 0.0f),
        glm::vec3(1.0f, 1.0f, 0.0f),
        glm::vec3(-1.0f, 1.0f, 0.0f),
    };
}

world_marker_batch::world_marker_batch()
    : unit_disc_(make_unit_disc_positions(world_marker_disc_segments)),
      unit_quad_(make_unit_quad_positions()),
      unit_cylinder_(make_cylinder_positions(primitive_cylinder_segments)),
      unit_sphere_(make_sphere_positions(primitive_sphere_latitude_segments,
                                         primitive_sphere_longitude_segments)) {}

void world_marker_batch::clear() {
    vertices_.clear();
    runs_.clear();
}

void world_marker_batch::append_disc(const glm::mat4& model,
                                     const glm::vec3& color,
                                     const float glow,
                                     const bool depth_write) {
    append_triangles(unit_disc_, model, color, glow, depth_write);
}

void world_marker_batch::append_quad(const glm::mat4& model,
                                     const glm::vec3& color,
                                     const float glow,
                                     const bool depth_write) {
    append_triangles(unit_quad_, model, color, glow, depth_write);
}

void world_marker_batch::append_cylinder(const glm::mat4& model,
                                         const glm::vec3& color,
                                         const float glow,
                                         const bool depth_write) {
    append_triangles(unit_cylinder_, model, color, glow, depth_write);
}

void world_marker_batch::append_sphere(const glm::mat4& model,
                                       const glm::vec3& color,
                                       const float glow,
                                       const bool depth_write) {
    append_triangles(unit_sphere_, model, color, glow, depth_write);
}

void world_marker_batch::append_triangles(const std::vector<glm::vec3>& local_positions,
                                          const glm::mat4& model,
                                          const glm::vec3& color,
                                          const float glow,
                                          const bool depth_write) {
    if (local_positions.empty()) {
        return;
    }

    if (runs_.empty() || runs_.back().depth_write != depth_write) {
        world_marker_run run;
        run.depth_write = depth_write;
        run.first = vertices_.size();
        runs_.push_back(run);
    }

    for (const glm::vec3& local : local_positions) {
        world_marker_vertex vertex;
        vertex.position = glm::vec3(model * glm::vec4(local, 1.0f));
        vertex.color = color;
        vertex.glow = glow;
        vertices_.push_back(vertex);
    }
    runs_.back().count += local_positions.size();
}

void world_marker_batch::append_world_quad(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& d,
                                           const glm::vec3& color, const float glow, const bool depth_write) {
    if (runs_.empty() || runs_.back().depth_write != depth_write) {
        world_marker_run run;
        run.depth_write = depth_write;
        run.first = vertices_.size();
        runs_.push_back(run);
    }
    for (const glm::vec3& position : {a, b, c, a, c, d}) {
        vertices_.push_back(world_marker_vertex{position, color, glow});
    }
    runs_.back().count += 6;
}

glm::mat4 ground_marker_model(const glm::vec3& position, const float scale) {
    return glm::scale(glm::translate(glm::mat4(1.0f), position + glm::vec3(0.0f, 0.01f, 0.0f)),
                      glm::vec3(scale, 1.0f, scale));
}

glm::mat4 pin_cup_model(const glm::vec3& position, const float cup_scale) {
    return glm::scale(glm::translate(glm::mat4(1.0f), position + glm::vec3(0.0f, 0.09f, 0.0f)),
                      glm::vec3(cup_scale, 1.0f, cup_scale));
}

glm::mat4 panel_model(const glm::vec3& center, const float yaw_degrees, const glm::vec3& scale) {
    glm::mat4 model = glm::translate(glm::mat4(1.0f), center);
    model = glm::rotate(model, glm::radians(yaw_degrees), glm::vec3(0.0f, 1.0f, 0.0f));
    return glm::scale(model, scale);
}

glm::mat4 world_panel_model(const glm::vec3& center,
                            const glm::vec3& axis_x,
                            const float local_z_rotation,
                            const glm::vec2& half_size) {
    glm::mat4 model = glm::translate(glm::mat4(1.0f), center);
    model = glm::rotate(model, axis_x_yaw_radians(axis_x), glm::vec3(0.0f, 1.0f, 0.0f));
    model = glm::rotate(model, local_z_rotation, glm::vec3(0.0f, 0.0f, 1.0f));
    return glm::scale(model, glm::vec3(half_size, 1.0f));
}

void append_ground_marker(world_marker_batch& batch,
                          const glm::vec3& position,
                          const float scale,
                          const glm::vec3& color) {
    batch.append_disc(ground_marker_model(position, scale), color, no_glow, true);
}

glm::mat4 ground_rect_model(const glm::vec3& center, const glm::vec3& down_hole, const glm::vec2& half_size) {
    const glm::vec3 along = safe_normalize(horizontal(down_hole), glm::vec3(0.0f, 0.0f, 1.0f));
    const glm::vec3 across = glm::normalize(glm::cross(along, world_up));
    glm::mat4 model(1.0f);
    model[0] = glm::vec4(across * half_size.x, 0.0f);
    model[1] = glm::vec4(along * half_size.y, 0.0f);
    model[2] = glm::vec4(world_up, 0.0f);
    model[3] = glm::vec4(center, 1.0f);
    return model;
}

void append_tee_box(world_marker_batch& batch, const tee_box& box, const marker_colors& colors) {
    const glm::vec3 along = box.down_hole;
    const glm::vec3 across = glm::normalize(glm::cross(along, world_up));
    const glm::vec2 half(box.half_width, box.half_length);

    // The top in grout colour, and the four sides down into the ground.
    batch.append_quad(ground_rect_model(box.center, along, half), colors.tee_grout, no_glow, true);
    const float side_half_height = (box.center.y - box.bottom + tee_side_bury) * 0.5f;
    const glm::vec3 side_middle(box.center.x, box.center.y - side_half_height, box.center.z);
    for (const float sign : {-1.0f, 1.0f}) {
        batch.append_quad(world_panel_model(side_middle + along * (half.y * sign), across, 0.0f,
                                            glm::vec2(half.x, side_half_height)),
                          colors.tee_grout, no_glow, true);
        batch.append_quad(world_panel_model(side_middle + across * (half.x * sign), along, 0.0f,
                                            glm::vec2(half.y, side_half_height)),
                          colors.tee_grout, no_glow, true);
    }

    // Whole tiles across the top, as near tee_tile_size as fits.
    const int tiles_across = std::max(1, static_cast<int>(std::lround(2.0f * half.x / tee_tile_size)));
    const int tiles_along = std::max(1, static_cast<int>(std::lround(2.0f * half.y / tee_tile_size)));
    const glm::vec2 tile(2.0f * half.x / static_cast<float>(tiles_across), 2.0f * half.y / static_cast<float>(tiles_along));
    const glm::vec2 tile_half = glm::max(tile * 0.5f - glm::vec2(tee_grout_gap * 0.5f), glm::vec2(0.0f));
    for (int row = 0; row < tiles_along; ++row) {
        for (int column = 0; column < tiles_across; ++column) {
            const float x = (static_cast<float>(column) + 0.5f) * tile.x - half.x;
            const float y = (static_cast<float>(row) + 0.5f) * tile.y - half.y;
            const glm::vec3 center = box.center + across * x + along * y + world_up * tee_tile_lift;
            batch.append_quad(ground_rect_model(center, along, tile_half), colors.tee_tile, no_glow, true);
        }
    }

    const glm::vec2 mat_half(half.x * tee_mat_share_across, half.y * tee_mat_share_along);
    batch.append_quad(ground_rect_model(box.center + world_up * tee_mat_lift, along, mat_half), colors.tee_mat, no_glow, true);
}

void append_pin_cup(world_marker_batch& batch, const glm::vec3& position, const float cup_radius_meters,
                    const marker_colors& colors, const glow_floors& glow) {
    // The cup never writes depth so the ball and flagstick stay visible through it.
    batch.append_disc(pin_cup_model(position, cup_radius_meters * 2.0f), colors.cup, glow.cup, false);
}

void append_pin_flagstick(world_marker_batch& batch, const glm::vec3& position, const float pin_visual_height_meters,
                          const marker_colors& colors, const glow_floors& glow, const flag_pose& flag) {
    const float pin_height = std::max(0.1f, pin_visual_height_meters);
    const glm::vec3 pin_base = position + glm::vec3(0.0f, pin_height * 0.5f, 0.0f);
    const glm::vec3 pole_scale(pole_half_width, pin_height * 0.5f, 1.0f);
    batch.append_quad(panel_model(pin_base, 0.0f, pole_scale), colors.pin_pole, glow.pin_pole, true);
    batch.append_quad(panel_model(pin_base, 90.0f, pole_scale), colors.pin_pole, glow.pin_pole, true);

    // Hung from just under the pole's top, flying out from it at `angle`
    // from straight down, its fly end waving across the wind.
    const glm::vec3 downwind = safe_normalize(horizontal(flag.wind), glm::vec3(1.0f, 0.0f, 0.0f));
    const glm::vec3 across(-downwind.z, 0.0f, downwind.x);
    const float angle = glm::mix(flag_hang_radians, glm::half_pi<float>(), clamp01(flag.lift));
    const glm::vec3 out = downwind * std::sin(angle) - world_up * std::cos(angle);
    // Each flag at its own point of the wave, so a course's flags never flap together.
    const float phase = flag.phase + position.x * flag_phase_per_metre + position.z * flag_phase_per_metre * 1.7f;
    const glm::vec3 hoist_top = position + glm::vec3(0.0f, pin_height * 0.86f + flag_half_height, 0.0f);
    const glm::vec3 hoist_bottom = hoist_top - world_up * (2.0f * flag_half_height);
    const auto along = [&](const float share) {
        const float wave = flag.flap * flag_length * share * std::sin(phase - glm::two_pi<float>() * flag_waves * share);
        return out * (flag_length * share) + across * wave;
    };
    const glm::vec3 middle = along(0.5f);
    const glm::vec3 fly = along(1.0f);
    batch.append_world_quad(hoist_top, hoist_bottom, hoist_bottom + middle, hoist_top + middle, colors.flag, glow.flag, true);
    batch.append_world_quad(hoist_top + middle, hoist_bottom + middle, hoist_bottom + fly, hoist_top + fly, colors.flag, glow.flag,
                            true);
}

void append_lying_flagstick(world_marker_batch& batch, const glm::vec3& foot, const glm::vec3& tip,
                            const marker_colors& colors, const glow_floors& glow) {
    const glm::vec3 along = safe_normalize(tip - foot, glm::vec3(1.0f, 0.0f, 0.0f));
    const glm::vec3 across = safe_normalize(glm::cross(world_up, along), glm::vec3(0.0f, 0.0f, 1.0f)) * lying_half_width;
    const glm::vec3 lift = world_up * lying_lift;
    batch.append_world_quad(foot + lift - across, tip + lift - across, tip + lift + across, foot + lift + across, colors.pin_pole,
                            glow.pin_pole, true);
    // The flag lies flat by the tip, out to one side.
    const glm::vec3 side = glm::normalize(across);
    const glm::vec3 hoist = tip - along * (2.0f * flag_half_height) + lift * 2.0f;
    batch.append_world_quad(hoist, tip + lift * 2.0f, tip + lift * 2.0f + side * flag_length,
                            hoist + side * flag_length, colors.flag, glow.flag, true);
}

namespace {
// The stick and the face's line, wide enough to read at the picture's
// lines, lifted off the ground they lie on; the shoes' size.
constexpr float stick_half_width = 0.012f;
constexpr float face_line_half_width = 0.008f;
constexpr float tick_half_length = 0.05f;
constexpr float mark_lift = 0.004f;
constexpr glm::vec3 shoe_half_size(0.05f, 0.045f, 0.14f);

// A flat strip on the ground from `a` to `b`.
void append_ground_strip(world_marker_batch& batch, const glm::vec3& a, const glm::vec3& b, const float half_width,
                         const glm::vec3& color, const float glow) {
    const glm::vec3 across = yaw_left(safe_normalize(horizontal(b - a), glm::vec3(0.0f, 0.0f, 1.0f))) * half_width;
    const glm::vec3 lift = world_up * mark_lift;
    batch.append_world_quad(a - across + lift, b - across + lift, b + across + lift, a + across + lift, color, glow, true);
}
}

void append_address_marks(world_marker_batch& batch, const render_address_marks& marks, const scene_colors& colors) {
    const marker_colors& markers = colors.markers;
    append_ground_strip(batch, marks.stick_back, marks.stick_front, stick_half_width, markers.alignment_stick,
                        colors.glow.alignment_stick);
    const glm::vec3 across = yaw_left(safe_normalize(horizontal(marks.stick_front - marks.stick_back), glm::vec3(0.0f, 0.0f, 1.0f)));
    append_ground_strip(batch, marks.stock_tick - across * tick_half_length, marks.stock_tick + across * tick_half_length,
                        stick_half_width, markers.alignment_stick, colors.glow.alignment_stick);
    append_ground_strip(batch, marks.face_heel, marks.face_toe, face_line_half_width, markers.face_line, colors.glow.face_line);
    for (const glm::vec3& foot : {marks.lead_foot, marks.trail_foot}) {
        // Toes towards the ball.
        const glm::vec3 toes = safe_normalize(horizontal(marks.ball - foot), glm::vec3(0.0f, 0.0f, 1.0f));
        const glm::mat4 shoe = posed_model(foot + world_up * shoe_half_size.y, world_up, toes, shoe_half_size);
        batch.append_sphere(shoe, colors.figures.shoes, 0.0f, true);
    }
}

void build_tee_box_batch(world_marker_batch& batch, const std::vector<tee_box>* boxes, const marker_colors& colors) {
    batch.clear();
    if (boxes != nullptr) {
        for (const tee_box& box : *boxes) {
            append_tee_box(batch, box, colors);
        }
    }
}

void build_world_marker_batch(world_marker_batch& batch, const world_marker_scene& scene, const scene_colors& colors) {
    const marker_colors& marks = colors.markers;
    batch.clear();

    const std::vector<glm::vec3> none;
    const auto markers = [&none](const std::vector<glm::vec3>* positions) -> const std::vector<glm::vec3>& {
        return positions != nullptr ? *positions : none;
    };
    const auto flag_out = [&](const glm::vec3& cup) {
        const std::vector<glm::vec3>& out = markers(scene.flags_out);
        return std::find(out.begin(), out.end(), cup) != out.end();
    };
    if (scene.show_hole) {
        append_pin_cup(batch, scene.pin_position, scene.cup_radius_meters, marks, colors.glow);
        if (!flag_out(scene.pin_position)) {
            append_pin_flagstick(batch, scene.pin_position, scene.pin_visual_height_meters, marks, colors.glow, scene.flag);
        }
    }
    if (scene.lying_flags != nullptr) {
        for (const auto& [foot, tip] : *scene.lying_flags) {
            append_lying_flagstick(batch, foot, tip, marks, colors.glow);
        }
    }

    for (const glm::vec3& position : markers(scene.collectible_markers)) {
        append_ground_marker(batch, position, collectible_scale, marks.collectible);
    }
    const std::vector<glm::vec3>& pins = markers(scene.pin_markers);
    for (const glm::vec3& position : pins) {
        append_pin_cup(batch, position, scene.cup_radius_meters, marks, colors.glow);
    }
    for (const glm::vec3& position : pins) {
        if (!flag_out(position)) {
            append_pin_flagstick(batch, position, scene.pin_visual_height_meters, marks, colors.glow, scene.flag);
        }
    }

    if (scene.address_marks != nullptr) {
        append_address_marks(batch, *scene.address_marks, colors);
    }
    if (scene.remote_avatars != nullptr) {
        for (const render_remote_avatar& avatar : *scene.remote_avatars) {
            append_remote_avatar(batch, avatar, scene.avatar_eye_height, scene.ball_visual_radius_meters, colors);
        }
    }
    if (scene.remote_balls != nullptr) {
        for (const render_remote_ball& ball : *scene.remote_balls) {
            append_remote_ball(batch, ball, scene.ball_visual_radius_meters, colors.figures, colors.glow.ball);
        }
    }
}
