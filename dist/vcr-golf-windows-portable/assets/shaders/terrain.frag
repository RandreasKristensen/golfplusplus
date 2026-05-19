#version 330 core

in vec3 v_normal;
in vec3 v_color;

out vec4 frag_color;

uniform vec3 u_color;
uniform vec3 u_light_dir;
uniform float u_alpha;
uniform int u_use_vertex_color;

void main() {
    if (u_use_vertex_color == 0) {
        frag_color = vec4(u_color, u_alpha);
        return;
    }

    vec3 base_color = v_color;
    float light = max(dot(normalize(v_normal), normalize(u_light_dir)), 0.0);
    vec3 lit_color = base_color * (0.70 + light * 0.30);
    frag_color = vec4(lit_color, u_alpha);
}
