#version 330 core

#include "tree_view.glsl"
#include "tree_pose.glsl"

// A tree drawn as its baked images (renderer/tree_impostor.h): a strip across
// the model's picture seen from the eye, rows of it posed by the shared wind
// and clock, one instance per tree like tree.vert. The grid view nearest the eye is
// chosen here for the fragment.

layout(location = 0) in vec2 a_corner;  // -1..1 across, -1..1 up the picture
layout(location = 3) in vec3 a_instance_offset;
layout(location = 4) in vec3 a_instance_scale;
layout(location = 5) in vec2 a_instance_turn;  // x: yaw; y: tint

uniform mat4 u_view_proj;
uniform vec3 u_eye;
uniform float u_time;
uniform vec2 u_wind;

out vec3 v_plane;
out vec3 v_world;
flat out vec3 v_view;
flat out vec3 v_scale;
flat out vec2 v_turn;
flat out float v_tint;
flat out ivec2 v_view_index;  // azimuth and elevation of the grid view

void main() {
    float c = cos(a_instance_turn.x);
    float s = sin(a_instance_turn.x);
    vec3 to_eye = u_eye - a_instance_offset;
    vec3 eye_unit = vec3(c * to_eye.x + s * to_eye.z, to_eye.y, -s * to_eye.x + c * to_eye.z) / a_instance_scale;
    vec3 view = normalize(eye_unit - u_box_centre);
    vec3 right;
    vec3 up;
    tree_view_axes(view, right, up);
    vec3 reach = tree_view_reach(view, right, up);
    vec3 plane = u_box_centre + right * (a_corner.x * reach.x) + up * (a_corner.y * reach.y);
    vec3 world = tree_world_position(plane, a_instance_scale, a_instance_offset, a_instance_turn.x, u_time, u_wind);
    gl_Position = u_view_proj * vec4(world, 1.0);
    v_plane = plane;
    v_world = world;
    v_view = view;
    v_scale = a_instance_scale;
    v_turn = vec2(c, s);
    v_tint = a_instance_turn.y;

    // The grid view nearest the eye's direction.
    float turns = atan(view.z, view.x) * 0.15915494 * float(u_azimuths);
    float elevation = asin(clamp(view.y, -1.0, 1.0));
    int nearest = 0;
    for (int k = 1; k < u_elevations; ++k) {
        nearest = abs(elevation - u_elevation_table[k].z) < abs(elevation - u_elevation_table[nearest].z) ? k : nearest;
    }
    v_view_index = ivec2(int(floor(turns + 0.5) + float(u_azimuths)) % u_azimuths, nearest);
}
