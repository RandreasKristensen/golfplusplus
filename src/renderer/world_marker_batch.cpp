#include "renderer/world_marker_batch.h"

#include "physics/vector_math.h"
#include "renderer/camera_local.h"
#include "renderer/primitive_mesh.h"
#include "renderer/lying_body.h"
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
// Over the box's top: the tiles, then the mat.
constexpr float tee_tile_lift = 0.01f;
constexpr float tee_mat_lift = 0.02f;
// The mat's share of the box's top, across and along the hole.
constexpr float tee_mat_share_across = 0.7f;
constexpr float tee_mat_share_along = 0.75f;
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
// A cup's hole: its round in this many sides; the cylinder that finds its
// opening reaches this far over and under its centre, more than a green
// rises or falls across a cup.
constexpr int cup_hole_segments = 24;
constexpr float cup_cut_reach = 0.05f;

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
    for (std::vector<mesh_instance>& list : instances_) { list.clear(); }
    instance_runs_.clear();
    open_run_.fill(0);
    part_ = marker_part::cups;
    part_ends_.fill(0);
    parts_ended_ = false;
    break_run_ = false;
    layer_ = 0;
}

void world_marker_batch::end_part(const marker_part part) {
    part_ends_[static_cast<std::size_t>(part)] = vertices_.size();
    parts_ended_ = true;
    break_run_ = true;
    open_run_.fill(0);
    part_ = static_cast<marker_part>(std::min(static_cast<std::size_t>(part) + 1, static_cast<std::size_t>(marker_part::count) - 1));
}

bool world_marker_batch::shown_in(const world_marker_run& run, const marker_view view) const {
    if (!parts_ended_) {
        return true;
    }
    // The first part whose end lies past the run's start.
    std::size_t part = 0;
    while (part + 1 < part_ends_.size() && part_ends_[part] <= run.first) {
        ++part;
    }
    return shown_in(static_cast<marker_part>(part), view);
}

bool world_marker_batch::shown_in(const marker_part part, const marker_view view) const {
    if (!parts_ended_) {
        return true;
    }
    switch (part) {
    case marker_part::address_feet: return view == marker_view::scene || view == marker_view::zoom;
    case marker_part::my_figure: return view == marker_view::mirror || view == marker_view::sun_shadow;
    case marker_part::poles: return view != marker_view::zoom;
    case marker_part::zoom_poles: return view == marker_view::zoom;
    case marker_part::flagsticks:
    case marker_part::avatars: return true;
    case marker_part::cups:
    case marker_part::collectibles:
    case marker_part::balls: return view != marker_view::sun_shadow;
    case marker_part::count: break;
    }
    return false;
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

void world_marker_batch::append_instance(const prop_mesh mesh, const glm::mat4& model, const glm::vec3& color, const float glow) {
    const std::size_t m = static_cast<std::size_t>(mesh);
    if (open_run_[m] == 0) {
        instance_runs_.push_back(world_marker_instance_run{part_, mesh, instances_[m].size(), 0});
        open_run_[m] = instance_runs_.size();
    }
    instances_[m].push_back(make_mesh_instance(model, color, glow));
    ++instance_runs_[open_run_[m] - 1].count;
}

std::size_t world_marker_batch::instance_count() const {
    std::size_t count = 0;
    for (const std::vector<mesh_instance>& list : instances_) { count += list.size(); }
    return count;
}

std::size_t world_marker_batch::instance_base(const prop_mesh mesh) const {
    std::size_t base = 0;
    for (std::size_t m = 0; m < static_cast<std::size_t>(mesh); ++m) { base += instances_[m].size(); }
    return base;
}

void world_marker_batch::join_instances(std::vector<mesh_instance>& out) const {
    out.clear();
    for (const std::vector<mesh_instance>& list : instances_) { out.insert(out.end(), list.begin(), list.end()); }
}

std::vector<world_marker_vertex> world_marker_batch::instanced_vertices() const {
    std::vector<world_marker_vertex> vertices;
    for (const prop_mesh mesh : {prop_mesh::cylinder, prop_mesh::sphere, prop_mesh::quad}) {
        for (const mesh_instance& instance : instances(mesh)) {
            for (std::size_t i = 0; i < meshes_.vertex_count(mesh); ++i) {
                vertices.push_back(world_marker_vertex{placed_position(instance, meshes_, mesh, i), instance.color, instance.glow});
            }
        }
    }
    return vertices;
}

void world_marker_batch::append_triangles(const std::vector<glm::vec3>& local_positions,
                                          const glm::mat4& model,
                                          const glm::vec3& color,
                                          const float glow,
                                          const bool depth_write) {
    if (local_positions.empty()) {
        return;
    }

    world_marker_run& run = run_for(depth_write, marker_stencil::none);
    for (const glm::vec3& local : local_positions) {
        world_marker_vertex vertex;
        vertex.position = glm::vec3(model * glm::vec4(local, 1.0f));
        vertex.color = color;
        vertex.glow = glow;
        vertices_.push_back(vertex);
    }
    run.count += local_positions.size();
}

void world_marker_batch::append_world_quad(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& d,
                                           const glm::vec3& color, const float glow, const bool depth_write) {
    world_marker_run& run = run_for(depth_write, marker_stencil::none);
    for (const glm::vec3& position : {a, b, c, a, c, d}) {
        vertices_.push_back(world_marker_vertex{position, color, glow});
    }
    run.count += 6;
}

void world_marker_batch::append_world_triangles(const std::vector<glm::vec3>& corners, const glm::vec3& color,
                                                const float glow, const marker_stencil stencil) {
    if (corners.empty()) {
        return;
    }
    world_marker_run& run = run_for(stencil != marker_stencil::mark_holes, stencil);
    for (const glm::vec3& position : corners) {
        vertices_.push_back(world_marker_vertex{position, color, glow});
    }
    run.count += corners.size();
}

world_marker_run& world_marker_batch::run_for(const bool depth_write, const marker_stencil stencil) {
    if (runs_.empty() || break_run_ || runs_.back().depth_write != depth_write || runs_.back().stencil != stencil ||
        runs_.back().layer != layer_) {
        break_run_ = false;
        world_marker_run run;
        run.depth_write = depth_write;
        run.stencil = stencil;
        run.layer = layer_;
        run.first = vertices_.size();
        runs_.push_back(run);
    }
    return runs_.back();
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

namespace {
glm::vec3 collectible_color(const std::uint32_t index, const marker_colors& colors) {
    // Knuth's multiplicative hash: its top bits spread neighbouring indices
    // evenly over 0..1, so about the paint share of a course is painted.
    const std::uint32_t hash = index * 2654435761u;
    const float pick = static_cast<float>(hash >> 8) / 16777216.0f;
    if (pick >= colors.collectible_paint_share || colors.collectible_paints.empty()) {
        return colors.ball;
    }
    return colors.collectible_paints[(hash >> 4) % colors.collectible_paints.size()];
}
}

void append_collectible(world_marker_batch& batch, const render_collectible& collectible, const float radius,
                        const marker_colors& colors, const float glow) {
    const glm::vec3 centre = collectible.position + world_up * radius;
    batch.append_sphere(glm::scale(glm::translate(glm::mat4(1.0f), centre), glm::vec3(radius)),
                        collectible_color(collectible.index, colors), glow, true);
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

namespace {
void append_tee_box_layer(world_marker_batch& batch, const tee_box& box, const marker_colors& colors,
                          const tee_box_layer layer) {
    const glm::vec3 along = box.down_hole;
    const glm::vec3 across = glm::normalize(glm::cross(along, world_up));
    const glm::vec2 half(box.half_width, box.half_length);
    batch.set_layer(static_cast<int>(layer));
    if (layer == tee_box_layer::mat) {
        const glm::vec2 mat_half(half.x * tee_mat_share_across, half.y * tee_mat_share_along);
        batch.append_quad(ground_rect_model(box.center + world_up * tee_mat_lift, along, mat_half), colors.tee_mat, no_glow, true);
        return;
    }
    if (layer == tee_box_layer::tiles) {
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
        return;
    }

    // The top in grout colour, on the levelled ground.
    batch.append_quad(ground_rect_model(box.center, along, half), colors.tee_grout, no_glow, true);
}
}

void append_tee_box(world_marker_batch& batch, const tee_box& box, const marker_colors& colors) {
    for (const tee_box_layer layer : {tee_box_layer::grout, tee_box_layer::tiles, tee_box_layer::mat}) {
        append_tee_box_layer(batch, box, colors, layer);
    }
    batch.set_layer(0);
}

void append_cup_holes(world_marker_batch& batch, const std::vector<glm::vec3>& centres, const float radius,
                      const float depth, const marker_colors& colors, const glow_floors& glow) {
    // Round a cup's axis, `height` over its centre.
    const auto rim = [radius](const glm::vec3& centre, const int side, const float height) {
        const float angle = glm::two_pi<float>() * static_cast<float>(side) / static_cast<float>(cup_hole_segments);
        return centre + glm::vec3(std::cos(angle) * radius, height, std::sin(angle) * radius);
    };
    std::vector<glm::vec3> marks;
    std::vector<glm::vec3> insides;
    for (const glm::vec3& centre : centres) {
        const glm::vec3 top = centre + world_up * cup_cut_reach;
        const glm::vec3 under = centre - world_up * cup_cut_reach;
        const glm::vec3 bottom = centre - world_up * depth;
        for (int side = 0; side < cup_hole_segments; ++side) {
            const glm::vec3 low0 = rim(centre, side, -cup_cut_reach);
            const glm::vec3 low1 = rim(centre, side + 1, -cup_cut_reach);
            const glm::vec3 high0 = rim(centre, side, cup_cut_reach);
            const glm::vec3 high1 = rim(centre, side + 1, cup_cut_reach);
            // The marking cylinder, every face turned outwards.
            marks.insert(marks.end(), {low0, high1, low1, low0, high0, high1, top, high1, high0, under, low0, low1});
            // The wall turned in, from the bottom up past the ground, and the bottom turned up.
            const glm::vec3 deep0 = rim(centre, side, -depth);
            const glm::vec3 deep1 = rim(centre, side + 1, -depth);
            insides.insert(insides.end(), {deep0, deep1, high1, deep0, high1, high0, bottom, deep1, deep0});
        }
    }
    batch.append_world_triangles(marks, colors.cup, glow.cup, marker_stencil::mark_holes);
    batch.append_world_triangles(insides, colors.cup, glow.cup, marker_stencil::hole_insides);
}

namespace {
float marker_height(const std::vector<float>* heights, const std::size_t index, const float fallback) {
    return heights != nullptr && index < heights->size() ? (*heights)[index] : fallback;
}
}

float flagstick_draw_radius(const world_marker_scene& scene, const glm::vec3& position, const float height_meters,
                            const float pixel_per_meter) {
    // The farther end of the pole is the narrowest on screen.
    const float distance = std::max(glm::length(position - scene.camera_position),
                                    glm::length(position + world_up * height_meters - scene.camera_position));
    const float across_flats = std::cos(glm::pi<float>() / static_cast<float>(primitive_cylinder_segments));
    return std::max(scene.flagstick_radius_meters, 0.5f * distance * pixel_per_meter / across_flats);
}

void append_pin_pole(world_marker_batch& batch, const glm::vec3& position, const float pin_visual_height_meters,
                     const marker_colors& colors, const glow_floors& glow, const float radius_meters) {
    const float pin_height = std::max(0.1f, pin_visual_height_meters);
    batch.append_cylinder(glm::scale(glm::translate(glm::mat4(1), position),
                                    glm::vec3(radius_meters, pin_height, radius_meters)),
                          colors.pin_pole, glow.pin_pole, true);
}

void append_pin_flagstick(world_marker_batch& batch, const glm::vec3& position, const float pin_visual_height_meters,
                          const marker_colors& colors, const glow_floors& glow, const flag_pose& flag,
                          const float radius_meters) {
    append_pin_pole(batch, position, pin_visual_height_meters, colors, glow, radius_meters);
    append_pin_flag(batch, position, pin_visual_height_meters, colors, glow, flag);
}

void append_pin_flag(world_marker_batch& batch, const glm::vec3& position, const float pin_visual_height_meters,
                     const marker_colors& colors, const glow_floors& glow, const flag_pose& flag) {
    const float pin_height = std::max(0.1f, pin_visual_height_meters);

    // Hung from just under the pole's top, flying out from it at `angle`
    // from straight down, its fly end waving across the wind.
    const glm::vec3 downwind = safe_normalize(horizontal(flag.wind), glm::vec3(1.0f, 0.0f, 0.0f));
    const glm::vec3 across(-downwind.z, 0.0f, downwind.x);
    const float angle = glm::mix(flag_hang_radians, glm::half_pi<float>(), clamp01(flag.lift));
    const glm::vec3 out = downwind * std::sin(angle) - world_up * std::cos(angle);
    // Each flag at its own point of the wave, so a course's flags never flap together.
    const float phase = flag.phase + position.x * flag_phase_per_metre + position.z * flag_phase_per_metre * 1.7f;
    const glm::vec3 hoist_top = position + glm::vec3(0.0f, std::min(pin_height * 0.86f + flag_half_height, pin_height), 0.0f);
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
// My shoes at address.
constexpr glm::vec3 shoe_half_size(0.05f, 0.045f, 0.14f);
}

void append_address_feet(world_marker_batch& batch, const render_address_feet& marks, const scene_colors& colors) {
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
        for (const tee_box_layer layer : {tee_box_layer::grout, tee_box_layer::tiles, tee_box_layer::mat}) {
            for (const tee_box& box : *boxes) {
                append_tee_box_layer(batch, box, colors, layer);
            }
        }
    }
    batch.set_layer(0);
}

namespace {
// Every cup's opening, the hole's and the course's.
void append_cups(world_marker_batch& batch, const world_marker_scene& scene, const scene_colors& colors) {
    std::vector<glm::vec3> cups;
    if (scene.pin_markers != nullptr) {
        cups = *scene.pin_markers;
    }
    if (scene.show_hole) {
        cups.push_back(scene.pin_position);
    }
    append_cup_holes(batch, cups, scene.cup_radius_meters, scene.cup_depth_meters, colors.markers, colors.glow);
}
}

void build_cup_batch(world_marker_batch& batch, const world_marker_scene& scene, const scene_colors& colors) {
    batch.clear();
    append_cups(batch, scene, colors);
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
    if (!scene.cups_apart) {
        append_cups(batch, scene, colors);
    }
    batch.end_part(marker_part::cups);
    // Every standing flagstick: the hole's, then the course's.
    std::vector<std::pair<glm::vec3, float>> standing;
    if (scene.show_hole && !flag_out(scene.pin_position)) {
        standing.emplace_back(scene.pin_position, scene.pin_visual_height_meters);
    }
    const std::vector<glm::vec3>& others = markers(scene.pin_markers);
    for (std::size_t i = 0; i < others.size(); ++i) {
        if (!flag_out(others[i])) {
            standing.emplace_back(others[i], marker_height(scene.pin_marker_heights, i, scene.pin_visual_height_meters));
        }
    }
    for (const auto& [position, height] : standing) {
        append_pin_pole(batch, position, height, marks, colors.glow,
                        flagstick_draw_radius(scene, position, height, scene.pixel_per_meter));
    }
    batch.end_part(marker_part::poles);
    if (scene.zoom_pixel_per_meter > 0.0f) {
        for (const auto& [position, height] : standing) {
            append_pin_pole(batch, position, height, marks, colors.glow,
                            flagstick_draw_radius(scene, position, height, scene.zoom_pixel_per_meter));
        }
    }
    batch.end_part(marker_part::zoom_poles);

    for (const auto& [position, height] : standing) {
        append_pin_flag(batch, position, height, marks, colors.glow, scene.flag);
    }
    if (scene.lying_flags != nullptr) {
        for (const auto& [foot, tip] : *scene.lying_flags) {
            append_lying_flagstick(batch, foot, tip, marks, colors.glow);
        }
    }
    batch.end_part(marker_part::flagsticks);
    if (scene.collectibles != nullptr) {
        for (const render_collectible& collectible : *scene.collectibles) {
            append_collectible(batch, collectible, scene.ball_visual_radius_meters, marks, colors.glow.ball);
        }
    }
    batch.end_part(marker_part::collectibles);
    if (scene.remote_avatars != nullptr) {
        for (const render_remote_avatar& avatar : *scene.remote_avatars) {
            append_remote_avatar(batch, avatar, scene.avatar_eye_height, scene.ball_visual_radius_meters, colors);
        }
    }
    if (scene.lying_bodies != nullptr) {
        for (const render_lying_body& body : *scene.lying_bodies) {
            append_lying_body(batch, body, colors);
        }
    }
    batch.end_part(marker_part::avatars);
    if (scene.remote_balls != nullptr) {
        for (const render_remote_ball& ball : *scene.remote_balls) {
            append_remote_ball(batch, ball, scene.ball_visual_radius_meters, colors.figures, colors.glow.ball);
        }
    }
    batch.end_part(marker_part::balls);
    if (scene.address_feet != nullptr) {
        append_address_feet(batch, *scene.address_feet, colors);
    }
    batch.end_part(marker_part::address_feet);
    if (scene.my_figure != nullptr) {
        append_remote_avatar(batch, *scene.my_figure, scene.avatar_eye_height, scene.ball_visual_radius_meters, colors);
    }
    batch.end_part(marker_part::my_figure);
}
