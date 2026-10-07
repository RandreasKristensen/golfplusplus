#version 330 core

// Instanced tree trunks and leaves, flat coloured (paired with world_marker.frag)
// and faded into the backdrop's haze with distance like the ground
// (renderer/scene_haze.h). World position = a_instance_offset + a_pos * a_instance_scale.

layout(location = 0) in vec3 a_pos;
layout(location = 3) in vec3 a_instance_offset;
layout(location = 4) in vec3 a_instance_scale;

uniform mat4 u_view_proj;
uniform vec3 u_color;
uniform vec3 u_haze_color;
uniform float u_haze_far_amount;
uniform float u_haze_full_distance;
uniform vec3 u_haze_eye;

flat out vec4 v_color;

void main() {
    vec3 world = a_instance_offset + a_pos * a_instance_scale;
    gl_Position = u_view_proj * vec4(world, 1.0);
    float far = min(distance(world, u_haze_eye) / u_haze_full_distance, 1.0);
    v_color = vec4(mix(u_color, u_haze_color, u_haze_far_amount * far * far), 1.0);
}
