#version 330 core

layout(location = 0) in vec3 a_pos;

out vec2 v_ndc;

void main() {
    v_ndc = a_pos.xy;
    gl_Position = vec4(a_pos.xy, 0.0, 1.0);
}
