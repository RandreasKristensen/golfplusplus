#version 330 core

// A streak of rain or a wind's speck, lit in weather_streak.vert.

in vec4 v_color;

out vec4 frag_color;

void main() {
    frag_color = v_color;
}
