#version 330 core

// The ground seen from straight above for the grass map
// (renderer/grass_field.h): each point's height and which blades grow there.

layout(location = 0) in vec3 a_pos;
layout(location = 9) in float a_blades;

uniform mat4 u_mvp;

out float v_height;
out float v_blades;

void main() {
    gl_Position = u_mvp * vec4(a_pos, 1.0);
    v_height = a_pos.y;
    v_blades = a_blades;
}
