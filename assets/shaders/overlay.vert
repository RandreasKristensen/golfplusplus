#version 330 core

// Batched 2D overlay: positions arrive already in overlay clip space.
layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec4 a_color;

// Every vertex of a quad carries the same colour; flat avoids any
// interpolation drift so the output matches the old uniform colour exactly.
flat out vec4 v_color;

void main() {
    gl_Position = vec4(a_pos, 0.0, 1.0);
    v_color = a_color;
}
