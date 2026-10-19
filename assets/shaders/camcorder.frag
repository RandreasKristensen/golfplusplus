#version 330 core

#include "camcorder.glsl"

// A cheap 1989 CCD camcorder recording the scene (renderer/camcorder_pass.h):
// iris, bloom and column smear in light, then the white balance, the
// narrow tone curve (highlights clip, shadows crush to a muddy blue-black),
// the colour it keeps and gain-up noise, into the 8-bit picture the tape
// records. The tape adds its own blur, bleed, colour loss and grain after.

in vec2 v_uv;

out vec4 frag_color;

uniform sampler2D u_scene;
uniform sampler2D u_bloom;
uniform sampler2D u_smear;
uniform vec4 u_white_balance;  // the time of day's white (rgb), how much of the light's cast it takes out
uniform vec4 u_tone;           // shoulder, toe, saturation, bloom strength
uniform vec3 u_shadow_tint;
uniform vec4 u_gain;           // gain (0..1), grain, colour noise, smear strength
uniform float u_field;

uvec3 pcg3d(uvec3 v) {
    v = v * 1664525u + 1013904223u;
    v.x += v.y * v.z;
    v.y += v.z * v.x;
    v.z += v.x * v.y;
    v ^= v >> 16u;
    v.x += v.y * v.z;
    v.y += v.z * v.x;
    v.z += v.x * v.y;
    return v;
}

// -1..1 each.
vec3 hash3(uvec3 v) {
    return vec3(pcg3d(v) >> 8) / 8388608.0 - 1.0;
}

void main() {
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    vec3 color = texelFetch(u_scene, pixel, 0).rgb * camera_exposure();
    color += texture(u_bloom, v_uv).rgb * u_tone.w;
    int last_column = textureSize(u_smear, 0).x - 1;
    vec3 smear = 0.5 * texelFetch(u_smear, ivec2(pixel.x, 0), 0).rgb +
                 0.25 * texelFetch(u_smear, ivec2(max(pixel.x - 1, 0), 0), 0).rgb +
                 0.25 * texelFetch(u_smear, ivec2(min(pixel.x + 1, last_column), 0), 0).rgb;
    color += smear * u_gain.w;

    vec3 light = max(texelFetch(u_adapt, ivec2(0), 0).rgb, vec3(0.001));
    vec3 balance = mix(vec3(1.0), vec3(dot(light, luma_weights)) / light, u_white_balance.w) * u_white_balance.rgb;
    color *= balance / dot(balance, luma_weights);

    // Soft shoulder up to clipping at 2 - shoulder, then the toe.
    float knee = u_tone.x;
    vec3 over = clamp(color - knee, 0.0, 2.0 * (1.0 - knee));
    color = max(min(color, vec3(knee)) + over - over * over / (4.0 * max(1.0 - knee, 0.001)), 0.0);
    color = color * color / (color + u_tone.y) * (1.0 + u_tone.y);

    float luma = dot(color, luma_weights);
    color = mix(vec3(luma), color, u_tone.z);
    float dark = 1.0 - clamp(luma, 0.0, 1.0);
    color += u_shadow_tint * (dark * dark * dark * dark * dark * dark);

    uvec3 seed = uvec3(uvec2(pixel), uint(u_field));
    vec3 noise = hash3(seed);
    vec3 chroma = noise - dot(noise, luma_weights);
    float grain = hash3(seed ^ uvec3(0x9e37u, 0x79b9u, 0x7f4au)).x;
    color += u_gain.x * (u_gain.y * grain + u_gain.z * chroma);
    frag_color = vec4(clamp(color, 0.0, 1.0), 1.0);
}
