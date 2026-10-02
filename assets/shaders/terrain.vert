#version 330 core

layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec3 a_color;
layout(location = 3) in float a_rough;

uniform mat4 u_mvp;
uniform mat4 u_model;
uniform int u_use_vertex_color;

out vec3 v_normal;
out vec3 v_color;
out float v_rough;
out vec2 v_ground;

void main() {
    gl_Position = u_mvp * vec4(a_pos, 1.0);
    v_normal = normalize(mat3(transpose(inverse(u_model))) * a_normal);
    v_color = a_color;
    v_rough = a_rough;
    v_ground = (u_model * vec4(a_pos, 1.0)).xz;
}
