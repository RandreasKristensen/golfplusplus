#version 330 core

// Every model's pieces (renderer/prop_batch.h): the cigarette, the can, the
// cart's body, the club. Each is a canonical mesh placed in the world by its
// instance (mesh_instance.glsl).
// A glow below zero names a slot of this frame's glow floors (a static
// model's, so night glow is never baked into it); zero up is the floor itself.

#include "mesh_instance.glsl"

layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;

uniform mat4 u_view_proj;
// The glow floors by slot (renderer/prop_glow.h), this frame's.
uniform vec4 u_glow_floors[2];
// World to the shadow map (renderer/shadow_map_pass.h).
uniform mat4 u_shadow_view_proj;

out vec3 v_world;
out vec3 v_normal;
flat out vec3 v_color;
flat out float v_alpha;
flat out float v_glow;
out vec3 v_shadow;  // where on the shadow map, and its depth there, 0..1

void main() {
    vec3 world = instance_world(a_pos);
    gl_Position = u_view_proj * vec4(world, 1.0);
    v_world = world;
    v_normal = instance_normal(a_normal);
    v_color = a_tint.rgb;
    v_alpha = a_tint.a;
    v_glow = a_glow < 0.0 ? u_glow_floors[int(-a_glow - 1.0) / 4][int(-a_glow - 1.0) % 4] : a_glow;
    v_shadow = (u_shadow_view_proj * vec4(world, 1.0)).xyz * 0.5 + 0.5;
}
