#pragma once

#include "game/lighting_loader.h"
#include "game/tee_box.h"

#include <cstddef>
#include <cstdint>
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
// matters only for depth-write changes, exact depth ties and the cups'
// holes, which come first so everything after is drawn into them.

// Uploaded straight into a GL buffer: position.xyz, color.rgb, then glow:
// the least light the piece shows (lighting.glsl's flat_light), 0 for none.
struct world_marker_vertex {
    glm::vec3 position{0.0f};
    glm::vec3 color{1.0f};
    float glow = 0.0f;
};

// How a run uses the stencil to cut the cups' holes into the ground drawn
// before it (append_cup_holes).
enum class marker_stencil : std::uint8_t {
    none,
    // Closed cylinders, drawing nothing: each pixel counts the faces in
    // front of the ground there, towards the camera +1 and away -1, so
    // where the ground lies inside a cup's cylinder (its opening) is left
    // non-zero.
    mark_holes,
    // Drawn only through the openings, over the ground and deeper than it:
    // the faces turned towards the camera, the wall's inside and the bottom.
    hole_insides
};

// Consecutive vertices drawn with the same depth-write, stencil use and
// layer: a layer above 0 is drawn that many depth steps nearer than it lies
// (a polygon offset), so it wins over what it lies on in the same plane
// however far off, where a lift of a centimetre is lost in the depth buffer.
struct world_marker_run {
    bool depth_write = true;
    marker_stencil stencil = marker_stencil::none;
    int layer = 0;
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
    // Triangles already in world space, three corners each, counter-clockwise
    // seen from their front, in a run of their own stencil use.
    void append_world_triangles(const std::vector<glm::vec3>& corners, const glm::vec3& color, float glow,
                                marker_stencil stencil);

    // The layer what is appended next lies in (world_marker_run), 0 after clear.
    void set_layer(int layer) { layer_ = layer; }

    const std::vector<world_marker_vertex>& vertices() const { return vertices_; }
    const std::vector<world_marker_run>& runs() const { return runs_; }
    bool empty() const { return vertices_.empty(); }

private:
    // The run the next vertices join, a new one when the state changes.
    world_marker_run& run_for(bool depth_write, marker_stencil stencil);
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
    int layer_ = 0;
};

// A rectangle lying flat at `center`: local x across the hole, local y along
// `down_hole` (horizontal), scaled by `half_size`.
glm::mat4 ground_rect_model(const glm::vec3& center, const glm::vec3& down_hole, const glm::vec2& half_size);
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

// My shoes at address, toes towards the ball.
struct render_address_feet {
    glm::vec3 lead_foot{0.0f};
    glm::vec3 trail_foot{0.0f};
    glm::vec3 ball{0.0f};
};

void append_address_feet(world_marker_batch& batch, const render_address_feet& marks, const scene_colors& colors);

// A collectible lying out on the course: a lost ball. `index` is its place in
// the course's list, so each keeps its paint (marker_colors) frame to frame.
struct render_collectible {
    glm::vec3 position{0.0f};  // on the ground under it
    std::uint32_t index = 0;
};

// What the marker pass draws this frame. Vector pointers are non-owning and
// may be null (treated as empty).
struct world_marker_scene {
    // The hole being played: cup and flag.
    bool show_hole = false;
    glm::vec3 pin_position{0.0f};
    // Every other hole's cup and flag, then the course's collectibles.
    const std::vector<glm::vec3>* pin_markers = nullptr;
    const std::vector<render_collectible>* collectibles = nullptr;
    // Cups without their flag, and the flagsticks lying beside them.
    const std::vector<glm::vec3>* flags_out = nullptr;
    const std::vector<std::pair<glm::vec3, glm::vec3>>* lying_flags = nullptr;

    float cup_radius_meters = 0.0f;
    float cup_depth_meters = 0.0f;
    float pin_visual_height_meters = 0.0f;
    flag_pose flag;
    // A flagstick's real radius; where the camera is, and how wide one scene
    // pixel is a metre from it: flagsticks are drawn at least a pixel wide.
    float flagstick_radius_meters = 0.0f;
    glm::vec3 camera_position{0.0f};
    float pixel_per_meter = 0.0f;

    const render_address_feet* address_feet = nullptr;

    float ball_visual_radius_meters = 0.0f;

    // Other players and their balls.
    const std::vector<render_remote_avatar>* remote_avatars = nullptr;
    const std::vector<render_remote_ball>* remote_balls = nullptr;
    float avatar_eye_height = 0.0f;
};

// A collectible: a ball `radius` round resting on the ground, mostly in the
// ball's white, now and then in one of the collectible paints.
void append_collectible(world_marker_batch& batch, const render_collectible& collectible, float radius,
                        const marker_colors& colors, float glow);
// The layers of a tee box's top, bottom up (world_marker_run::layer). Its
// grout lies on the ground, so it is drawn nearer than the ground and the
// zone shapes on it (terrain_renderer.cpp's overlay offset).
enum class tee_box_layer : int { grout = 3, tiles, mat };

// A tee box: grey tiles over its whole top, flush with the ground, and a
// turf mat on the tiles, each in its tee_box_layer.
void append_tee_box(world_marker_batch& batch, const tee_box& box, const marker_colors& colors);
// A hole `radius` across and `depth` deep cut into the ground at each of
// `centres` (on the green, the middle of the hole): first every cup's
// cylinder marking its opening, then every one's inside in the cup's colour.
void append_cup_holes(world_marker_batch& batch, const std::vector<glm::vec3>& centres, float radius, float depth,
                      const marker_colors& colors, const glow_floors& glow);
// How a standing flagstick's pole is seen: its real radius, the way from it
// towards the eye, and how wide a scene pixel is where it stands.
struct pole_view {
    float radius_meters = 0.0f;
    glm::vec3 toward_eye{0.0f, 0.0f, 1.0f};
    float pixel_meters = 0.0f;
};
// The pole, one panel turned to the eye at its real width, or a scene pixel
// wide where that is thinner, so far off it still shows; then its flag flying
// as `flag` says from the top of it in two panels, hoist to fly.
void append_pin_flagstick(world_marker_batch& batch, const glm::vec3& position, float pin_visual_height_meters,
                          const marker_colors& colors, const glow_floors& glow, const flag_pose& flag = flag_pose{},
                          const pole_view& pole = pole_view{});
// A flagstick taken out, lying on the green from `foot` to `tip`, its flag
// flat on the ground at the tip end.
void append_lying_flagstick(world_marker_batch& batch, const glm::vec3& foot, const glm::vec3& tip,
                            const marker_colors& colors, const glow_floors& glow);
// Clears `batch` and fills it with every tee box of the area (`boxes` may be
// null), in a pass of their own that the shadow map falls on
// (assets/picture/shaders/tee_box.frag): every box's grout, then
// every box's tiles, then every mat, one run each.
void build_tee_box_batch(world_marker_batch& batch, const std::vector<tee_box>* boxes, const marker_colors& colors);

// Clears `batch` and fills it for one frame: hole and course markers, aim
// dots, and other players and their balls. All other
// holes' cups come before all their flagsticks: cups never write depth and sit
// above their own terrain, so the order between different holes' cups and
// flags is never visible, and the course collapses into a few runs instead of
// two per hole.
void build_world_marker_batch(world_marker_batch& batch, const world_marker_scene& scene, const scene_colors& colors);
