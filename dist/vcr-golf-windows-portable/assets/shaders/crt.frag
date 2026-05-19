#version 330 core

in vec2 v_uv;

out vec4 frag_color;

uniform sampler2D u_scene;
uniform vec2 u_resolution;

void main() {
    vec3 color = texture(u_scene, v_uv).rgb;

    float scan = 0.04 * sin(v_uv.y * u_resolution.y * 3.14159);
    color = max(color - scan, 0.0);

    float dist = length(v_uv - vec2(0.5));
    float vignette = smoothstep(0.75, 0.25, dist);
    color *= vignette;

    frag_color = vec4(color, 1.0);
}
