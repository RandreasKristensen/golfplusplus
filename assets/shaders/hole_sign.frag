#version 330 core

// A sign's face from its texture, wood elsewhere, lit like the terrain and
// fading into the same haze with distance.

in vec3 v_normal;
in vec3 v_color;
in vec2 v_uv;
in float v_textured;
in vec3 v_world;

out vec4 frag_color;

uniform sampler2D u_face;
uniform vec3 u_light_dir;
// The haze far things fade into, matching the backdrop (renderer/scene_haze.h).
uniform vec3 u_haze_color;
uniform float u_haze_far_amount;
uniform float u_haze_full_distance;
uniform vec3 u_haze_eye;

void main() {
    // Sampled before choosing so the mipmap choice stays well defined.
    vec3 face = texture(u_face, v_uv).rgb;
    vec3 base_color = mix(v_color, face, clamp(v_textured, 0.0, 1.0));
    float light = max(dot(normalize(v_normal), normalize(u_light_dir)), 0.0);
    float far = min(distance(v_world, u_haze_eye) / u_haze_full_distance, 1.0);
    float haze = u_haze_far_amount * far * far;
    frag_color = vec4(mix(base_color * (0.70 + light * 0.30), u_haze_color, haze), 1.0);
}
