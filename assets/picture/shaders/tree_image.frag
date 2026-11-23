#version 330 core

#include "lighting.glsl"
#include "shadow_map_light.glsl"
#include "noise.glsl"
#include "tree_view.glsl"
#include "tree_light.glsl"

// A tree drawn as its baked images: the grid view nearest the eye's
// direction, read at the point of the picture's plane each ray crosses; the
// surface point it stored gives the depth, shadow and light. A filtered
// texel's coverage is kept by a fixed, tree-anchored hash deciding its share,
// so a tree's pattern neither crawls nor flickers. Lit by tree_lit, as the
// geometry is.

in vec3 v_plane;
in vec3 v_world;
flat in vec3 v_view;
flat in vec3 v_scale;
flat in vec2 v_turn;
flat in float v_tint;
flat in ivec2 v_view_index;

out vec4 frag_color;

uniform sampler2DArray u_albedo;
uniform sampler2DArray u_normal;
uniform sampler2DArray u_leaf;
uniform int u_first_layer;
uniform float u_shine_through;
uniform float u_cell_scale;  // hash cells per unit of the model's frame
uniform mat4 u_view_proj;
uniform mat4 u_shadow_view_proj;

uint cell_hash(uint x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

// A uniform 0..1 for a cell of the model's frame and a tree: an integer hash,
// so a filtered texel's share of coverage is kept on average exactly.
float tree_cell_threshold(ivec3 cell, float tint) {
    uint h = cell_hash(uint(cell.x) + cell_hash(uint(cell.y) + cell_hash(uint(cell.z) + floatBitsToUint(tint))));
    return (float(h >> 8) + 0.5) * (1.0 / 16777216.0);
}

void main() {
    vec3 toward = tree_grid_direction(v_view_index.x, v_view_index.y);
    vec3 right;
    vec3 up;
    tree_view_axes(toward, right, up);
    vec3 reach = tree_view_reach(toward, right, up);
    vec3 d = v_plane - u_box_centre;
    vec3 st = vec3(vec2(dot(d, right) / reach.x, dot(d, up) / reach.y) * 0.5 + 0.5,
                   float(u_first_layer + v_view_index.y * u_azimuths + v_view_index.x));
    vec4 colour = texture(u_albedo, st);
    vec4 stored = texture(u_normal, st);
    vec2 code = texture(u_leaf, st).rg;
    float cover = colour.a;
    if (any(lessThan(st.xy, vec2(0.0))) || any(greaterThan(st.xy, vec2(1.0))) || cover < 0.5 / 255.0) {
        discard;
    }
    vec3 point = u_box_centre + right * ((st.x * 2.0 - 1.0) * reach.x) + up * ((st.y * 2.0 - 1.0) * reach.y) +
        toward * ((stored.a / cover * 2.0 - 1.0) * reach.z);
    if (cover <= tree_cell_threshold(ivec3(floor(point * u_cell_scale)), v_tint)) {
        discard;
    }
    colour.rgb /= cover;
    code /= cover;
    // tree_bake.frag's leaf share and how far out: twigs and bark are shaded alike, bark at the surface.
    float kind = code.x > 0.5 ? 1.0 : 0.5;
    float depth = 1.0 - code.y;
    vec3 n = normalize(stored.rgb / cover * 2.0 - 1.0) / v_scale;
    n = normalize(vec3(v_turn.x * n.x - v_turn.y * n.z, n.y, v_turn.y * n.x + v_turn.x * n.z));
    vec3 p = (point - v_plane) * v_scale;
    vec3 world = v_world + vec3(v_turn.x * p.x - v_turn.y * p.z, p.y, v_turn.y * p.x + v_turn.x * p.z);
    vec4 clip = u_view_proj * vec4(world, 1.0);
    gl_FragDepth = clip.z / clip.w * 0.5 + 0.5;
    vec3 base = colour.rgb * (kind > 0.75 ? 0.9 + 0.2 * v_tint : 0.85 + 0.3 * v_tint);
    float sun = shadow_map_light((u_shadow_view_proj * vec4(world, 1.0)).xyz * 0.5 + 0.5);
    frag_color = vec4(tree_lit(base, n, kind, clamp(depth, 0.0, 1.0), world, sun, u_shine_through), 1.0);
}
