#pragma once

#include <cstddef>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

// GL-free CPU batch for small, repeated, flat-colored world geometry: ground
// markers, pin cups, flagsticks, aim dots and the swing club. Every piece used
// to be its own draw call with five uniform sets (model, mvp, color, alpha,
// vertex-color flag) against the terrain shader's flat-color path. Here each
// piece is pre-transformed on the CPU into world-space triangles carrying
// their color, appended in submission order, and grouped into runs that share
// render state. The GL side uploads one buffer and issues one draw per run.
//
// Only opaque geometry belongs here: blending is never enabled for a run, so
// submission order matters only for depth-write changes and exact depth ties.

// Uploaded straight into a GL buffer: position.xyz then color.rgba.
struct world_marker_vertex {
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec4 color = glm::vec4(1.0f);
};

// A contiguous range of vertices drawn with the same render state.
// Consecutive appends with equal state extend the previous run.
struct world_marker_run {
    bool depth_write = true;
    std::size_t first = 0;
    std::size_t count = 0;
};

// Same geometry the renderer's old marker VAO held: `segments` triangles
// fanned around the origin in the XZ plane, radius 0.5.
std::vector<glm::vec3> make_unit_disc_positions(int segments);

// Two triangles covering [-1, 1] in XY at z = 0, in the old screen quad order.
std::vector<glm::vec3> make_unit_quad_positions();

inline constexpr int world_marker_disc_segments = 18;

struct world_marker_batch {
    world_marker_batch();

    // Empties vertices and runs but keeps their capacity for the next frame.
    void clear();
    void reserve(std::size_t vertex_count, std::size_t run_count);

    // `model` is applied to the unit disc / unit quad exactly like the old
    // per-draw u_model did. Alpha is written as-is; blending stays off.
    void append_disc(const glm::mat4& model, const glm::vec3& color, float alpha, bool depth_write);
    void append_quad(const glm::mat4& model, const glm::vec3& color, float alpha, bool depth_write);

    const std::vector<world_marker_vertex>& vertices() const { return vertices_; }
    const std::vector<world_marker_run>& runs() const { return runs_; }
    bool empty() const { return vertices_.empty(); }

private:
    void append_triangles(const std::vector<glm::vec3>& local_positions,
                          const glm::mat4& model,
                          const glm::vec4& color,
                          bool depth_write);

    std::vector<glm::vec3> unit_disc_;
    std::vector<glm::vec3> unit_quad_;
    std::vector<world_marker_vertex> vertices_;
    std::vector<world_marker_run> runs_;
};

// Model matrices, reproduced exactly from the old per-draw code.
glm::mat4 ground_marker_model(const glm::vec3& position, float scale);
glm::mat4 pin_cup_model(const glm::vec3& position, float cup_scale);
glm::mat4 aim_dot_model(const glm::vec3& point, std::size_t index);
glm::mat4 panel_model(const glm::vec3& center, float yaw_degrees, const glm::vec3& scale);
glm::mat4 world_panel_model(const glm::vec3& center,
                            const glm::vec3& axis_x,
                            float local_z_rotation,
                            const glm::vec2& half_size);

// Everything the world-marker pass reads from the frame's render data. The
// vector pointers are non-owning and may be null (treated as empty).
struct world_marker_scene {
    bool show_primary_hole_markers = true;
    glm::vec3 tee_position = glm::vec3(0.0f);
    glm::vec3 pin_position = glm::vec3(0.0f);
    const std::vector<glm::vec3>* start_markers = nullptr;
    const std::vector<glm::vec3>* tee_markers = nullptr;
    const std::vector<glm::vec3>* pin_markers = nullptr;

    float cup_radius = 0.65f;
    float cup_visual_radius_meters = 0.75f;
    float pin_visual_height_meters = 2.10f;

    bool show_aim_indicator = false;
    const std::vector<glm::vec3>* aim_arc_points = nullptr;

    bool show_swing_club = false;
    glm::vec3 ball_position = glm::vec3(0.0f);
    float ball_visual_radius_meters = 0.10f;
    float aim_angle = 0.0f;
    float swing_power = 0.0f;
};

// Pieces, in the order the old render_scene drew them.
void append_ground_marker(world_marker_batch& batch, const glm::vec3& position, float scale, const glm::vec3& color);
void append_pin_cup(world_marker_batch& batch, const glm::vec3& position, float cup_radius, float cup_visual_radius_meters);
void append_pin_flagstick(world_marker_batch& batch, const glm::vec3& position, float pin_visual_height_meters);
void append_aim_dots(world_marker_batch& batch, const std::vector<glm::vec3>& points);
void append_swing_club(world_marker_batch& batch,
                       const glm::vec3& ball_position,
                       float ball_visual_radius_meters,
                       float aim_angle,
                       float swing_power);

// Clears `batch` and fills it for one frame. Draw order matches the old
// per-draw code except that hub pin cups (depth write off) are all submitted
// before the hub flagsticks, so the whole hub collapses into three runs; see
// the comment in the implementation for why that is visually identical.
void build_world_marker_batch(world_marker_batch& batch, const world_marker_scene& scene);

// Grow-only GPU buffer sizing: returns `current` when `required` fits,
// otherwise at least double `current` (and at least `required`).
std::size_t grow_buffer_capacity(std::size_t current, std::size_t required);
