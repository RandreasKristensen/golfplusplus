#pragma once

// The rangefinder's eyepiece (assets/picture/shaders/eyepiece.frag), between the
// scene and the camcorder, at scene resolution into an HDR target of its
// own: inside the round view the scene drawn again at the eyepiece's zoom
// into its zoom target (begin_zoom, end_zoom), so the magnified view has
// as many pixels as the rest of the picture; round it the scene as it is,
// darkened and greyed. Only while the rangefinder is held up; its outline
// and LCD are overlay (renderer/rangefinder_overlay.h).
//
// What it throws away is not shaded: the depth test rejects the circle in the
// scene (mask_scene) and the corners of the zoom's square (begin_zoom). Both
// stop short of the circle's edge by `eyepiece_mask_margin_pixels`: the sun
// shafts' depth reads reach two pixels (sun_shafts_composite.frag), so a pixel
// the eyepiece keeps never reads a masked one. The renderer masks nothing while
// a pond may show, whose pixels read what lies beside them (water.frag).

#include "renderer/gl_storage.h"
#include "renderer/draw_target.h"
#include "profiling/profiling.h"
#include "renderer/framebuffer.h"
#include "renderer/gl_timer.h"
#include "renderer/frustum.h"
#include "renderer/render_data.h"
#include "renderer/shader.h"

#include <string>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

inline constexpr float eyepiece_mask_margin_pixels = 4.0f;

// `proj` narrowed `magnification` times about the middle of the view.
glm::mat4 zoomed_projection(const glm::mat4& proj, float magnification);

class eyepiece_pass {
public:
    explicit eyepiece_pass(gl_storage storage = {})
        : shader_(storage), mask_(storage), target_(storage), zoom_(storage) {}

    // `screen_vao` is a full-screen quad (position.xyz, uv) owned by the caller.
    bool init(const std::string& vertex_path, const std::string& fragment_path, unsigned int screen_vao);
    void shutdown();
    // The targets match the scene's size.
    bool ensure_size(int width, int height);
    // Binds the zoom target, cleared to `clear` within the round view only,
    // which is all that is drawn until end_zoom. With `trim`, the square's
    // corners are rejected by depth.
    void begin_zoom(const glm::vec3& clear, bool trim);
    void end_zoom();
    // The zoom target as begin_zoom leaves it: bound, scissored.
    draw_target zoom_target() const { return draw_target{zoom_.id(), zoom_.width(), zoom_.height(), true}; }
    // Rejects the round view in the bound scene target, which is `width` x
    // `height` and just cleared; scissor and depth state are left as drawing
    // expects. Returns the disc (for `view_proj`) that culling may hide.
    hidden_disc mask_scene(const glm::mat4& view_proj, int width, int height);
    // The eyepiece's last picture, as the camcorder reads it.
    const framebuffer& picture() const { return target_; }
    // Draws `scene_texture` through the eyepiece, the zoom target inside it,
    // and returns the result's texture.
    unsigned int draw(unsigned int scene_texture, const render_eyepiece& eyepiece, gl_timer_pool& timers);

private:
    // Writes the nearest depth inside (`cover` +1) or outside (-1) the circle of `radius` pixels, within `box` pixels of its middle.
    void write_depth(const glm::vec2& center, float radius, float cover, float box);

    shader_program shader_;
    shader_program mask_;
    framebuffer target_;
    framebuffer zoom_;
    unsigned int screen_vao_ = 0;
};
