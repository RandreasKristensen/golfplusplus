#version 330 core

// The camcorder's lag, one pixel: the log of the scene's mean brightness
// (alpha) eases towards this frame's for the iris, the light's colour (rgb)
// towards this frame's for the white balance.

out vec4 frag_color;

uniform sampler2D u_quarter;   // its smallest mip is the scene's mean
uniform sampler2D u_previous;  // last frame's
uniform vec3 u_light;
uniform vec2 u_blend;          // iris, white balance: how far towards this frame (1 cuts)

const vec3 luma_weights = vec3(0.2126, 0.7152, 0.0722);

void main() {
    vec3 mean = textureLod(u_quarter, vec2(0.5), 32.0).rgb;
    float log_mean = log(max(dot(mean, luma_weights), 0.0001));
    vec4 previous = texelFetch(u_previous, ivec2(0), 0);
    frag_color = vec4(mix(previous.rgb, u_light, u_blend.y), mix(previous.a, log_mean, u_blend.x));
}
