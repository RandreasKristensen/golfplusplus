#pragma once

// The camera that recorded the tape: a cheap 1989 CCD camcorder, at scene
// resolution between the HDR scene and the tape pass (assets/picture/shaders/
// camcorder*.frag). The iris meters the scene's mean brightness (a quarter-
// size copy's smallest mip, eased in a 1x1 ping-pong texture: no readback)
// and the white balance the light's colour, both lagging; bright light
// blooms (from the quarter-size copy, wider sideways) and smears its whole
// column, fading and thinning away from it (a Wx1 pass summing each
// column's light past the threshold and finding where it is); then the tone
// curve, the colour it keeps and gain-up noise. Never off. Strengths are picture.json's
// `camcorder` section; exposure, white, gain and saturation by time of day
// are the lighting palette's (render_lighting).

#include "game/picture_loader.h"
#include "profiling/profiling.h"
#include "renderer/framebuffer.h"
#include "renderer/gl_timer.h"
#include "renderer/render_data.h"
#include "renderer/shader.h"

#include <array>
#include <string>

class camcorder_pass {
public:
    // `shader_dir` holds the camcorder shaders and crt.vert; `screen_vao` is
    // a full-screen quad (position.xyz, uv) owned by the caller.
    // `fields_per_second` is how often the gain noise changes.
    bool init(const std::string& shader_dir, unsigned int screen_vao, const camcorder_style& style, float fields_per_second);
    void shutdown();
    // The targets match the scene's size; a new size starts metering afresh.
    bool ensure_size(int width, int height);
    // Records `scene_texture` (HDR) in `light` into the picture; `seconds`
    // runs the lag and the noise.
    void draw(unsigned int scene_texture, const render_lighting& light, float seconds, gl_timer_pool& timers);
    // Whether the picture holds a recorded frame at the current size.
    bool has_frame() const { return has_frame_; }
    unsigned int color_texture() const { return picture_.color_texture(); }
    void set_profile(frame_profile* profile);

private:
    void draw_quad(const shader_program& shader);
    void set_iris(const shader_program& shader, float exposure) const;

    shader_program downsample_;
    shader_program adapt_;
    shader_program bloom_;
    shader_program smear_;
    shader_program record_;
    framebuffer quarter_;
    framebuffer bloom_target_;
    framebuffer smear_target_;
    std::array<framebuffer, 2> adapt_targets_;
    framebuffer picture_;
    camcorder_style style_;
    float fields_per_second_ = 0.0f;
    int adapt_index_ = 0;  // which adapt target holds the latest
    float last_seconds_ = 0.0f;
    bool has_frame_ = false;
    unsigned int screen_vao_ = 0;
};
