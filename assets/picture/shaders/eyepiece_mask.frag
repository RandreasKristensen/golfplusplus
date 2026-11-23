#version 330 core

// Writes the nearest depth where the eyepiece throws the picture away
// (renderer/eyepiece_pass.h), so the depth test rejects everything drawn
// there. u_mask: the circle's middle in pixels, its radius, and +1 to cover
// its inside or -1 its outside.

uniform vec4 u_mask;

void main() {
    float beyond = length(gl_FragCoord.xy - u_mask.xy) - u_mask.z;
    if (beyond * u_mask.w >= 0.0) {
        discard;
    }
    gl_FragDepth = 0.0;
}
