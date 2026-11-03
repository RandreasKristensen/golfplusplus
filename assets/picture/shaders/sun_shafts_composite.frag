#version 330 core

// The sun's shafts brought up to the scene's size and taken out of it
// (renderer/sun_shaft_pass.h, blended as the scene minus this): each pixel
// mixes the four half-size pixels round it, those whose rays went about as
// far as its own counting most, so a trunk's edge keeps the shade of the air
// in front of it, not of the air behind.

#include "lighting.glsl"
#include "scene_ray.glsl"

out vec4 frag_color;

uniform sampler2D u_shafts;  // rgb: the glow to take out; a: its ray's metres

void main() {
    ivec2 texel = ivec2(gl_FragCoord.xy);
    float meters = scene_ray(texel).w;
    ivec2 last = textureSize(u_shafts, 0) - 1;
    vec2 at = (vec2(texel) + 0.5) * 0.5 - 0.5;
    ivec2 base = ivec2(floor(at));
    vec2 along = at - vec2(base);
    vec3 sum = vec3(0.0);
    float total = 0.0;
    for (int y = 0; y < 2; ++y) {
        for (int x = 0; x < 2; ++x) {
            vec4 shaft = texelFetch(u_shafts, clamp(base + ivec2(x, y), ivec2(0), last), 0);
            float near = (x == 1 ? along.x : 1.0 - along.x) * (y == 1 ? along.y : 1.0 - along.y);
            float weight = (near + 1.0e-3) / (0.02 + abs(shaft.a - meters) / max(meters, 0.1));
            sum += shaft.rgb * weight;
            total += weight;
        }
    }
    frag_color = vec4(sum / total, 0.0);
}
