#version 330 core

// The course's backdrop behind everything: each pixel looks up the panoramas
// in the direction it sees, so the horizon stays put as the camera turns. The
// sky goes down first, then the land over it, the land fading into the haze
// colour as far as its alpha says.

in vec2 v_ndc;

out vec4 frag_color;

// The camera's rotation and projection, inverted (no translation: the
// panoramas are infinitely far away).
uniform mat4 u_inverse_view_proj;
uniform sampler2D u_sky;
// RGB is the land's own colour, alpha how much of it shows through the haze:
// 0 where the sky shows, otherwise at least min_land_visibility.
uniform sampler2D u_land;
uniform vec3 u_haze_color;
// Scales the drawn haze: 1 as drawn, 0 clear.
uniform float u_haze_amount;

// The pictures wrap once around the horizon and span these elevations from
// their bottom row to their top row; land never shows less than this through
// the haze. Keep in step with tooling/art/make_art.py.
const float bottom_elevation = radians(-30.0);
const float top_elevation = radians(60.0);
const float min_land_visibility = 0.15;
const float full_turn = 6.28318530718;

void main() {
    vec4 far_point = u_inverse_view_proj * vec4(v_ndc, 1.0, 1.0);
    vec3 direction = normalize(far_point.xyz / far_point.w);
    float u = atan(direction.x, -direction.z) / full_turn + 0.5;
    float v = (asin(clamp(direction.y, -1.0, 1.0)) - bottom_elevation) / (top_elevation - bottom_elevation);
    vec3 sky = texture(u_sky, vec2(u, v)).rgb;
    vec4 land = texture(u_land, vec2(u, v));
    // Any land at all covers the sky; what of it the haze hides shows the
    // haze colour instead.
    float coverage = clamp(land.a / min_land_visibility, 0.0, 1.0);
    float haze = min((coverage - land.a) * u_haze_amount, coverage);
    vec3 color = sky * (1.0 - coverage) + u_haze_color * haze + land.rgb * (coverage - haze);
    frag_color = vec4(color, 1.0);
}
