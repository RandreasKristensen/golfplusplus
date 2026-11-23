#version 330 core

#include "lighting.glsl"

// Batched world markers (ground discs, pin cups, flagsticks, aim dots, swing
// club, carts, other players). Colour and glow floor are per piece. Unlit, but
// only as bright as the light there is, and faded into the haze like
// everything else (paired with lit_flat.frag). The batch's own triangles are
// already in world space; with INSTANCED each piece is a canonical mesh placed
// by its instance (mesh_instance.glsl), the figures and balls.

#ifdef INSTANCED
#include "mesh_instance.glsl"
layout(location = 0) in vec3 a_local;
#else
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_color;
layout(location = 2) in float a_glow;
#endif

uniform mat4 u_view_proj;
// The glow floors by slot (renderer/prop_glow.h), this frame's: a glow below
// zero names one (a cached piece's, so night glow is never baked into it).
uniform vec4 u_glow_floors[2];

flat out vec4 v_color;
out vec3 v_world;

void main() {
#ifdef INSTANCED
    vec3 a_pos = instance_world(a_local);
    vec3 a_color = a_tint.rgb;
#endif
    gl_Position = u_view_proj * vec4(a_pos, 1.0);
    float glow = a_glow < 0.0 ? u_glow_floors[int(-a_glow - 1.0) / 4][int(-a_glow - 1.0) % 4] : a_glow;
    v_color = vec4(faded(a_color * (flat_light(glow) + local_light(a_pos)), a_pos), 1.0);
    v_world = a_pos;
}
