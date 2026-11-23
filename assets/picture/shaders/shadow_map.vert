#version 330 core

// The trees', floodlights', figures' and carts' depth seen from the sun
// (renderer/shadow_map_pass.h), and the trees' and floodlights' from a
// floodlight (renderer/lamp_shadow_pass.h), instanced like tree.vert (including its yaw); the
// floodlights' and flagsticks' meshes are one unscaled instance at the origin. With
// MODEL_CASTER the figures' and models' pieces are canonical meshes placed by their
// instances (mesh_instance.glsl).

layout(location = 0) in vec3 a_pos;
#ifdef MODEL_CASTER
#include "mesh_instance.glsl"
#else
layout(location = 3) in vec3 a_instance_offset;
layout(location = 4) in vec3 a_instance_scale;
#endif

#ifdef TREE_CASTER
#include "tree_pose.glsl"
uniform float u_time;
uniform vec2 u_wind;
layout(location = 2) in vec4 a_leaf;
layout(location = 5) in vec2 a_instance_turn;
layout(location = 6) in vec2 a_card;
out vec2 v_uv;
flat out vec2 v_card;
out float v_leaf;
flat out float v_tint;
#endif

uniform mat4 u_view_proj;

invariant gl_Position;  // the trees' depth pass (tree_renderer.h) must match tree.vert exactly

void main() {
#ifdef MODEL_CASTER
    vec3 world = instance_world(a_pos);
#else
    vec3 world = a_instance_offset + a_pos * a_instance_scale;
#endif
#ifdef TREE_CASTER
    world=tree_world_position(a_pos,a_instance_scale,a_instance_offset,a_instance_turn.x,u_time,u_wind);
    v_uv = a_leaf.xy;
    v_leaf = a_leaf.z;
    v_tint = a_instance_turn.y;
    v_card = a_card;
#endif
    gl_Position = u_view_proj * vec4(world, 1.0);
}
