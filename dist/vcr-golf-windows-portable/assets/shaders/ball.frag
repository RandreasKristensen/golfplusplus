#version 330 core

out vec4 frag_color;

uniform vec3 u_color;
uniform vec3 u_light_dir;

in vec3 v_normal;

void main() {
    float light = max(dot(normalize(v_normal), normalize(u_light_dir)), 0.0);
    vec3 lit_color = u_color * (0.45 + light * 0.55);
    frag_color = vec4(lit_color, 1.0);
}
