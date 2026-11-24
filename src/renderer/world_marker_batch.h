#pragma once

#include "game/lighting_loader.h"
#include "game/tee_box.h"
#include "renderer/mesh_instance.h"

#include <array>
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
//
// What moves with the crowd, the figures and their balls, is not transformed
// at all: each of its pieces is an instance (mesh_instance.h) of a canonical
// mesh, kept in a list per mesh, and the GL side draws every part's pieces of
// one mesh as one instanced draw (world_marker_renderer.h). Their draw order is
// by mesh, not by submission. Everything else (cups, flags, poles, collectibles,
// feet, tee boxes) is world-space triangles in runs, kept as vertices.

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
struct render_lying_body;

// The parts build_world_marker_batch lays out one after the other, each in
// runs of its own, so a view draws the parts it shows from the one upload:
// the picture's main view and its zoomed one (my shoes at address), the
// mirrored views (my figure) and the sun's shadow map (flagsticks and figures).
enum class marker_part : std::size_t {
    cups,
    poles,        // the standing flagsticks' poles, as wide as a pixel of the main view needs
    zoom_poles,   // the same poles as wide as a pixel of the zoomed view needs (empty without one)
    flagsticks,   // the hole's, the course's and the lying ones' flags and the lying poles
    collectibles,
    avatars,
    balls,
    address_feet,
    my_figure,
    count
};
enum class marker_view { scene, zoom, mirror, sun_shadow };

// One part's instances of one mesh (all depth-writing, no stencil, layer 0),
// from the start of that mesh's list.
struct world_marker_instance_run {
    marker_part part = marker_part::cups;
    prop_mesh mesh = prop_mesh::cylinder;
    std::size_t first = 0;
    std::size_t count = 0;
};

class world_marker_batch {
public:
    world_marker_batch();

    // Empties vertices and runs but keeps their capacity for the next frame.
    void clear();
    // Ends `part`: what is appended next starts a run of its own. Parts end in order.
    void end_part(marker_part part);
    // Where each part ends, in vertices, for re-sending only the parts that changed;
    // a batch not built in parts is one part.
    std::size_t part_end_count() const { return parts_ended_ ? part_ends_.size() : 1; }
    std::size_t part_end(std::size_t index) const { return parts_ended_ ? part_ends_[index] : vertices_.size(); }
    // Whether `view` draws the part `run` belongs to (a batch with no parts ended draws every run).
    bool shown_in(const world_marker_run& run, marker_view view) const;
    bool shown_in(marker_part part, marker_view view) const;

    // Unit disc / quad / cylinder / sphere transformed by `model`, with its
    // glow floor (world_marker_vertex).
    void append_disc(const glm::mat4& model, const glm::vec3& color, float glow, bool depth_write);
    void append_quad(const glm::mat4& model, const glm::vec3& color, float glow, bool depth_write);
    void append_cylinder(const glm::mat4& model, const glm::vec3& color, float glow, bool depth_write);
    void append_sphere(const glm::mat4& model, const glm::vec3& color, float glow, bool depth_write);
    // A figure's or ball's piece: `mesh` placed by `model`, drawn instanced, depth-writing.
    void append_instance(prop_mesh mesh, const glm::mat4& model, const glm::vec3& color, float glow);
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
    // The instances of `mesh`; the lists joined (cylinders, spheres, quads) are what is uploaded, and
    // `instance_base` is where one starts in them.
    const std::vector<mesh_instance>& instances(prop_mesh mesh) const { return instances_[static_cast<std::size_t>(mesh)]; }
    const std::vector<world_marker_instance_run>& instance_runs() const { return instance_runs_; }
    std::size_t instance_base(prop_mesh mesh) const;
    std::size_t instance_count() const;
    void join_instances(std::vector<mesh_instance>& out) const;
    // Every vertex the instances place, list by list, as the draw puts them on screen: for tests.
    std::vector<world_marker_vertex> instanced_vertices() const;
    bool empty() const { return vertices_.empty() && instance_count() == 0; }

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
    canonical_meshes meshes_;
    std::array<std::vector<mesh_instance>, prop_mesh_count> instances_;
    std::vector<world_marker_instance_run> instance_runs_;
    std::array<std::size_t, prop_mesh_count> open_run_{};  // the run each mesh is adding to, plus one; 0 for none
    marker_part part_ = marker_part::cups;                  // the part being built
    std::vector<world_marker_vertex> vertices_;
    std::vector<world_marker_run> runs_;
    std::array<std::size_t, static_cast<std::size_t>(marker_part::count)> part_ends_{};
    bool parts_ended_ = false;
    bool break_run_ = false;
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
    // Each marker's flagstick height in metres; a marker past its end stands
    // pin_visual_height_meters tall.
    const std::vector<float>* pin_marker_heights = nullptr;
    const std::vector<render_collectible>* collectibles = nullptr;
    // Cups without their flag, and the flagsticks lying beside them.
    const std::vector<glm::vec3>* flags_out = nullptr;
    const std::vector<std::pair<glm::vec3, glm::vec3>>* lying_flags = nullptr;

    float cup_radius_meters = 0.0f;
    float cup_depth_meters = 0.0f;
    float pin_visual_height_meters = 0.0f;
    flag_pose flag;
    // A flagstick's real radius; where the camera is, and how wide one pixel is a
    // metre from it in the main view and in the zoomed one (0: no such view; the
    // main view then draws every pole at its real radius, the zoomed one none).
    float flagstick_radius_meters = 0.0f;
    glm::vec3 camera_position{0.0f};
    float pixel_per_meter = 0.0f;
    float zoom_pixel_per_meter = 0.0f;

    const render_address_feet* address_feet = nullptr;
    // The cups are in a batch of their own (build_cup_batch), kept for the course.
    bool cups_apart = false;
    // My own figure, which only a mirror or the sun's shadow shows (marker_view).
    const render_remote_avatar* my_figure = nullptr;

    float ball_visual_radius_meters = 0.0f;

    // Other players and their balls.
    const std::vector<render_remote_avatar>* remote_avatars = nullptr;
    const std::vector<render_remote_ball>* remote_balls = nullptr;
    // Fallen golfers' bodies (lying_body.h), drawn with the avatars.
    const std::vector<render_lying_body>* lying_bodies = nullptr;
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
// A cylindrical pole of `radius_meters` and its flag, which flies in the world's wind.
void append_pin_flagstick(world_marker_batch& batch, const glm::vec3& position, float pin_visual_height_meters,
                          const marker_colors& colors, const glow_floors& glow, const flag_pose& flag = flag_pose{},
                          float radius_meters = 0.0f);
// The pole alone, and the flag alone, of a flagstick.
void append_pin_pole(world_marker_batch& batch, const glm::vec3& position, float pin_visual_height_meters,
                     const marker_colors& colors, const glow_floors& glow, float radius_meters);
void append_pin_flag(world_marker_batch& batch, const glm::vec3& position, float pin_visual_height_meters,
                     const marker_colors& colors, const glow_floors& glow, const flag_pose& flag);
// The radius a standing flagstick is drawn at in a view whose pixels are
// `pixel_per_meter` wide a metre from the camera: its real one, or where that
// is thinner than a pixel of that view, one just wide enough that its
// silhouette is a whole pixel (the cylinder is an n-gon, narrowest across its
// flats), so it never drops out far off. Each view has its own pole (marker_part
// poles, zoom_poles), so a zoomed view never magnifies the main view's width.
float flagstick_draw_radius(const world_marker_scene& scene, const glm::vec3& position, float height_meters,
                            float pixel_per_meter);
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
// Clears `batch` and fills it with the cups' holes of `scene` alone, for a scene whose cups_apart is set.
void build_cup_batch(world_marker_batch& batch, const world_marker_scene& scene, const scene_colors& colors);
