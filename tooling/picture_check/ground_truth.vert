#version 330 core
layout(location=0) in vec3 a_pos;
layout(location=9) in float a_blades;
uniform mat4 u_mvp;
out float v_blades;
void main() { gl_Position=u_mvp*vec4(a_pos,1); v_blades=a_blades; }
