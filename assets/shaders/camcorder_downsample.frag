#version 330 core

// The scene at a quarter of its size each way, each pixel the mean of the
// 4x4 it covers: what the camcorder meters and blooms from.

out vec4 frag_color;

uniform sampler2D u_scene;

void main() {
    ivec2 size = textureSize(u_scene, 0);
    ivec2 base = ivec2(gl_FragCoord.xy) * 4;
    vec3 sum = vec3(0.0);
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            sum += texelFetch(u_scene, min(base + ivec2(x, y), size - 1), 0).rgb;
        }
    }
    frag_color = vec4(sum / 16.0, 1.0);
}
