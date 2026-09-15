#version 330 core

// Unlit, exactly like terrain.frag's u_use_vertex_color == 0 path.

flat in vec4 v_color;

out vec4 frag_color;

void main() {
    frag_color = v_color;
}
