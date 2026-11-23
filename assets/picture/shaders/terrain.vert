#version 330 core

layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec4 a_normal;  // x, y, z as 10-bit signed normalized (renderer/terrain_vertex.h)
layout(location = 2) in vec4 a_color_rough;  // linear colour, rough 0..1
layout(location = 3) in vec4 a_surface;  // paved, ao, turf, blades as bytes, not normalized
layout(location = 4) in vec4 a_horizon_a;
layout(location = 5) in vec4 a_horizon_b;

// The steps of turf and blades per 1.0 (render_terrain_vertex::level_steps).
const float level_steps = 127.0;

uniform mat4 u_mvp;
uniform mat4 u_model;
// How far, per metre of depth, the vertex is lifted: the material overlay
// over a coarser ground stands that far over the full ground it was cut from.
uniform float u_lift_per_depth;
// World to the shadow map (renderer/shadow_map_pass.h).
uniform mat4 u_shadow_view_proj;

out vec3 v_normal;
out vec3 v_color;
out float v_rough;
out float v_paved;
out float v_turf;
out float v_ao;
out vec4 v_horizon_a;
out vec4 v_horizon_b;
out vec2 v_ground;
out vec3 v_world;
out vec3 v_shadow;  // where on the shadow map, and its depth there, 0..1

void main() {
    gl_Position = u_mvp * vec4(a_pos, 1.0);
    if (u_lift_per_depth != 0.0) {
        gl_Position = u_mvp * vec4(a_pos + vec3(0.0, u_lift_per_depth * gl_Position.w, 0.0), 1.0);
    }
    v_normal = normalize(mat3(transpose(inverse(u_model))) * a_normal.xyz);
    v_color = a_color_rough.rgb;
    v_rough = a_color_rough.a;
    v_paved = a_surface.x / 255.0;
    v_ao = a_surface.y / 255.0;
    v_turf = a_surface.z / level_steps;
    v_horizon_a = a_horizon_a;
    v_horizon_b = a_horizon_b;
    v_world = (u_model * vec4(a_pos, 1.0)).xyz;
    v_ground = v_world.xz;
    v_shadow = (u_shadow_view_proj * vec4(v_world, 1.0)).xyz * 0.5 + 0.5;
}
