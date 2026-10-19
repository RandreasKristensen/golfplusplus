#version 330 core

#include "lighting.glsl"

// One blob shadow per instance (render_blob_shadow): a quad on the ground's
// plane at its base, cast along the sun from its lift, stretched along the
// sun's way the lower it is, and drawn back under its caster, round and
// faint, as the sun goes. One with no lift stays round under its caster.

layout(location = 0) in vec2 a_corner;  // -1..1
layout(location = 1) in vec3 a_base;
layout(location = 2) in vec3 a_normal;
layout(location = 3) in vec3 a_shape;  // lift, radius, strength

uniform mat4 u_view_proj;

out vec2 v_corner;
out float v_strength;

// Off the ground it lies on, so it never sinks into the ground's own facets.
const float ground_clearance = 0.04;

void main() {
    vec3 n = normalize(a_normal);
    float lowest_sine = 1.0 / shadow.z;
    vec2 sun_way = length(sun_direction.xz) > 0.0001 ? normalize(sun_direction.xz) : vec2(1.0, 0.0);
    float sine = max(sun_direction.y, lowest_sine);
    vec3 towards_sun = vec3(sun_way.x * sqrt(1.0 - sine * sine), sine, sun_way.y * sqrt(1.0 - sine * sine));
    float facing = max(dot(towards_sun, n), lowest_sine);

    vec3 caster = a_base + vec3(0.0, a_shape.x, 0.0);
    vec3 cast = caster - towards_sun * (dot(caster - a_base, n) / facing);
    vec3 center = mix(a_base, cast, shadow.x);

    vec3 away = -towards_sun - n * dot(-towards_sun, n);
    vec3 along = length(away) > 0.0001 ? normalize(away) : normalize(cross(n, vec3(0.0, 0.0, 1.0)));
    vec3 side = normalize(cross(n, along));
    float stretch = mix(1.0, min(1.0 / facing, shadow.z), a_shape.x > 0.0 ? shadow.x : 0.0);

    vec3 world = center + n * ground_clearance + (along * (a_corner.x * stretch) + side * a_corner.y) * a_shape.y;
    gl_Position = u_view_proj * vec4(world, 1.0);
    v_corner = a_corner;
    v_strength = a_shape.z * mix(shadow.y, 1.0, shadow.x) * (1.0 - haze_at(world));
}
