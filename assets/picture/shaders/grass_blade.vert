#version 330 core

#include "lighting.glsl"
#include "noise.glsl"

// One blade of a tuft of the rough round the eye (renderer/grass_renderer.h),
// in one ring. Each instance is a cell of a block of the ring's world-fixed
// grid; its tuft stands at a random spot in it on the ground map, as tall
// as the grass there, and is there or not by the ring's thinning. A blade
// leans its own way, bends further with the wind and its gusts, and tapers
// to its tip.

layout(location = 0) in vec3 a_blade;  // x: side across (-1, 1, 0 at the tip); y: share of its height; z: which blade

uniform mat4 u_view_proj;
uniform sampler2D u_field;  // r: height over the base; g: the blades, 1 rough, 2 heavy rough, 0 none
uniform vec4 u_window;      // xy: the map's corner; z: its side; w: its base height
uniform vec2 u_grid;        // x: the cells' spacing; y: which ring
uniform vec4 u_blocks[64];  // the blocks' corners of least x and z, two to each
// The tufts thicken in from x to y metres from the eye and thin out from z
// to w.
uniform vec4 u_thinning;
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

const float tau = 6.2831853;
const int block_tufts = 24;  // grass_block_tufts
// The wind at which a blade bends fully over its sway.
const float bending_wind = 8.0;

void main() {
    int block = gl_InstanceID / (block_tufts * block_tufts);
    int tuft = gl_InstanceID % (block_tufts * block_tufts);
    vec4 pair = u_blocks[block / 2];
    vec2 block_corner = block % 2 == 0 ? pair.xy : pair.zw;
    vec2 cell = block_corner + vec2(float(tuft % block_tufts), float(tuft / block_tufts)) * u_grid.x;
    vec2 tuft_seed = floor(cell / u_grid.x + 0.5) + 41.3 * u_grid.y;
    vec2 seed = tuft_seed + 0.37 * a_blade.z;
    vec2 root = cell + vec2(hash12(seed), hash12(seed + 17.3)) * u_grid.x;
    // Thinned as a whole tuft, by its cell's middle, the same in both rings
    // where one thins into the next.
    float from_eye = distance(cell + 0.5 * u_grid.x, haze_eye.xz);
    float keep = hash12(tuft_seed + 23.9);
    float kept = step(smoothstep(u_thinning.z, u_thinning.w, from_eye), keep) *
        step(keep, smoothstep(u_thinning.x, u_thinning.y, from_eye));
    vec2 uv = (root - u_window.xy) / u_window.z;
    // The four texels round the root, blended by hand: whether grass grows is
    // their blend (where grass meets bare ground, half and half is the edge),
    // but its height only the grassy texels', so a blade beside a path, a tee
    // or a bunker stands on the turf, never partway up to them.
    ivec2 texels = textureSize(u_field, 0);
    vec2 at = uv * vec2(texels) - 0.5;
    ivec2 corner = ivec2(floor(at));
    vec2 f = at - vec2(corner);
    float blades = 0.0;
    float ground = 0.0;
    float grassy = 0.0;
    for (int i = 0; i < 4; ++i) {
        ivec2 offset = ivec2(i & 1, i >> 1);
        vec4 texel = texelFetch(u_field, clamp(corner + offset, ivec2(0), texels - 1), 0);
        float weight = mix(1.0 - f.x, f.x, float(offset.x)) * mix(1.0 - f.y, f.y, float(offset.y));
        float grows = weight * step(0.5, texel.g);
        blades += weight * texel.g;
        ground += grows * texel.r;
        grassy += grows;
    }
    ground /= max(grassy, 1.0e-6);
    float on_map = step(0.0, uv.x) * step(uv.x, 1.0) * step(0.0, uv.y) * step(uv.y, 1.0);
    float grass = blades < 0.5 ? 0.0 : blades <= 1.0 ? 1.0 : mix(1.0, u_heavy_share, blades - 1.0);
    float height = u_blade.x * grass * kept * on_map * (0.6 + 0.8 * hash12(seed + 9.7));
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
    lean *= min(1.0, 0.85 / max(length(lean), 1.0e-4));
    float up = a_blade.y;
    // Bent along a curve: the lean grows with the square of the way up.
    vec2 bend = lean * up * up;
    vec2 sideways = across * (a_blade.x * 0.5 * u_blade.y * (1.0 - 0.6 * up)) + bend * height;
    vec3 world = vec3(root.x + sideways.x, ground + u_window.w + height * up * sqrt(max(1.0 - dot(bend, bend), 0.1)),
                      root.y + sideways.y);
    gl_Position = u_view_proj * vec4(world, 1.0);
    // A tuft is lit much like the turf it stands in: mostly towards the sky,
    // a little its own way and back from its bend.
    v_normal = normalize(vec3(facing.x, 0.0, facing.y) * 0.35 + vec3(-bend.x, 1.0, -bend.y));
    v_world = world;
    v_shadow = (u_shadow_view_proj * vec4(world, 1.0)).xyz * 0.5 + 0.5;
    v_up = up;
    v_tint = 0.85 + 0.3 * hash12(seed + 11.1);
}
