#pragma once

#include "game/lighting_loader.h"
#include "game/tee_box.h"

#include <cstddef>
#include <utility>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

// GL-free CPU batch for small, repeated, flat-coloured world geometry: ground
// markers, tee boxes, pin cups, flagsticks, aim dots, and other players and
// their balls (remote_avatar_batch.h). Models are drawn lit (prop_batch.h).
// Each piece is transformed on the CPU into world-space triangles carrying
// its colour, appended in submission order, and grouped into runs that share
// render state; the GL side uploads one buffer and draws once per run.
//
// Only opaque geometry belongs here: runs never blend, so submission order
// matters only for depth-write changes and exact depth ties.

// Uploaded straight into a GL buffer: position.xyz, color.rgb, then glow:
// the least light the piece shows (lighting.glsl's flat_light), 0 for none.
struct world_marker_vertex {
    glm::vec3 position{0.0f};
    glm::vec3 color{1.0f};
    float glow = 0.0f;
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

struct render_remote_avatar;
struct render_remote_ball;

class world_marker_batch {
public:
    world_marker_batch();

    // Empties vertices and runs but keeps their capacity for the next frame.
    void clear();

    // Unit disc / quad / cylinder / sphere transformed by `model`, with its
    // glow floor (world_marker_vertex).
    void append_disc(const glm::mat4& model, const glm::vec3& color, float glow, bool depth_write);
    void append_quad(const glm::mat4& model, const glm::vec3& color, float glow, bool depth_write);
    void append_cylinder(const glm::mat4& model, const glm::vec3& color, float glow, bool depth_write);
    void append_sphere(const glm::mat4& model, const glm::vec3& color, float glow, bool depth_write);
    // The quad a, b, c, d (in order round it), already in world space.
    void append_world_quad(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& d,
                           const glm::vec3& color, float glow, bool depth_write);

    const std::vector<world_marker_vertex>& vertices() const { return vertices_; }
    const std::vector<world_marker_run>& runs() const { return runs_; }
    bool empty() const { return vertices_.empty(); }

private:
    void append_triangles(const std::vector<glm::vec3>& local_positions,
                          const glm::mat4& model,
                          const glm::vec3& color,
                          float glow,
                          bool depth_write);

    std::vector<glm::vec3> unit_disc_;
    std::vector<glm::vec3> unit_quad_;
    std::vector<glm::vec3> unit_cylinder_;
    std::vector<glm::vec3> unit_sphere_;
    std::vector<world_marker_vertex> vertices_;
    std::vector<world_marker_run> runs_;
};

glm::mat4 ground_marker_model(const glm::vec3& position, float scale);
// A rectangle lying flat at `center`: local x across the hole, local y along
// `down_hole` (horizontal), scaled by `half_size`.
glm::mat4 ground_rect_model(const glm::vec3& center, const glm::vec3& down_hole, const glm::vec2& half_size);
glm::mat4 pin_cup_model(const glm::vec3& position, float cup_scale);
glm::mat4 panel_model(const glm::vec3& center, float yaw_degrees, const glm::vec3& scale);
glm::mat4 world_panel_model(const glm::vec3& center,
                            const glm::vec3& axis_x,
                            float local_z_rotation,
                            const glm::vec2& half_size);

// How every flag flies this frame (render_weather): along `wind` (level;
// none: along +x), `lift` of the way from hanging (0) to straight out (1),
// waving at `phase` radians by `flap` of its length.
struct flag_pose {
    glm::vec3 wind{0.0f};
    float lift = 0.0f;
    float phase = 0.0f;
    float flap = 0.0f;
};

// Standing over my ball, the setup on the ground (game/address_view.h
// address_marks): the alignment stick with its stock tick, the face's line,
// and my shoes, toes towards the ball.
struct render_address_marks {
    glm::vec3 face_heel{0.0f};
    glm::vec3 face_toe{0.0f};
    glm::vec3 stick_back{0.0f};
    glm::vec3 stick_front{0.0f};
    glm::vec3 stock_tick{0.0f};
    glm::vec3 lead_foot{0.0f};
    glm::vec3 trail_foot{0.0f};
    glm::vec3 ball{0.0f};
};

void append_address_marks(world_marker_batch& batch, const render_address_marks& marks, const scene_colors& colors);

// What the marker pass draws this frame. Vector pointers are non-owning and
// may be null (treated as empty).
struct world_marker_scene {
    // The hole being played: cup and flag.
    bool show_hole = false;
    glm::vec3 pin_position{0.0f};
    // Every other hole's cup and flag, then the course's collectibles.
    const std::vector<glm::vec3>* pin_markers = nullptr;
    const std::vector<glm::vec3>* collectible_markers = nullptr;
    // Cups without their flag, and the flagsticks lying beside them.
    const std::vector<glm::vec3>* flags_out = nullptr;
    const std::vector<std::pair<glm::vec3, glm::vec3>>* lying_flags = nullptr;

    float cup_radius_meters = 0.0f;
    float pin_visual_height_meters = 0.0f;
    flag_pose flag;

    const render_address_marks* address_marks = nullptr;

    float ball_visual_radius_meters = 0.0f;

    // Other players and their balls.
    const std::vector<render_remote_avatar>* remote_avatars = nullptr;
    const std::vector<render_remote_ball>* remote_balls = nullptr;
    float avatar_eye_height = 0.0f;
};

void append_ground_marker(world_marker_batch& batch, const glm::vec3& position, float scale, const glm::vec3& color);
// A tee box: grey tiles over its whole top, sides down to the ground under
// it, and a turf mat on the tiles.
void append_tee_box(world_marker_batch& batch, const tee_box& box, const marker_colors& colors);
void append_pin_cup(world_marker_batch& batch, const glm::vec3& position, float cup_radius_meters, const marker_colors& colors,
                    const glow_floors& glow);
// The pole, and its flag flying as `flag` says from the top of it in two
// panels, hoist to fly.
void append_pin_flagstick(world_marker_batch& batch, const glm::vec3& position, float pin_visual_height_meters,
                          const marker_colors& colors, const glow_floors& glow, const flag_pose& flag = flag_pose{});
// A flagstick taken out, lying on the green from `foot` to `tip`, its flag
// flat on the ground at the tip end.
void append_lying_flagstick(world_marker_batch& batch, const glm::vec3& foot, const glm::vec3& tip,
                            const marker_colors& colors, const glow_floors& glow);
// Clears `batch` and fills it with every tee box of the area (`boxes` may be
// null), in a pass of their own that
// the shadow map falls on (assets/picture/shaders/tee_box.frag).
void build_tee_box_batch(world_marker_batch& batch, const std::vector<tee_box>* boxes, const marker_colors& colors);

// Clears `batch` and fills it for one frame: hole and course markers, aim
// dots, and other players and their balls. All other
// holes' cups come before all their flagsticks: cups never write depth and sit
// above their own terrain, so the order between different holes' cups and
// flags is never visible, and the course collapses into a few runs instead of
// two per hole.
void build_world_marker_batch(world_marker_batch& batch, const world_marker_scene& scene, const scene_colors& colors);
