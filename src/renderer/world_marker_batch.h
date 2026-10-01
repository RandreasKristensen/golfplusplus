#pragma once

#include <cstddef>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

// GL-free CPU batch for small, repeated, flat-coloured world geometry: ground
// markers, pin cups, flagsticks, aim dots, the swing club and the golf cart.
// Each piece is transformed on the CPU into world-space triangles carrying
// its colour, appended in submission order, and grouped into runs that share
// render state; the GL side uploads one buffer and draws once per run.
//
// Only opaque geometry belongs here: runs never blend, so submission order
// matters only for depth-write changes and exact depth ties.

// Uploaded straight into a GL buffer: position.xyz then color.rgba.
struct world_marker_vertex {
    glm::vec3 position{0.0f};
    glm::vec4 color{1.0f};
};

// Consecutive vertices drawn with the same depth-write setting.
struct world_marker_run {
    bool depth_write = true;
    std::size_t first = 0;
    std::size_t count = 0;
};

// `segments` triangles fanned around the origin in the XZ plane, radius 0.5.
std::vector<glm::vec3> make_unit_disc_positions(int segments);
// Two triangles covering [-1, 1] in XY at z = 0.
std::vector<glm::vec3> make_unit_quad_positions();

inline constexpr int world_marker_disc_segments = 18;

class world_marker_batch {
public:
    world_marker_batch();

    // Empties vertices and runs but keeps their capacity for the next frame.
    void clear();

    // Unit disc / quad / cylinder / sphere transformed by `model`. Alpha is
    // stored as given; blending stays off.
    void append_disc(const glm::mat4& model, const glm::vec3& color, float alpha, bool depth_write);
    void append_quad(const glm::mat4& model, const glm::vec3& color, float alpha, bool depth_write);
    void append_cylinder(const glm::mat4& model, const glm::vec3& color, float alpha, bool depth_write);
    void append_sphere(const glm::mat4& model, const glm::vec3& color, float alpha, bool depth_write);

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
    std::vector<glm::vec3> unit_cylinder_;
    std::vector<glm::vec3> unit_sphere_;
    std::vector<world_marker_vertex> vertices_;
    std::vector<world_marker_run> runs_;
};

glm::mat4 ground_marker_model(const glm::vec3& position, float scale);
glm::mat4 pin_cup_model(const glm::vec3& position, float cup_scale);
glm::mat4 aim_dot_model(const glm::vec3& point, std::size_t index);
glm::mat4 panel_model(const glm::vec3& center, float yaw_degrees, const glm::vec3& scale);
glm::mat4 world_panel_model(const glm::vec3& center,
                            const glm::vec3& axis_x,
                            float local_z_rotation,
                            const glm::vec2& half_size);

// What the marker pass draws this frame. Vector pointers are non-owning and
// may be null (treated as empty).
struct world_marker_scene {
    // The hole being played: tee marker, cup and flag.
    bool show_hole = false;
    glm::vec3 tee_position{0.0f};
    glm::vec3 pin_position{0.0f};
    // Hub markers.
    const std::vector<glm::vec3>* start_markers = nullptr;
    const std::vector<glm::vec3>* tee_markers = nullptr;
    const std::vector<glm::vec3>* pin_markers = nullptr;
    const std::vector<glm::vec3>* collectible_markers = nullptr;

    float cup_radius_meters = 0.0f;
    float pin_visual_height_meters = 0.0f;

    bool show_aim_indicator = false;
    const std::vector<glm::vec3>* aim_arc_points = nullptr;

    bool show_swing_club = false;
    glm::vec3 ball_position{0.0f};
    float ball_visual_radius_meters = 0.0f;
    float aim_angle = 0.0f;
    float swing_power = 0.0f;

    // The cart is drawn in camera-local space (see cart_batch.h).
    bool cart_active = false;
    glm::vec3 camera_position{0.0f};
    glm::vec3 camera_target{0.0f, 0.0f, 1.0f};
};

void append_ground_marker(world_marker_batch& batch, const glm::vec3& position, float scale, const glm::vec3& color);
void append_pin_cup(world_marker_batch& batch, const glm::vec3& position, float cup_radius_meters);
void append_pin_flagstick(world_marker_batch& batch, const glm::vec3& position, float pin_visual_height_meters);
void append_aim_dots(world_marker_batch& batch, const std::vector<glm::vec3>& points);
void append_swing_club(world_marker_batch& batch,
                       const glm::vec3& ball_position,
                       float ball_visual_radius_meters,
                       float aim_angle,
                       float swing_power);

// Clears `batch` and fills it for one frame: cart first, then hole and hub
// markers, aim dots and the swing club. All hub cups come before all hub
// flagsticks: cups never write depth and sit above their own terrain, so the
// order between different holes' cups and flags is never visible, and the
// hub collapses into a few runs instead of two per hole.
void build_world_marker_batch(world_marker_batch& batch, const world_marker_scene& scene);
