#version 330 core

// Instanced tree trunks/leaves. Paired with terrain.frag.
// Per-instance model matrix is translate(a_instance_offset) * scale(a_instance_scale).

layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;
layout(location = 3) in vec3 a_instance_offset;
layout(location = 4) in vec3 a_instance_scale;

uniform mat4 u_view_proj;

out vec3 v_normal;
out vec3 v_color;

void main() {
    vec3 world_pos = a_instance_offset + a_pos * a_instance_scale;
    gl_Position = u_view_proj * vec4(world_pos, 1.0);
    // transpose(inverse(translate * scale)) applied to a normal is normal / scale,
    // matching terrain.vert. Scale is clamped to >= 0.01 on the CPU.
    v_normal = normalize(a_normal / a_instance_scale);
    v_color = vec3(1.0);
}
