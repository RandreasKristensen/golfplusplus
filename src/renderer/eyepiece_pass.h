#pragma once

// The rangefinder's eyepiece (assets/shaders/eyepiece.frag), between the
// scene and the camcorder, at scene resolution into an HDR target of its
// own: inside the round view the scene drawn again at the eyepiece's zoom
// into its zoom target (begin_zoom, end_zoom), so the magnified view has
// as many pixels as the rest of the picture; round it the scene as it is,
// darkened and greyed. Only while the rangefinder is held up; its outline
// and LCD are overlay (renderer/rangefinder_overlay.h).

#include "profiling/profiling.h"
#include "renderer/framebuffer.h"
#include "renderer/gl_timer.h"
#include "renderer/render_data.h"
#include "renderer/shader.h"

#include <string>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

// `proj` narrowed `magnification` times about the middle of the view.
glm::mat4 zoomed_projection(const glm::mat4& proj, float magnification);

class eyepiece_pass {
public:
    // `screen_vao` is a full-screen quad (position.xyz, uv) owned by the caller.
    bool init(const std::string& vertex_path, const std::string& fragment_path, unsigned int screen_vao);
    void shutdown();
    // The targets match the scene's size.
    bool ensure_size(int width, int height);
    // Binds the zoom target, cleared to `clear` within the round view only,
    // which is all that is drawn until end_zoom.
    void begin_zoom(const glm::vec3& clear);
    void end_zoom();
    // Draws `scene_texture` through the eyepiece, the zoom target inside it,
    // and returns the result's texture.
    unsigned int draw(unsigned int scene_texture, const render_eyepiece& eyepiece, gl_timer_pool& timers);
    void set_profile(frame_profile* profile) { shader_.set_profile(profile); }

private:
    shader_program shader_;
    framebuffer target_;
    framebuffer zoom_;
    unsigned int screen_vao_ = 0;
};
