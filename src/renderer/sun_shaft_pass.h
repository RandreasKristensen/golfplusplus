#pragma once

// The sun's shafts: the shadows of trees, floodlights, players and carts in
// the haze. Every scene shader hazes what it draws with the sun's glow in the
// haze (lighting.glsl's haze_toward), as if the air in front were all in sun;
// after the opaque scene, this pass steps each ray through the shadow map at
// half the scene's size (sun_shafts.frag) and takes out of the scene's colour
// the glow of the air that lies in shadow (sun_shafts_composite.frag, the
// scene's depth deciding which half-size pixels a pixel takes from). The
// gaps between trees keep their glow and read as shafts. Real, never
// boosted: faint in clear air, strong in mist and fog. Skipped, it leaves
// the scene exactly as drawn: with no shadow map (the sun down or behind
// cloud) and when the most it could take is under shaft_style::min_glow
// (scattered_light.h's shaft_peak), decided here, never in the shader.
//
// The same steps take the shadowed part of the nearest floodlights' beams
// out (lamp_shadow_pass.h's maps, scattered_light.h's lamp_shaft_shade):
// the trees and floodlights cut the beams they stand in. Each pair of sun
// or not and how many lamps is its own program, picked here.

#include "game/lighting_loader.h"
#include "renderer/framebuffer.h"
#include "renderer/gl_timer.h"
#include "renderer/lamp_shadow_pass.h"
#include "renderer/lighting_uniforms.h"
#include "renderer/shader.h"
#include "renderer/shadow_map_pass.h"

#include <cstddef>
#include <string>
#include <vector>

#include <glm/mat4x4.hpp>

class sun_shaft_pass {
public:
    // `shader_dir` holds the shaft shaders and crt.vert; `screen_vao` is a
    // full-screen quad owned by the caller; the shadow map is bound to
    // `shadow_map_unit` and the lamps' maps to `lamp_map_unit` while the
    // shafts step through them.
    bool init(const std::string& shader_dir, unsigned int screen_vao, const shaft_style& style, int shadow_map_unit,
              int lamp_map_unit);
    void shutdown();
    // Its half-size target for a scene `width` x `height`, whose colour
    // texture `scene_color` it takes the shafts out of.
    bool ensure_size(int width, int height, unsigned int scene_color);
    // Takes the shafts and the lamps' beams' shadows out of the scene drawn
    // with `lighting` as `view_proj` sees it, whose depth is `scene_depth`.
    // Leaves its own framebuffer bound: the caller binds the scene's again.
    void draw(const frame_lighting_block& lighting, const shadow_map_pass& shadows, const lamp_shadow_pass& lamps,
              unsigned int scene_depth, const glm::mat4& view_proj, gl_timer_pool& timers);
    void set_profile(frame_profile* profile);

private:
    void destroy_targets();
    // The march for the sun or not and `lamps` lamps.
    shader_program& march(bool sun, std::size_t lamps);

    std::vector<shader_program> marches_;
    shader_program composite_;
    framebuffer shafts_;
    unsigned int scene_fbo_ = 0;  // the scene's colour alone, to draw into while its depth is read
    unsigned int scene_color_ = 0;
    int width_ = 0;
    int height_ = 0;
    shaft_style style_;
    int shadow_map_unit_ = 0;
    int lamp_map_unit_ = 0;
    unsigned int screen_vao_ = 0;
};
