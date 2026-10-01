#version 330 core

// Instanced tree trunks and leaves, flat coloured (paired with world_marker.frag).
// World position = a_instance_offset + a_pos * a_instance_scale.

layout(location = 0) in vec3 a_pos;
layout(location = 3) in vec3 a_instance_offset;
layout(location = 4) in vec3 a_instance_scale;

uniform mat4 u_view_proj;
uniform vec3 u_color;

flat out vec4 v_color;

void main() {
    gl_Position = u_view_proj * vec4(a_instance_offset + a_pos * a_instance_scale, 1.0);
    v_color = vec4(u_color, 1.0);
}
