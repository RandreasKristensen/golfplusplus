#version 330 core

#include "camcorder.glsl"

// CCD smear, one pixel per column: the brightest light in the column past
// the threshold, in its colour. The camcorder pass runs it down the whole
// column.

out vec4 frag_color;

uniform sampler2D u_scene;
uniform float u_threshold;

void main() {
    ivec2 size = textureSize(u_scene, 0);
    int x = int(gl_FragCoord.x);
    vec3 brightest = vec3(0.0);
    float brightest_luma = 0.0;
    for (int y = 0; y < size.y; ++y) {
        vec3 color = texelFetch(u_scene, ivec2(x, y), 0).rgb;
        float luma = dot(color, luma_weights);
        if (luma > brightest_luma) {
            brightest_luma = luma;
            brightest = color;
        }
    }
    float exposure = camera_exposure();
    float excess = max(brightest_luma * exposure - u_threshold, 0.0);
    frag_color = vec4(brightest / max(brightest_luma, 0.0001) * excess, 1.0);
}
