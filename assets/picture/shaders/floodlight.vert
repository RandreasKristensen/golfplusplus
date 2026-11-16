#version 330 core

#include "lighting.glsl"

// The floodlights' poles and lamp heads (renderer/floodlight_batch.h),
// already in world space (paired with floodlight.frag), lit per vertex and
// faded into the haze with distance like the trees. A lamp's lens glows of
// itself in lamp_glow, hottest at its centre: past white after dark, for the
// camcorder to bloom and smear.

layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec3 a_color;
layout(location = 5) in float a_lamp;

uniform mat4 u_view_proj;

flat out vec4 v_color;
out vec3 v_glow;
out vec3 v_world;

void main() {
    gl_Position = u_view_proj * vec4(a_pos, 1.0);
    vec3 lit = a_color * (surface_light(a_normal, 1.0) + local_light(a_pos, a_normal));
    v_color = vec4(faded(lit, a_pos), 1.0);
    // What the haze leaves of the glow; faded() is linear in its colour.
    v_glow = a_lamp * lamp_glow.rgb * (1.0 - haze_at(a_pos));
    v_world = a_pos;
}
