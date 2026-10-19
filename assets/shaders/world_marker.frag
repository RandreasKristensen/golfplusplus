#version 330 core

// A flat colour, its light already worked out per vertex: world markers and trees.

flat in vec4 v_color;

out vec4 frag_color;

void main() {
    frag_color = v_color;
}
