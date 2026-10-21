#version 330 core

layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec3 a_color;
layout(location = 3) in float a_rough;
layout(location = 4) in float a_ao;
layout(location = 5) in vec4 a_horizon_a;
layout(location = 6) in vec4 a_horizon_b;

uniform mat4 u_mvp;
uniform mat4 u_model;
// World to the shadow map (renderer/shadow_map_pass.h).
uniform mat4 u_shadow_view_proj;

out vec3 v_normal;
out vec3 v_color;
out float v_rough;
out float v_ao;
out vec4 v_horizon_a;
out vec4 v_horizon_b;
out vec2 v_ground;
out vec3 v_world;
out vec3 v_shadow;  // where on the shadow map, and its depth there, 0..1

void main() {
    gl_Position = u_mvp * vec4(a_pos, 1.0);
    v_normal = normalize(mat3(transpose(inverse(u_model))) * a_normal);
    v_color = a_color;
    v_rough = a_rough;
    v_ao = a_ao;
    v_horizon_a = a_horizon_a;
    v_horizon_b = a_horizon_b;
    v_world = (u_model * vec4(a_pos, 1.0)).xyz;
    v_ground = v_world.xz;
    v_shadow = (u_shadow_view_proj * vec4(v_world, 1.0)).xyz * 0.5 + 0.5;
}
