#version 330 core

#include "lighting.glsl"
#include "shadow_map_light.glsl"

// A model's piece lit where it is in the world: the sun on the side facing
// it, less what the shadow map takes away, the sky and the ground's bounce,
// the night lights on the side facing them, and never below the glow floor
// its part names. A thin panel is lit on the side facing the camera.
// renderer/prop_light.h is the same sum on the CPU, for tests.

in vec3 v_world;
in vec3 v_normal;
flat in vec3 v_color;
flat in float v_alpha;
flat in float v_glow;
in vec3 v_shadow;

out vec4 frag_color;

void main() {
    vec3 n = normalize(v_normal);
    if (dot(n, haze_eye.xyz - v_world) < 0.0) {
        n = -n;
    }
    vec3 light = surface_light(n, 1.0, shadow_map_light(v_shadow)) + local_light(v_world, n);
    frag_color = vec4(hazed(v_color * max(light, vec3(v_glow)), v_world), v_alpha);
}
