#pragma once

// VHS playback of what the camcorder recorded (renderer/camcorder_pass.h;
// assets/shaders/tape.frag), at scene resolution into a target of its own
// that the overlay is then drawn onto, so text is never smeared or noisy: brightness and colour blurred sideways apart,
// colour bleeding right, the tone curve, grain, wobble, the head-switching
// band, dropouts and, when the frame asks for it, tracking noise. Never off.
// Strengths are picture.json's `tape` section, blended by the tape wear
// setting from next to nothing (fresh) to worn.
//
// Written from these, no code taken:
//   https://ntsc.rs/ - an analog TV and VHS simulator; which artifacts matter, and how strong
//   https://github.com/rgm89git/ntscQTplus - head switching, colour bleed
//   https://github.com/LMP88959/NTSC-CRT - a small NTSC encoder and decoder for games
//   https://github.com/libretro/slang-shaders - scanline, mask and curvature techniques
//   https://xdevs.com/doc/Sencore/Sencore_Tech_Tips/TT189p%20-%20Comparison%20Of%20VCR%20Formats.pdf - 240 lines
//   https://xdevs.com/doc/Sencore/Sencore_Tech_Tips/TT133%20-%20Testing%20VCR%20Color%20Circuits%20Using%20The%20VA62-VC63%20And%20SC61.pdf - 629 kHz colour-under
//   https://www.digitalfaq.com/forum/video-capture/13372-color-horizontal-resolution-print.html - about 45 colour lines

#include "game/picture_loader.h"
#include "profiling/profiling.h"
#include "renderer/framebuffer.h"
#include "renderer/shader.h"

#include <string>

class tape_pass {
public:
    // `screen_vao` is a full-screen quad (position.xyz, uv) owned by the caller.
    bool init(const std::string& vertex_path, const std::string& fragment_path, unsigned int screen_vao,
              const tape_style& style);
    void shutdown();
    // 0 fresh .. 1 worn.
    void set_wear(float wear) { wear_ = wear; }
    // The target matches the scene's size.
    bool ensure_size(int width, int height);
    // Plays `picture_texture` into the target and leaves the target bound.
    // `seconds` runs the noise; `tracking` (0..1) is how much tracking noise.
    void draw(unsigned int picture_texture, float seconds, float tracking);
    unsigned int color_texture() const { return target_.color_texture(); }
    void set_profile(frame_profile* profile) { shader_.set_profile(profile); }

private:
    shader_program shader_;
    framebuffer target_;
    tape_style style_;
    float wear_ = 0.0f;
    unsigned int screen_vao_ = 0;
};
