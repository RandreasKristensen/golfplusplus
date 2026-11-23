#version 330 core

// The sheet's texels as painted, clear where there is no ground; blended over the paper.
in vec2 v_uv;

out vec4 frag_color;

uniform sampler2D u_sheet;

void main() {
    frag_color = texture(u_sheet, v_uv);
}
