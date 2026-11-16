#version 330 core

// r: the ground's height over the map's base; g: its blades, 1 rough, 2
// heavy rough, 0 none.

in float v_height;
in float v_blades;

out vec4 frag_color;

uniform float u_base;

void main() {
    frag_color = vec4(v_height - u_base, max(v_blades, 0.0), 0.0, 1.0);
}
