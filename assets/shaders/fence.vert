#version 330 core

// Fences: posts and nets, already in world space.

layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec3 a_color;
layout(location = 3) in vec2 a_uv;

uniform mat4 u_view_proj;

out vec3 v_normal;
out vec3 v_color;
out vec2 v_uv;
out vec3 v_world;

void main() {
    gl_Position = u_view_proj * vec4(a_pos, 1.0);
    v_normal = a_normal;
    v_color = a_color;
    v_uv = a_uv;
    v_world = a_pos;
}
