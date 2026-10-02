#version 330 core

// The course's panorama behind everything: each pixel looks up the picture in
// the direction it sees, so the horizon stays put as the camera turns.

in vec2 v_ndc;

out vec4 frag_color;

// The camera's rotation and projection, inverted (no translation: the
// panorama is infinitely far away).
uniform mat4 u_inverse_view_proj;
uniform sampler2D u_panorama;

// The picture wraps once around the horizon and spans these elevations from
// its bottom row to its top row. Keep in step with tooling/art/make_art.py.
const float bottom_elevation = radians(-30.0);
const float top_elevation = radians(60.0);
const float full_turn = 6.28318530718;

void main() {
    vec4 far_point = u_inverse_view_proj * vec4(v_ndc, 1.0, 1.0);
    vec3 direction = normalize(far_point.xyz / far_point.w);
    float u = atan(direction.x, -direction.z) / full_turn + 0.5;
    float v = (asin(clamp(direction.y, -1.0, 1.0)) - bottom_elevation) / (top_elevation - bottom_elevation);
    frag_color = vec4(texture(u_panorama, vec2(u, v)).rgb, 1.0);
}
