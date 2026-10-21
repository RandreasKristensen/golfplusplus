#version 330 core

#include "lighting.glsl"

// The tee boxes (world_marker_batch.h build_tee_box_batch): world markers
// that the shadow map falls on, so a golfer on the tee sees their shadow.

layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_color;
layout(location = 2) in float a_glow;

uniform mat4 u_view_proj;
// World to the shadow map (renderer/shadow_map_pass.h).
uniform mat4 u_shadow_view_proj;

flat out vec3 v_color;
flat out float v_glow;
flat out vec3 v_local;
out vec3 v_shadow;  // where on the shadow map, and its depth there, 0..1

void main() {
    gl_Position = u_view_proj * vec4(a_pos, 1.0);
    v_color = a_color;
    v_glow = a_glow;
    v_local = local_light(a_pos);
    v_shadow = (u_shadow_view_proj * vec4(a_pos, 1.0)).xyz * 0.5 + 0.5;
}
