#version 330 core

// Pond surfaces, already in world space.

layout(location = 0) in vec3 a_pos;

uniform mat4 u_view_proj;

out vec3 v_world;

void main() {
    gl_Position = u_view_proj * vec4(a_pos, 1.0);
    v_world = a_pos;
}
