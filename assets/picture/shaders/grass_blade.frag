#version 330 core

#include "lighting.glsl"
#include "shadow_map_light.glsl"

// A blade: the rough's colour from base_shade at its root to tip_shade at
// its tip, lit on whichever side faces the sun, in the shadow map's shade,
// darker low down where the tuft shades itself; hazed.

in vec3 v_world;
in vec3 v_normal;
in vec3 v_shadow;
in float v_up;
flat in float v_tint;
flat in float v_alpha;
#ifdef GRASS_DEBUG_ROOTS
flat in vec3 v_root;
#endif
#ifdef GRASS_DEBUG_RINGS
flat in int v_ring;
#endif

out vec4 frag_color;

uniform vec3 u_rough_color;
uniform vec2 u_shades;  // x: at the root; y: at the tip

void main() {
#ifdef GRASS_DEBUG_ROOTS
    frag_color = vec4(v_root, 1);
    return;
#endif
    #ifdef GRASS_DEBUG_RINGS
    frag_color = vec4(v_ring == 0 ? vec3(1,0,0) : v_ring == 1 ? vec3(0,1,0) : v_ring == 2 ? vec3(0,0,1) : vec3(1,1,0), 1);
    return;
    #endif
    vec3 n = normalize(v_normal);
    n = dot(n, sun_direction.xyz) < 0.0 ? vec3(-n.x, abs(n.y), -n.z) : n;
    float sun = shadow_map_light(v_shadow);
    vec3 light = surface_light(n, mix(0.7, 1.0, v_up), sun) + local_light(v_world, n);
    vec3 color = u_rough_color * v_tint * mix(u_shades.x, u_shades.y, v_up) * wet.x;
    frag_color = vec4(hazed(color * light, v_world), v_alpha);
}
