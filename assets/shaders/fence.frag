#version 330 core

// A fence's posts in their colour, or its net from the net texture (strands
// and holes, a faint veil once mipmapped far off). Lit like the terrain on
// whichever side faces the light, fading into the same haze with distance.

in vec3 v_normal;
in vec3 v_color;
in vec2 v_uv;
in vec3 v_world;

out vec4 frag_color;

uniform sampler2D u_net;
uniform float u_textured;  // 1 for nets
uniform vec3 u_light_dir;
// The haze far things fade into, matching the backdrop (renderer/scene_haze.h).
uniform vec3 u_haze_color;
uniform float u_haze_far_amount;
uniform float u_haze_full_distance;
uniform vec3 u_haze_eye;

void main() {
    // Sampled before choosing so the mipmap choice stays well defined.
    vec4 net = texture(u_net, v_uv);
    vec4 base = mix(vec4(v_color, 1.0), net, clamp(u_textured, 0.0, 1.0));
    if (base.a < 0.02) {
        discard;
    }
    float light = abs(dot(normalize(v_normal), normalize(u_light_dir)));
    float far = min(distance(v_world, u_haze_eye) / u_haze_full_distance, 1.0);
    float haze = u_haze_far_amount * far * far;
    frag_color = vec4(mix(base.rgb * (0.70 + light * 0.30), u_haze_color, haze), base.a);
}
