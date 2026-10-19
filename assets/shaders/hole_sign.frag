#version 330 core

#include "lighting.glsl"

// A sign's face from its texture, wood elsewhere, lit like the terrain and
// fading into the haze with distance.

in vec3 v_normal;
in vec3 v_color;
in vec2 v_uv;
in float v_textured;
in vec3 v_world;

out vec4 frag_color;

uniform sampler2D u_face;

void main() {
    // Sampled before choosing so the mipmap choice stays well defined.
    vec3 face = texture(u_face, v_uv).rgb;
    vec3 base_color = mix(v_color, face, clamp(v_textured, 0.0, 1.0));
    frag_color = vec4(hazed(base_color * (surface_light(v_normal, 1.0) + local_light(v_world, v_normal)), v_world), 1.0);
}
