#version 330 core

// Batched world markers (ground discs, pin cups, flagsticks, aim dots, swing
// club). Positions are already world space; color is per piece.

layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec4 a_color;

uniform mat4 u_view_proj;

flat out vec4 v_color;

void main() {
    gl_Position = u_view_proj * vec4(a_pos, 1.0);
    v_color = a_color;
}
