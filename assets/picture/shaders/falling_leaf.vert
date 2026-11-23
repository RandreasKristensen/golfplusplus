#version 330 core

#include "lighting.glsl"

// Leaves falling from a tree (renderer/falling_leaf.h, whose place_falling_leaf
// this replays): one instance per tree of tree.vert's instance buffer, six
// vertices a leaf, gl_VertexID choosing the leaf and its corner, so nothing is
// uploaded or kept per leaf. A leaf not falling, or too far to cover
// u_min_pixels, collapses to nothing.

layout(location = 3) in vec3 a_instance_offset;
layout(location = 4) in vec3 a_instance_scale;
layout(location = 5) in vec2 a_instance_turn;  // x: yaw; y: tint

uniform mat4 u_view_proj;
uniform mat4 u_shadow_view_proj;
uniform float u_time;
uniform vec2 u_wind;            // m/s, x and z
uniform vec3 u_eye;
uniform float u_focal_pixels;   // the pass's pixels per radian
uniform vec3 u_crown_centre;    // tree_crown_box, in the unit frame
uniform vec3 u_crown_half;
uniform float u_daily_share;    // the share of a full crown falling a day
// leaves per square metre of crown, size, fall speed, lie seconds
uniform vec4 u_leaf;
// flutter seconds, flutter metres, wind factor, min pixels
uniform vec4 u_flutter;

out vec3 v_world;
out vec3 v_shadow;
out vec2 v_uv;
out vec3 v_normal;
flat out float v_tint;

const float tau = 6.2831853;
const float highest_start = 1.3;
const float tumble_radians = 0.8;
const float level_meters = 0.2;
const float most_spin = 1.5;
const float lying_lift = 0.005;
const float shrinking_share = 0.3;

uint leaf_hash(uint x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

float leaf_unit(uint key, uint index, uint cycle, uint draw) {
    uint mixed = key ^ leaf_hash(index * 0x9e3779b9u + cycle * 0x85ebca6bu + draw * 0xc2b2ae35u);
    return float(leaf_hash(mixed) >> 8) * (1.0 / 16777216.0);
}

vec3 tree_point(vec3 local) {
    float c = cos(a_instance_turn.x);
    float s = sin(a_instance_turn.x);
    vec3 p = local * a_instance_scale;
    return a_instance_offset + vec3(c * p.x - s * p.z, p.y, s * p.x + c * p.z);
}

void hide() {
    gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
    v_world = vec3(0.0);
    v_shadow = vec3(0.0);
    v_uv = vec2(0.0);
    v_normal = vec3(0.0, 1.0, 0.0);
    v_tint = 0.0;
}

void main() {
    int index = gl_VertexID / 6;
    int corner = gl_VertexID - index * 6;
    float size_meters = u_leaf.y;
    float fall_speed = u_leaf.z;
    float lie_seconds = u_leaf.w;
    vec3 to_tree = a_instance_offset - u_eye;
    float reach = max(a_instance_scale.x, a_instance_scale.z) * 1.5 + a_instance_scale.y * 1.3;
    if ((length(to_tree) - reach) * u_flutter.w > size_meters * u_focal_pixels) {
        hide();
        return;
    }
    float cycle_seconds = highest_start * a_instance_scale.y / fall_speed + lie_seconds;
    float leaves = u_leaf.x * 3.14159265 * a_instance_scale.x * a_instance_scale.z;
    float per_cycle = leaves * max(u_daily_share, 0.0) / 86400.0 * cycle_seconds * (1.0 + u_flutter.z * length(u_wind));
    uint key = leaf_hash(floatBitsToUint(a_instance_offset.x) +
                         leaf_hash(floatBitsToUint(a_instance_offset.z) + leaf_hash(floatBitsToUint(a_instance_offset.y))));
    uint slot = uint(index);
    float shifted = u_time + leaf_unit(key, slot, 0u, 0u) * cycle_seconds;
    float cycle = floor(shifted / cycle_seconds);
    float age = shifted - cycle * cycle_seconds;
    uint round_ = uint(int(cycle));
    if (leaf_unit(key, slot, round_, 1u) >= clamp(per_cycle - float(index), 0.0, 1.0)) {
        hide();
        return;
    }
    float rise = 2.0 * leaf_unit(key, slot, round_, 2u) - 1.0;
    float around = tau * leaf_unit(key, slot, round_, 3u);
    float ring = sqrt(max(1.0 - rise * rise, 0.0));
    vec3 start = tree_point(u_crown_centre + vec3(ring * cos(around), rise, ring * sin(around)) * u_crown_half);
    float fall_seconds = max(start.y - a_instance_offset.y, 0.0) / fall_speed;
    if (age > fall_seconds + lie_seconds) {
        hide();
        return;
    }
    bool lying = age > fall_seconds;
    float flying = min(age, fall_seconds);
    float sway_way = tau * leaf_unit(key, slot, round_, 4u);
    float flutter = tau * leaf_unit(key, slot, round_, 5u);
    float spin = (2.0 * leaf_unit(key, slot, round_, 6u) - 1.0) * most_spin;
    float swing = sin(tau * flying / u_flutter.x + flutter);
    vec3 centre = start + vec3(cos(sway_way), 0.0, sin(sway_way)) * (u_flutter.y * swing) + vec3(u_wind.x, 0.0, u_wind.y) * flying;
    centre.y = lying ? a_instance_offset.y + lying_lift : start.y - fall_speed * flying;
    float height = centre.y - a_instance_offset.y;
    float tilt = lying ? 0.0 : tumble_radians * swing * clamp(height / level_meters, 0.0, 1.0);
    float turn = spin * flying + flutter;
    float size = 0.5 * size_meters *
        (lying ? clamp((fall_seconds + lie_seconds - age) / (shrinking_share * lie_seconds), 0.0, 1.0) : 1.0);
    vec3 level = vec3(cos(turn), 0.0, sin(turn));
    vec3 along = vec3(-sin(turn), 0.0, cos(turn)) * size;
    vec3 across = (level * cos(tilt) + vec3(0.0, 1.0, 0.0) * sin(tilt)) * size;
    centre.y = max(centre.y, a_instance_offset.y + abs(across.y));
    if (size <= 0.0) {
        hide();
        return;
    }
    // Corners: (0,0) (1,0) (1,1), (0,0) (1,1) (0,1).
    vec2 uv = corner == 0 || corner == 3 ? vec2(0.0) : (corner == 1 ? vec2(1.0, 0.0) : (corner == 5 ? vec2(0.0, 1.0) : vec2(1.0)));
    vec3 world = centre + across * (uv.x * 2.0 - 1.0) + along * (uv.y * 2.0 - 1.0);
    gl_Position = u_view_proj * vec4(world, 1.0);
    v_world = world;
    v_shadow = (u_shadow_view_proj * vec4(world, 1.0)).xyz * 0.5 + 0.5;
    v_uv = uv;
    v_normal = normalize(cross(across, along));
    v_tint = a_instance_turn.y;
}
