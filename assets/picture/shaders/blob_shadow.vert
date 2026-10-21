#version 330 core

#include "lighting.glsl"

// One blob shadow per instance (render_blob_shadow): a quad on the ground's
// plane at its base, stretched away from the sun as long as its stretch
// says, faint as the sun goes.

layout(location = 0) in vec2 a_corner;  // -1..1
layout(location = 1) in vec3 a_base;
layout(location = 2) in vec3 a_normal;
layout(location = 3) in vec3 a_shape;  // stretch, radius, strength

uniform mat4 u_view_proj;

out vec2 v_corner;
out float v_strength;

// Off the ground it lies on, so it never sinks into the ground's own facets.
const float ground_clearance = 0.04;

void main() {
    vec3 n = normalize(a_normal);
    vec3 away = -sun_direction.xyz - n * dot(-sun_direction.xyz, n);
    vec3 along = length(away) > 0.0001 ? normalize(away) : normalize(cross(n, vec3(0.0, 0.0, 1.0)));
    vec3 side = normalize(cross(n, along));

    vec3 world = a_base + n * ground_clearance + (along * (a_corner.x * a_shape.x) + side * a_corner.y) * a_shape.y;
    gl_Position = u_view_proj * vec4(world, 1.0);
    v_corner = a_corner;
    v_strength = a_shape.z * mix(shadow.y, 1.0, shadow.x) * (1.0 - haze_at(world));
}
