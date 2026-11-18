#version 330 core
in float v_blades;
out vec4 frag_color;
void main() { frag_color=vec4(0,max(v_blades,0),0,1); }
