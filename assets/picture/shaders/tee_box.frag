#version 330 core

#include "lighting.glsl"
#include "shadow_map_light.glsl"

// A tee box's flat colour, lit as world_marker.vert lights it but for the
// sun the shadow map takes away, hazed as the ground is.

flat in vec3 v_color;
flat in float v_glow;
flat in vec3 v_local;
in vec3 v_shadow;
in vec3 v_world;

out vec4 frag_color;

void main() {
    frag_color = vec4(hazed(v_color * (flat_light(v_glow, shadow_map_light(v_shadow)) + v_local), v_world), 1.0);
}
