#version 330 core

// The full-screen quad the underwater tint covers.

layout(location = 0) in vec3 a_pos;

void main() {
    gl_Position = vec4(a_pos.xy, 0.0, 1.0);
}
