// Reading the shadow map of trees, floodlights, players and carts
// (renderer/shadow_map_pass.h), for the ground and tee boxes it falls on.
// x: how much sun their shadow takes away, 0 with no map; y: its taps'
// spread, in map widths; z: its depth bias; w: the share of the map's
// half-width its edge fades over.

uniform sampler2DShadow u_shadow_map;
uniform vec4 u_shadow;

// How much sun the casters let through to `coords` (where on the map, and
// the depth there, 0..1), 0..1: four soft taps on a turned square, each
// already a 2x2 filtered test.
float shadow_map_light(vec3 coords) {
    vec2 from_middle = abs(coords.xy * 2.0 - 1.0);
    float inside = (1.0 - smoothstep(1.0 - u_shadow.w, 1.0, max(from_middle.x, from_middle.y))) *
        step(0.0, coords.z) * step(coords.z, 1.0);
    float depth = coords.z - u_shadow.z;
    float spread = u_shadow.y;
    float lit = 0.25 * (texture(u_shadow_map, vec3(coords.xy + vec2(-0.4, -1.0) * spread, depth)) +
                        texture(u_shadow_map, vec3(coords.xy + vec2(1.0, -0.4) * spread, depth)) +
                        texture(u_shadow_map, vec3(coords.xy + vec2(-1.0, 0.4) * spread, depth)) +
                        texture(u_shadow_map, vec3(coords.xy + vec2(0.4, 1.0) * spread, depth)));
    return 1.0 - u_shadow.x * inside * (1.0 - lit);
}
