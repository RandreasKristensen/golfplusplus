#version 330 core

#include "camcorder.glsl"

// CCD smear, one pixel per column. While the CCD shifts its charge out, the
// whole column passes under the light falling on it, so every row picks up
// a little of all the light past the threshold in the column, summed in
// picture heights: only light the camera sees smears, a sliver of sun
// faintly and the whole disc fully. Alpha is where down the picture that
// light is (0..1, its mean), which the camcorder pass fades the smear away
// from.

out vec4 frag_color;

uniform sampler2D u_scene;
uniform float u_threshold;

void main() {
    ivec2 size = textureSize(u_scene, 0);
    int x = int(gl_FragCoord.x);
    float exposure = camera_exposure();
    vec3 sum = vec3(0.0);
    float weight = 0.0;
    float row = 0.0;
    for (int y = 0; y < size.y; ++y) {
        vec3 color = texelFetch(u_scene, ivec2(x, y), 0).rgb * exposure;
        float luma = dot(color, luma_weights);
        float excess = max(luma - u_threshold, 0.0);
        sum += color * (excess / max(luma, 0.0001));
        weight += excess;
        row += excess * (float(y) + 0.5);
    }
    float height = float(size.y);
    frag_color = vec4(sum / height, weight > 0.0 ? row / (weight * height) : 0.5);
}
