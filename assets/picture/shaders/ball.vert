#version 330 core

// The ball, a sphere scaled to its drawn radius (paired with ball.frag).

layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;

uniform mat4 u_mvp;
uniform mat4 u_model;

out vec3 v_normal;
out vec3 v_world;

void main() {
    v_normal = normalize(mat3(transpose(inverse(u_model))) * a_normal);
    v_world = (u_model * vec4(a_pos, 1.0)).xyz;
    gl_Position = u_mvp * vec4(a_pos, 1.0);
}
