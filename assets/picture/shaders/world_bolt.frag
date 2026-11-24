#version 330 core

// A bolt's ribbon, added over the picture: a hard core a third of the way out
// and a soft halo to its edge, in u_color (brightness and flash included).

in vec2 v_ribbon;

uniform vec3 u_color;

out vec4 frag_color;

void main() {
    float out_from_axis = abs(v_ribbon.x);
    float core = 1.0 - smoothstep(0.25, 0.36, out_from_axis);
    float halo = 0.25 * pow(max(1.0 - out_from_axis, 0.0), 2.0);
    frag_color = vec4(u_color * (core + halo) * v_ribbon.y, 1.0);
}
