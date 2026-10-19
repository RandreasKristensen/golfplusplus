#version 330 core

// A blob shadow: dark at its middle, fading out softly to its edge, the way
// a camcorder's soft lens sees a shadow. Blended over the ground.

in vec2 v_corner;
in float v_strength;

out vec4 frag_color;

void main() {
    float edge = length(v_corner);
    frag_color = vec4(0.0, 0.0, 0.0, v_strength * (1.0 - smoothstep(0.15, 1.0, edge)));
}
