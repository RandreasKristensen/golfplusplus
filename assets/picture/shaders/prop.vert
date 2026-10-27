#version 330 core

// Every model's pieces (renderer/prop_batch.h): the cigarette, the can, the
// cart's body, the club. Positions and normals are already world space.

layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec3 a_color;
layout(location = 3) in float a_alpha;
layout(location = 4) in float a_glow;

uniform mat4 u_view_proj;
// World to the shadow map (renderer/shadow_map_pass.h).
uniform mat4 u_shadow_view_proj;

out vec3 v_world;
out vec3 v_normal;
flat out vec3 v_color;
flat out float v_alpha;
flat out float v_glow;
out vec3 v_shadow;  // where on the shadow map, and its depth there, 0..1

void main() {
    gl_Position = u_view_proj * vec4(a_pos, 1.0);
    v_world = a_pos;
    v_normal = a_normal;
    v_color = a_color;
    v_alpha = a_alpha;
    v_glow = a_glow;
    v_shadow = (u_shadow_view_proj * vec4(a_pos, 1.0)).xyz * 0.5 + 0.5;
}
