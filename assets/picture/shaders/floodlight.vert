#version 330 core

#include "lighting.glsl"

// The floodlights' poles and lamp heads (renderer/floodlight_batch.h),
// already in world space (paired with world_marker.frag), lit per vertex and
// faded into the haze with distance like the trees. A lamp's face glows of
// itself in lamp_glow: past white after dark, for the camcorder to bloom.

layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec3 a_color;
layout(location = 5) in float a_lamp;

uniform mat4 u_view_proj;

flat out vec4 v_color;

void main() {
    gl_Position = u_view_proj * vec4(a_pos, 1.0);
    vec3 lit = a_color * (surface_light(a_normal, 1.0) + local_light(a_pos, a_normal));
    v_color = vec4(hazed(lit + a_lamp * lamp_glow.rgb, a_pos), 1.0);
}
