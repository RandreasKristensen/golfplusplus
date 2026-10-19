#version 330 core

// The trees' and floodlights' depth seen from the sun
// (renderer/shadow_map_pass.h), instanced like tree_instanced.vert; the
// floodlights' mesh is one unscaled instance at the origin.

layout(location = 0) in vec3 a_pos;
layout(location = 3) in vec3 a_instance_offset;
layout(location = 4) in vec3 a_instance_scale;

uniform mat4 u_view_proj;

void main() {
    gl_Position = u_view_proj * vec4(a_instance_offset + a_pos * a_instance_scale, 1.0);
}
