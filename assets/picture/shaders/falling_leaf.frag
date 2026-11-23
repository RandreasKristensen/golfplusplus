#version 330 core

#include "lighting.glsl"
#include "shadow_map_light.glsl"

// A falling leaf: an ellipse cut from its quad (never blended), in the
// species' leaf colour on the day or withered, the instance's tint; lit on
// both faces, the back darker, in the shadow map's shade, hazed.

in vec3 v_world;
in vec3 v_shadow;
in vec2 v_uv;
in vec3 v_normal;
flat in float v_tint;

out vec4 frag_color;

uniform vec3 u_leaf_color;
uniform vec3 u_withered_color;
uniform float u_withered;  // the share of falling leaves that are withered

void main() {
    vec2 f = v_uv * 2.0 - 1.0;
    if (dot(f / vec2(1.0, 0.72), f / vec2(1.0, 0.72)) > 1.0) {
        discard;
    }
    vec3 n = normalize(v_normal);
    float back = 1.0;
    if (!gl_FrontFacing) {
        n = -n;
        back = 0.75;
    }
    vec3 to_eye = normalize(haze_eye.xyz - v_world);
    n = dot(n, to_eye) < 0.0 ? -n : n;
    float sun = shadow_map_light(v_shadow);
    vec3 color = mix(u_leaf_color, u_withered_color, u_withered) * (0.9 + 0.2 * v_tint) * back;
    vec3 light = surface_light(n, 1.0, sun) + local_light(v_world, n);
    frag_color = vec4(hazed(color * light * wet.x, v_world), 1.0);
}
