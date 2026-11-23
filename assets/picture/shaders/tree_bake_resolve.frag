#version 330 core

// A tile of a tree's image grid: the mean of its u_scale x u_scale block of the
// finer bake (tree_bake.frag). Every channel is stored times its coverage
// (cleared to 0 where nothing was drawn), so the mean keeps each texel's real
// share covered and tree_image.frag divides it back out.

uniform sampler2D u_albedo;
uniform sampler2D u_normal;
uniform sampler2D u_leaf;
uniform int u_scale;

layout(location = 0) out vec4 o_albedo;
layout(location = 1) out vec4 o_normal;
layout(location = 2) out vec4 o_leaf;

void main() {
    ivec2 first = ivec2(gl_FragCoord.xy) * u_scale;
    vec4 albedo = vec4(0.0);
    vec4 normal = vec4(0.0);
    vec2 leaf = vec2(0.0);
    for (int y = 0; y < u_scale; ++y) {
        for (int x = 0; x < u_scale; ++x) {
            ivec2 at = first + ivec2(x, y);
            vec4 colour = texelFetch(u_albedo, at, 0);
            albedo += vec4(colour.rgb * colour.a, colour.a);
            normal += texelFetch(u_normal, at, 0) * colour.a;
            leaf += texelFetch(u_leaf, at, 0).rg * colour.a;
        }
    }
    float count = float(u_scale * u_scale);
    o_albedo = albedo / count;
    o_normal = normal / count;
    o_leaf = vec4(leaf / count, 0.0, 0.0);
}
