#pragma once

// The picture's look from assets/visuals/picture.json: art direction for the
// renderer, loaded by app before the renderer starts (it draws the loading
// card with it), so not part of game_content. Lengths are fractions of the
// picture's height, never pixels, so the look is the same at every window
// size.

#include <optional>
#include <string>

#include <glm/vec3.hpp>

inline constexpr const char* picture_style_path = "visuals/picture.json";

// What tape wear changes, from a fresh tape (next to no tape at all) to a
// worn one.
struct tape_look {
    float luma_lines = 0.0f;          // brightness detail, lines per picture height
    float chroma_lines = 0.0f;        // colour detail, the same
    float peaking = 0.0f;             // the edge halo VCRs sharpen back
    float chroma_delay = 0.0f;        // colour lags right of brightness
    float black_lift = 0.0f;
    float highlight_knee = 0.0f;      // brightness where highlights start rolling off
    float saturation = 0.0f;
    float grain = 0.0f;               // brightness noise, of full white
    float chroma_noise = 0.0f;        // slow colour noise, in I/Q units
    float wobble = 0.0f;              // each line's sideways jitter
    float head_switch_skew = 0.0f;    // how far the bottom lines are pulled sideways
    float head_switch_noise = 0.0f;
    float dropouts_per_second = 0.0f;
};

// VHS playback (renderer/tape_pass.h).
struct tape_style {
    float fields_per_second = 0.0f;  // how often the noise changes
    float grain_width = 0.0f;        // how far a grain stretches sideways
    float chroma_noise_size = 0.0f;
    float wobble_span = 0.0f;        // how far down the frame lines wobble alike
    float head_switch_height = 0.0f;
    float dropout_length = 0.0f;
    float tracking_height = 0.0f;    // the rolling band of tracking noise
    float tracking_speed = 0.0f;     // picture heights per second
    float tracking_noise = 0.0f;
    float tracking_skew = 0.0f;
    float tracking_burst_seconds = 0.0f;  // after a hole starts or ends
    float tracking_fade_seconds = 0.0f;
    tape_look fresh;
    tape_look worn;
};

// The small TV it plays on (crt.frag, renderer/tv_pass.h).
struct tv_style {
    float horizontal_blur = 0.0f;   // in scene pixels: the beam is soft sideways
    float scanline_depth = 0.0f;    // the dark gap under each line, at full strength
    float scanline_gap = 0.0f;      // of each line's height
    int scanline_full_factor = 0;   // window rows per line where the depth is full; none at 1
    float mask = 0.0f;              // phosphor stripes
    float curvature = 0.0f;
    float corner_radius = 0.0f;
    float halation = 0.0f;
    float halation_threshold = 0.0f;
    float halation_radius = 0.0f;
    float vignette = 0.0f;
    glm::vec3 border_color{0.0f};   // the cabinet around the curved screen
};

// The cheap CCD camcorder that recorded the tape (renderer/camcorder_pass.h).
// How bright, how white and how noisy it records at each time of day are
// the lighting palette's keys (game/lighting_loader.h); this is how it
// responds.
struct camcorder_style {
    // The iris opens or closes the frame's mean brightness towards
    // middle_grey, by at most these stops either way, easing over
    // iris_seconds.
    float middle_grey = 0.0f;
    float iris_min_stops = 0.0f;
    float iris_max_stops = 0.0f;
    float iris_seconds = 0.0f;
    // How much of the light's colour cast the white balance takes out
    // (0 none, 1 all), easing over white_balance_seconds.
    float white_balance_correction = 0.0f;
    float white_balance_seconds = 0.0f;
    // Brightness where highlights start rolling off; they clip at 2 - shoulder.
    float shoulder = 0.0f;
    float toe = 0.0f;               // how hard shadows crush
    glm::vec3 shadow_tint{0.0f};    // what crushed shadows sink to
    float bloom_threshold = 0.0f;   // exposed brightness that spills
    float bloom_strength = 0.0f;
    float bloom_width = 0.0f;       // how far it spills sideways
    float bloom_height = 0.0f;      // and up and down
    float smear_threshold = 0.0f;   // exposed brightness that smears its column
    float smear_strength = 0.0f;    // of the light past it, summed down the column in picture heights
    // How much of that sum the smear loses per picture height away from its
    // light: bright columns reach further, so it thins as it fades.
    float smear_fade = 0.0f;
    // Gain-up noise at full gain (the palette's gain_noise): per pixel,
    // brightness and, stronger, colour.
    float gain_grain = 0.0f;
    float gain_chroma = 0.0f;
};

struct picture_style {
    // About how many lines the scene has: each line is a whole number of
    // window rows, and the width follows the window.
    int target_lines = 0;
    camcorder_style camcorder;
    tape_style tape;
    tv_style tv;
};

// nullopt unless every field is present and in range.
std::optional<picture_style> parse_picture_from_text(const std::string& text);
std::optional<picture_style> load_picture_style(const std::string& asset_root);

// The look at `wear` (0 fresh .. 1 worn, clamped). Detail blends by blur
// width (1 / lines), so the blur grows evenly along the setting.
tape_look tape_look_at(const tape_style& tape, float wear);
