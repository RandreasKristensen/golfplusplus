#version 330 core

#include "lighting.glsl"
#include "noise.glsl"

// Each instance selects an actual finest-grid tuft, by its integer rank.
// Root, yaw, height and lean never depend on the eye or the ring.

layout(location = 0) in vec3 a_blade;  // x: side across (-1, 1, 0 at the tip); y: share of its height; z: which blade

uniform mat4 u_view_proj;
uniform sampler2D u_field;  // r height, g distance to bare ground, b kind
uniform vec4 u_window;      // xy field origin, z spacing, w interpolation error
uniform vec2 u_grid;        // x: the cells' spacing; y: which ring
uniform vec4 u_blocks[64];  // the blocks' corners of least x and z, two to each
uniform int u_last_ring;
uniform vec2 u_density; // base reach, this level reach
uniform vec4 u_blade;       // x: height; y: width at the root; w: sway
uniform float u_heavy_share;  // heavy rough's blades over rough's
uniform vec2 u_wind;        // m/s, x and z
uniform float u_time;
uniform mat4 u_shadow_view_proj;

out vec3 v_world;
out vec3 v_normal;
out vec3 v_shadow;
out float v_up;
flat out float v_tint;
#ifdef GRASS_DEBUG_ROOTS
flat out vec3 v_root;
#endif
#ifdef GRASS_DEBUG_RINGS
flat out int v_ring;
#endif

const float tau = 6.2831853;
const int block_tufts = 24;  // grass_block_tufts
// The wind at which a blade bends fully over its sway.
const float bending_wind = 8.0;

uint rank_cell(ivec2 cell) {
    uint h = uint(cell.x) * 0x8da6b343u ^ uint(cell.y) * 0xd8163841u;
    h = (h ^ (h >> 13)) * 0x5bd1e995u;
    return h ^ (h >> 15);
}

ivec2 representative(ivec2 block, int side) {
    ivec2 origin = block * side;
    uint h = rank_cell(ivec2(floor(vec2(origin) / 8.0)));
    uint mask = uint(side - 1);
    return origin + ivec2(h & mask, (h >> 3) & mask);
}

void main() {
#ifdef GRASS_DEBUG_RINGS
    v_ring = int(u_grid.y);
#endif
    int block = gl_InstanceID / (block_tufts * block_tufts);
    int tuft = gl_InstanceID % (block_tufts * block_tufts);
    vec4 pair = u_blocks[block / 2];
    vec2 block_corner = block % 2 == 0 ? pair.xy : pair.zw;
    vec2 cell = block_corner + vec2(float(tuft % block_tufts), float(tuft / block_tufts)) * u_grid.x;
    int level = int(u_grid.y);
    int scale = 1 << level;
    ivec2 block_cell = ivec2(floor(cell / u_grid.x + 0.5));
    ivec2 fine = representative(block_cell, scale);
    // A parent representative is drawn only by its highest owning level,
    // even nearby: no duplicate tuft in overlapping rings.
    bool owned = level == u_last_ring || any(notEqual(fine, representative(ivec2(floor(vec2(block_cell) / 2.0)), scale * 2)));
    vec2 tuft_seed = vec2(fine);
    vec2 seed = tuft_seed + 0.37 * a_blade.z;
    float spacing = u_grid.x / float(scale);
    vec2 root = (vec2(fine) + vec2(hash12(seed), hash12(seed + 17.3))) * spacing;
    float from_eye = distance(root, haze_eye.xz);
    // Width, including the thinning fade, is continuous across every boundary.
    float width_scale = clamp(from_eye / u_density.x, 1.0, exp2(float(u_last_ring)));
    float coverage = 1.0 - smoothstep(0.55 * u_density.y, u_density.y, from_eye);
    vec2 at = (root - u_window.xy) / u_window.z;
    vec2 size = vec2(textureSize(u_field, 0));
    vec3 field = texture(u_field, (at + 0.5) / size).rgb;
    float reach = 0.85 * u_blade.x * max(1.0, u_heavy_share) * 1.4 +
        0.5 * u_blade.y * exp2(float(u_last_ring));
    bool grows = owned && field.g >= reach + u_window.w && all(greaterThanEqual(at, vec2(0))) &&
        all(lessThan(at, size - 1.0));
    float grass = mix(1.0, u_heavy_share, clamp(field.b - 1.0, 0.0, 1.0));
    float ground = field.r;
    float height = grows && coverage > 0.0 ? u_blade.x * grass * (0.6 + 0.8 * hash12(seed + 9.7)) : 0.0;
    if (height < 0.005) {
        // Nothing grows here: a point off the far plane.
        gl_Position = vec4(0.0, 0.0, 2.0, 1.0);
        return;
    }
    float yaw = tau * hash12(seed + 5.1);
    vec2 facing = vec2(cos(yaw), sin(yaw));
    vec2 across = vec2(-facing.y, facing.x);
    float gust = value_noise((root - 0.8 * u_wind * u_time) / 3.0);
    float flutter = 0.15 * sin(u_time * (3.0 + 2.0 * hash12(seed + 2.3)) + tau * hash12(seed + 4.4));
    vec2 lean = facing * (0.15 + 0.2 * hash12(seed + 6.6)) +
        u_wind / bending_wind * u_blade.w * (0.5 + 0.5 * gust + flutter);
    // grass_blade_lean_share.
    lean *= min(1.0, 0.85 / max(length(lean), 1.0e-4));
    float up = a_blade.y;
    // Bent along a curve: the lean grows with the square of the way up.
    vec2 bend = lean * up * up;
    vec2 sideways = across * (a_blade.x * 0.5 * u_blade.y * width_scale * coverage * (1.0 - 0.6 * up)) + bend * height;
    vec3 world = vec3(root.x + sideways.x, ground + height * up * sqrt(max(1.0 - dot(bend, bend), 0.1)),
                      root.y + sideways.y);
    gl_Position = u_view_proj * vec4(world, 1.0);
#ifdef GRASS_DEBUG_ROOTS
    v_root = vec3(root.x, ground, root.y);
    // Disjoint markers within the finest cell prevent one root hiding
    // another in the dense reference picture. Their data is the actual root.
    vec2 marker = (vec2(fine) + vec2(0.2 + 0.3 * mod(a_blade.z,3.0),
                                    0.25 + 0.5 * floor(a_blade.z/3.0))) * spacing;
    gl_Position = u_view_proj * vec4(marker.x + spacing * 0.05 * a_blade.x, ground,
                                    marker.y + spacing * 0.12 * a_blade.y, 1.0);
#endif
    // A tuft is lit much like the turf it stands in: mostly towards the sky,
    // a little its own way and back from its bend.
    v_normal = normalize(vec3(facing.x, 0.0, facing.y) * 0.35 + vec3(-bend.x, 1.0, -bend.y));
    v_world = world;
    v_shadow = (u_shadow_view_proj * vec4(world, 1.0)).xyz * 0.5 + 0.5;
    v_up = up;
    v_tint = 0.85 + 0.3 * hash12(seed + 11.1);
}
