#version 330 core

// The rangefinder's eyepiece at scene resolution (renderer/eyepiece_pass.h):
// inside the round view the scene drawn again at the eyepiece's zoom; round
// it the scene darkened and greyed.

out vec4 frag_color;

uniform sampler2D u_scene;
uniform sampler2D u_zoom;
uniform vec3 u_eyepiece;  // radius in pixels, surround brightness, surround saturation

void main() {
    vec2 center = vec2(textureSize(u_scene, 0)) * 0.5;
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    if (length(gl_FragCoord.xy - center) < u_eyepiece.x) {
        frag_color = vec4(texelFetch(u_zoom, pixel, 0).rgb, 1.0);
        return;
    }
    vec3 color = texelFetch(u_scene, pixel, 0).rgb;
    float luma = dot(color, vec3(0.299, 0.587, 0.114));
    frag_color = vec4(mix(vec3(luma), color, u_eyepiece.z) * u_eyepiece.y, 1.0);
}
