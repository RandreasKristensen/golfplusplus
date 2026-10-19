#version 330 core

#include "lighting.glsl"

// Batched world markers (ground discs, pin cups, flagsticks, aim dots, swing
// club, carts, other players). Positions are already world space; colour and
// glow floor are per piece. Unlit, but only as bright as the light there is.

layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_color;
layout(location = 2) in float a_glow;

uniform mat4 u_view_proj;

flat out vec4 v_color;

void main() {
    gl_Position = u_view_proj * vec4(a_pos, 1.0);
    v_color = vec4(a_color * (flat_light(a_glow) + local_light(a_pos)), 1.0);
}
