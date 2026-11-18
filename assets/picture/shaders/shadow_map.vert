#version 330 core

// The trees', floodlights', figures' and carts' depth seen from the sun
// (renderer/shadow_map_pass.h), and the trees' and floodlights' from a
// floodlight (renderer/lamp_shadow_pass.h), instanced like tree.vert (including its yaw); the
// floodlights' and figures' meshes are one unscaled instance at the origin.

layout(location = 0) in vec3 a_pos;
layout(location = 3) in vec3 a_instance_offset;
layout(location = 4) in vec3 a_instance_scale;

#ifdef TREE_CASTER
#include "tree_pose.glsl"
uniform float u_time;
uniform vec2 u_wind;
layout(location = 2) in vec4 a_leaf;
layout(location = 5) in vec2 a_instance_turn;
out vec2 v_uv;
out float v_leaf;
flat out float v_tint;
#endif

uniform mat4 u_view_proj;

void main() {
    vec3 world = a_instance_offset + a_pos * a_instance_scale;
#ifdef TREE_CASTER
    world=tree_world_position(a_pos,a_instance_scale,a_instance_offset,a_instance_turn.x,u_time,u_wind);
    v_uv = a_leaf.xy;
    v_leaf = a_leaf.z;
    v_tint = a_instance_turn.y;
#endif
    gl_Position = u_view_proj * vec4(world, 1.0);
}
