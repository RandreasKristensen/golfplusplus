#version 330 core

#include "camcorder.glsl"

// What spills from the brightest parts at a quarter of the scene's size:
// their light past the threshold, spread wider sideways than up and down.

in vec2 v_uv;

out vec4 frag_color;

uniform sampler2D u_quarter;
uniform vec3 u_bloom;  // threshold, tap spacing sideways and up and down (uv)

vec3 bright(vec2 uv, float exposure) {
    vec3 color = textureLod(u_quarter, uv, 0.0).rgb * exposure;
    float luma = dot(color, luma_weights);
    return color * (max(luma - u_bloom.x, 0.0) / max(luma, 0.0001));
}

void main() {
    float exposure = camera_exposure();
    vec3 sum = vec3(0.0);
    float weight = 0.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -4; x <= 4; ++x) {
            float w = exp(-0.125 * float(x * x) - 0.5 * float(y * y));
            sum += bright(v_uv + vec2(float(x) * u_bloom.y, float(y) * u_bloom.z), exposure) * w;
            weight += w;
        }
    }
    frag_color = vec4(sum / weight, 1.0);
}
