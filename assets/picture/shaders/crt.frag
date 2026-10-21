#version 330 core

// The small TV the tape plays on (renderer/tv_pass.h), at window resolution:
// every scene line is u_factor window rows (nearest vertically), soft
// sideways, with a dark gap under each line, phosphor stripes, a slightly
// curved screen with rounded corners, glow around bright things and a
// vignette.

out vec4 frag_color;

uniform sampler2D u_scene;
uniform vec2 u_resolution;
// Each scene pixel covers u_factor x u_factor window pixels; u_crop window
// pixels of the scene's overhang are cut off the left and bottom.
uniform int u_factor;
uniform vec2 u_crop;
uniform vec4 u_beam;       // horizontal blur (scene pixels), scanline depth, scanline gap, mask
uniform vec4 u_screen;     // curvature, corner radius (window pixels), vignette
uniform vec4 u_halation;   // strength, threshold, radius (scene pixels)
uniform vec3 u_border;

const float edge_softness = 1.5;  // window pixels

vec3 scene_at(int x, int y, ivec2 size) {
    return texelFetch(u_scene, ivec2(clamp(x, 0, size.x - 1), clamp(y, 0, size.y - 1)), 0).rgb;
}

// How much of a window row `width` tall, centred at `t` within a scene line,
// lies in the gap at the bottom of a line, [0, gap) and its repeats.
float gap_coverage(float t, float gap, float width) {
    float low = t - 0.5 * width;
    float high = t + 0.5 * width;
    float covered = 0.0;
    for (int k = -1; k <= 1; ++k) {
        covered += max(0.0, min(high, float(k) + gap) - max(low, float(k)));
    }
    return covered / width;
}

void main() {
    vec2 centered = gl_FragCoord.xy / u_resolution * 2.0 - 1.0;
    float curvature = u_screen.x;
    vec2 bent = centered * (1.0 + curvature * dot(centered, centered)) / (1.0 + curvature);
    vec2 window_pos = (bent * 0.5 + 0.5) * u_resolution;

    float radius = u_screen.y;
    vec2 corner = abs(window_pos - 0.5 * u_resolution) - (0.5 * u_resolution - radius);
    float outside = length(max(corner, 0.0)) + min(max(corner.x, corner.y), 0.0) - radius;
    float on_screen = clamp(0.5 - outside / edge_softness, 0.0, 1.0);
    if (on_screen <= 0.0) {
        frag_color = vec4(u_border, 1.0);
        return;
    }

    ivec2 size = textureSize(u_scene, 0);
    float factor = float(u_factor);
    vec2 scene_pos = (window_pos + u_crop) / factor;
    int line = int(floor(scene_pos.y));

    float sigma = max(u_beam.x, 0.05);
    float sx = scene_pos.x - 0.5;
    int x0 = int(floor(sx));
    float fx = sx - float(x0);
    vec3 color = vec3(0.0);
    float weight = 0.0;
    for (int i = -1; i <= 2; ++i) {
        float d = float(i) - fx;
        float w = exp(-d * d / (2.0 * sigma * sigma));
        color += scene_at(x0 + i, line, size) * w;
        weight += w;
    }
    color /= weight;

    vec3 glow = vec3(0.0);
    float halation_radius = u_halation.z;
    for (int i = 0; i < 8; ++i) {
        float angle = float(i) * 0.785398;
        vec2 offset = vec2(cos(angle), sin(angle)) * halation_radius * (1.0 + float(i & 1));
        vec2 tap = scene_pos + offset;
        glow += max(scene_at(int(floor(tap.x)), int(floor(tap.y)), size) - u_halation.y, 0.0);
    }
    color += u_halation.x * glow / (8.0 * (1.0 - u_halation.y));

    float depth = u_beam.y;
    float gap = u_beam.z;
    float scan = 1.0 - depth * gap_coverage(fract(scene_pos.y), gap, 1.0 / factor);
    color *= scan / (1.0 - depth * gap);

    float mask = u_beam.w;
    vec3 stripes = vec3(1.0 - mask);
    int phosphor = int(gl_FragCoord.x) % 3;
    stripes[phosphor] = 1.0;
    color *= stripes / (1.0 - mask * 2.0 / 3.0);

    float from_center = dot(centered, centered) * 0.5;
    color *= 1.0 - u_screen.z * from_center;

    frag_color = vec4(mix(u_border, clamp(color, 0.0, 1.0), on_screen), 1.0);
}
