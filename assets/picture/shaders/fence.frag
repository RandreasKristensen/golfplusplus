#version 330 core

#include "lighting.glsl"

// A fence's posts in their colour, or its net from the net texture (strands
// and holes, a faint veil once mipmapped far off). Lit on whichever side
// faces the sun, fading into the haze with distance.

in vec3 v_normal;
in vec3 v_color;
in vec2 v_uv;
in vec3 v_world;

out vec4 frag_color;

uniform sampler2D u_net;
uniform float u_textured;  // 1 for nets

void main() {
    // Sampled before choosing so the mipmap choice stays well defined.
    vec4 net = texture(u_net, v_uv);
    vec4 base = mix(vec4(v_color, 1.0), net, clamp(u_textured, 0.0, 1.0));
    if (base.a < 0.02) {
        discard;
    }
    frag_color = vec4(hazed(base.rgb * two_sided_light(v_normal), v_world), base.a);
}
