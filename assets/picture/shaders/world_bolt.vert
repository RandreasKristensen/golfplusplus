#version 330 core

// A real lightning strike's bolt: camera-facing ribbons (renderer/world_bolt.h).

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec2 a_ribbon;  // across -1 .. 1, strength

uniform mat4 u_view_proj;

out vec2 v_ribbon;

void main() {
    v_ribbon = a_ribbon;
    gl_Position = u_view_proj * vec4(a_position, 1.0);
}
