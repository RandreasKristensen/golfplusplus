#version 330 core

// A pond's surface: its colour over what lies under it, fading into the same
// haze as the terrain with distance.

in vec3 v_world;

out vec4 frag_color;

uniform vec3 u_color;
uniform float u_alpha;
// The haze far things fade into, matching the backdrop (renderer/scene_haze.h).
uniform vec3 u_haze_color;
uniform float u_haze_far_amount;
uniform float u_haze_full_distance;
uniform vec3 u_haze_eye;

void main() {
    float far = min(distance(v_world, u_haze_eye) / u_haze_full_distance, 1.0);
    float haze = u_haze_far_amount * far * far;
    frag_color = vec4(mix(u_color, u_haze_color, haze), u_alpha);
}
