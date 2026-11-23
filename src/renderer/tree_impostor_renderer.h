#pragma once

// Far trees drawn as images of their own grown models (renderer/tree_model.h),
// never as a different tree. Each species' variant is rendered once per course
// season, unlit, from a grid of views (lighting.json "trees"."impostor"):
// colour with coverage, the unit-frame normal with depth, and what is leaf,
// twig or bark and how deep in the crown. Each view is drawn at
// bake_supersample times a tile's size and averaged into it, so a winter
// crown's twigs, far finer than a texel, keep their real share of it. A far
// tree is then one strip, posed by the shared wind and clock like its
// geometry, that blends the grid views round the eye's direction and is lit by the same code as the geometry
// (assets/picture/shaders/tree_light.glsl, tree_image.frag). Which trees are
// far is render_tree.h's tree_image_error_pixels; shadows are cast by the full
// models. The cache is rewritten in place when the looks or a season change.

#include "renderer/gl_storage.h"

#include <cstddef>
#include <string>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include "game/lighting_loader.h"
#include "profiling/profiling.h"
#include "renderer/shader.h"
#include "renderer/tree_model.h"

// A run of one model's instances in the instance buffer.
struct tree_image_run {
    std::size_t model = 0;
    std::size_t first = 0;
    std::size_t count = 0;
};

// What baking one model needs: its indexed mesh and the season it is in.
struct tree_image_source {
    unsigned vao = 0;
    int index_count = 0;
    tree_box box;
    glm::vec3 bark{0.0f};
    glm::vec3 twig{0.0f};
    glm::vec3 leaves{0.0f};
    glm::vec3 withered{0.0f};
    glm::vec2 season{1.0f, 0.0f};  // the crown's share in leaf and withered (tree.frag's u_season)
    glm::vec4 twigs{0.0f};         // tree.frag's u_twigs
    float shine_through = 0.0f;
};

// Each view is drawn this many times finer each way than its tile.
inline constexpr int bake_supersample = 8;

class tree_impostor_renderer {
public:
    explicit tree_impostor_renderer(gl_storage storage = {})
        : storage_(storage), bake_shader_(storage), resolve_shader_(storage), shader_(storage) {}

    // Makes the programs and room for `models` models' views. The resolve
    // shaders (tree_bake_resolve) are found beside `bake_vertex`.
    bool init(const std::string& bake_vertex, const std::string& bake_fragment, const std::string& image_vertex,
              const std::string& image_fragment, const tree_impostor_style& style, std::size_t models);
    void shutdown();

    // Renders every view of `source` into model `model`'s layers. Between
    // begin_bake and end_bake the caller's framebuffer and viewport are kept.
    void begin_bake(frame_profile* profile = nullptr);
    void bake(std::size_t model, const tree_image_source& source);
    void end_bake();
    bool baked(std::size_t model) const { return model < models_.size() && models_[model].baked; }
    void forget_all();

    const shader_program& shader() const { return shader_; }
    const tree_impostor_style& style() const { return style_; }
    int views_per_model() const { return style_.azimuths * static_cast<int>(style_.elevations_degrees.size()); }
    std::size_t texture_bytes() const { return texture_bytes_; }

    // Draws `runs` of the instance buffer (tree.vert's layout) seen from `eye`.
    // The caller binds the shadow map into shader() first.
    void draw(const std::vector<tree_image_run>& runs, unsigned instance_buffer, const glm::mat4& view_proj,
              const glm::vec3& eye, float seconds, const glm::vec2& wind, frame_profile* profile);

private:
    struct model_images {
        bool baked = false;
        tree_box box;
        float shine_through = 0.0f;
    };

    void set_grid(const shader_program& program) const;
    static void set_box(const shader_program& program, const tree_box& box);

    gl_storage storage_;
    shader_program bake_shader_;
    shader_program resolve_shader_;
    shader_program shader_;
    tree_impostor_style style_;
    std::vector<model_images> models_;
    unsigned int albedo_ = 0;
    unsigned int normal_ = 0;
    unsigned int leaf_ = 0;
    unsigned int framebuffer_ = 0;       // a tile's layers
    unsigned int fine_framebuffer_ = 0;  // a view drawn bake_supersample times finer
    unsigned int fine_[3] = {0, 0, 0};
    unsigned int fine_depth_ = 0;
    unsigned int resolve_vao_ = 0;
    unsigned int strip_vao_ = 0;
    unsigned int strip_vbo_ = 0;
    std::size_t texture_bytes_ = 0;
    int saved_framebuffer_ = 0;
    int saved_viewport_[4] = {0, 0, 0, 0};
    bool saved_depth_test_ = false;
    bool saved_blend_ = false;
    bool saved_scissor_ = false;
};
