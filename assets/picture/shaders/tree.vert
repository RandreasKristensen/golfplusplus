#version 330 core

#include "lighting.glsl"
#include "tree_pose.glsl"

// A tree of its species' grown model (renderer/tree_model.h), one instance
// per tree: the unit frame scaled to its crown and height, turned about its
// trunk (render_tree.h's tree_world_point), the crown swaying in the wind,
// more the higher it is.

layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec4 a_leaf;  // xy: a leaf card's corner; z: 1 on leaves; w: how deep in the crown
layout(location = 3) in vec3 a_instance_offset;
layout(location = 4) in vec3 a_instance_scale;
layout(location = 5) in vec2 a_instance_turn;  // x: yaw; y: tint

uniform mat4 u_view_proj;
uniform mat4 u_shadow_view_proj;
uniform float u_time;
uniform vec2 u_wind;  // m/s, x and z

out vec3 v_world;
out vec3 v_normal;
out vec3 v_shadow;
out vec2 v_uv;
out float v_leaf;
out float v_depth;
flat out float v_tint;

void main() {
    float c = cos(a_instance_turn.x);
    float s = sin(a_instance_turn.x);
    vec3 world=tree_world_position(a_pos,a_instance_scale,a_instance_offset,a_instance_turn.x,u_time,u_wind);
    vec3 n = a_normal / a_instance_scale;
    v_normal = normalize(vec3(c * n.x - s * n.z, n.y, s * n.x + c * n.z));
    gl_Position = u_view_proj * vec4(world, 1.0);
    v_world = world;
    v_shadow = (u_shadow_view_proj * vec4(world, 1.0)).xyz * 0.5 + 0.5;
    v_uv = a_leaf.xy;
    v_leaf = a_leaf.z;
    v_depth = a_leaf.w;
    v_tint = a_instance_turn.y;
}
