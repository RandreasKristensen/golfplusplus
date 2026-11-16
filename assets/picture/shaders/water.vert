#version 330 core

// Pond surfaces, already in world space, each point with its fetch (the open
// water upwind of it) for the eight kept wind directions, the k-th at
// k * 45 degrees from +X towards +Z; the wind's is between the nearest two.
// Mirrors water_fetch_toward in renderer/water_light.h.

layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec4 a_fetch_low;   // directions 0..3
layout(location = 2) in vec4 a_fetch_high;  // directions 4..7

uniform mat4 u_view_proj;
uniform vec2 u_wind;  // m/s, x and z

out vec3 v_world;
out float v_fetch;

float fetch_toward(int k) {
    return k < 4 ? a_fetch_low[k] : a_fetch_high[k - 4];
}

void main() {
    gl_Position = u_view_proj * vec4(a_pos, 1.0);
    v_world = a_pos;
    // Still air has no way (atan(0, 0) is undefined); its fetch goes unused.
    vec2 way = dot(u_wind, u_wind) > 1.0e-8 ? u_wind : vec2(1.0, 0.0);
    float slot = mod(atan(way.y, way.x) / 0.78539816, 8.0);
    int low = int(floor(slot)) % 8;
    v_fetch = mix(fetch_toward(low), fetch_toward((low + 1) % 8), fract(slot));
}
