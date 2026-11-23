#version 330 core

#include "tree_view.glsl"

// One view of a grown tree model in its unit frame, orthographic, for
// the image grid (renderer/tree_impostor_renderer.h), drawn finer than a tile
// and averaged down into it.

layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec4 a_leaf;
layout(location = 6) in vec2 a_card;

uniform int u_view_azimuth;
uniform int u_view_elevation;

out vec3 v_normal;
out vec2 v_uv;
out float v_leaf;
out float v_depth;
out float v_ray;
flat out vec2 v_card;

void main() {
    vec3 toward_eye = tree_grid_direction(u_view_azimuth, u_view_elevation);
    vec3 right;
    vec3 up;
    tree_view_axes(toward_eye, right, up);
    vec3 reach = tree_view_reach(toward_eye, right, up);
    vec3 d = a_pos - u_box_centre;
    gl_Position = vec4(dot(d, right) / reach.x, dot(d, up) / reach.y, -dot(d, toward_eye) / reach.z, 1.0);
    v_ray = dot(d, toward_eye) / reach.z * 0.5 + 0.5;
    v_normal = a_normal;
    v_uv = a_leaf.xy;
    v_leaf = a_leaf.z;
    v_depth = a_leaf.w;
    v_card = a_card;
}
