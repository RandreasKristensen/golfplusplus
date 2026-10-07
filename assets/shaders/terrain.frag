#version 330 core

in vec3 v_normal;
in vec3 v_color;
in float v_rough;
in vec2 v_ground;
// Per fragment: the backdrop ground is two huge triangles.
in vec3 v_world;

out vec4 frag_color;

uniform vec3 u_color;
uniform vec3 u_light_dir;
uniform float u_alpha;
uniform int u_use_vertex_color;
// Blades of grass laid over the rough. A tint around mid grey: 128 leaves the
// colour as it is, darker and lighter strokes shade it.
uniform sampler2D u_grass;
// The haze far ground fades into, matching the backdrop (renderer/scene_haze.h).
uniform vec3 u_haze_color;
uniform float u_haze_far_amount;
uniform float u_haze_full_distance;
uniform vec3 u_haze_eye;

// Metres of ground one copy of the grass texture covers.
const float grass_tile_meters = 2.0;

void main() {
    // Sampled before any branch so the mipmap choice stays well defined.
    vec3 grass = texture(u_grass, v_ground / grass_tile_meters).rgb * 2.0;
    float far = min(distance(v_world, u_haze_eye) / u_haze_full_distance, 1.0);
    float haze = u_haze_far_amount * far * far;
    if (u_use_vertex_color == 0) {
        frag_color = vec4(mix(u_color, u_haze_color, haze), u_alpha);
        return;
    }

    vec3 base_color = v_color * mix(vec3(1.0), grass, clamp(v_rough, 0.0, 1.0));
    float light = max(dot(normalize(v_normal), normalize(u_light_dir)), 0.0);
    vec3 lit_color = base_color * (0.70 + light * 0.30);
    frag_color = vec4(mix(lit_color, u_haze_color, haze), u_alpha);
}
