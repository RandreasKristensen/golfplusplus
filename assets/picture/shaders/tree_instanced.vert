#version 330 core

#include "lighting.glsl"

// Instanced tree trunks and leaves (paired with world_marker.frag), lit per
// vertex and faded into the haze with distance like the ground.
// World position = a_instance_offset + a_pos * a_instance_scale.

layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;
layout(location = 3) in vec3 a_instance_offset;
layout(location = 4) in vec3 a_instance_scale;

uniform mat4 u_view_proj;
uniform vec3 u_color;

flat out vec4 v_color;

void main() {
    vec3 world = a_instance_offset + a_pos * a_instance_scale;
    gl_Position = u_view_proj * vec4(world, 1.0);
    // The scale is diagonal, so the normal scales by its inverse.
    vec3 normal = a_normal / a_instance_scale;
    v_color = vec4(hazed(u_color * (surface_light(normal, 1.0) + local_light(world, normal)), world), 1.0);
}
