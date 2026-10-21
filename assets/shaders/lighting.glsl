// The scene's light and haze this frame: the one home of both. Every scene
// shader pulls this in with `#include "lighting.glsl"`; the renderer fills
// the block once per frame (renderer/lighting_uniforms.h) from the palette
// (assets/visuals/lighting.json).
//
//   lit:   albedo * (hemisphere(N) * ao + sun_color * wrap(N.L) * sun_shadow + local_light)
//   unlit: albedo * (max(sky_light + sun_color, glow) + local_light)
//   then:  hazed toward haze_color with distance.

const int max_lights = 16;  // renderer/scene_lights.h's max_scene_lights

layout(std140) uniform frame_lighting {
    vec4 sun_direction;  // xyz: world space, towards the sun; w: how much the stars show
    vec4 sun_color;      // rgb: the light along the sun
    vec4 sky_light;      // rgb: the light from the sky above
    vec4 ground_light;   // rgb: the light bounced up from the ground
    vec4 sky_zenith;     // rgb: the sky overhead; a: how far it replaces the sky panorama
    vec4 haze_color;     // rgb: what far things fade into; a: haze at full distance
    vec4 haze_eye;       // xyz: the camera; w: 1 / the distance haze is full at
    vec4 moon_direction; // xyz: world space, towards the moon; w: how much of it is lit, 0 new .. 1 full
    vec4 horizon_glow;   // rgb: the sky's glow along the horizon towards the sun; a: its strength
    vec4 sun_horizon_a;  // how much horizon ways 0..3 count towards the sun (renderer/sky_visibility.h)
    vec4 sun_horizon_b;  // and ways 4..7
    // x: how much of their shadow the sun casts; y: a blob shadow's strength
    // with no sun; z: unused; w: the softness of the land's shadow edge, as
    // a sine of the sun's elevation.
    vec4 shadow;
    vec4 lamp_glow;  // rgb: a floodlight lamp's face, past 1 when on, none by day
    // x: how much of the sky panorama's cloud contrast the overcast keeps
    // (negative: rain's dark patches where its clouds are bright); y: a sun
    // shower's rainbow, 0..1; z: the sky's cover by cloud, 0..1; w: how far
    // the weather thickens the haze, 0..1.
    vec4 weather;
    // x: the ground's colour kept wet; y: its wet shine; z: how sharp the shine is.
    vec4 wet;
    vec4 light_position[max_lights];  // xyz: world space; w: how far it reaches, 0 for no light
    vec4 light_color[max_lights];     // rgb: its light, past 1 near it; w: cos of where its cone is full
    vec4 light_cone[max_lights];      // xyz: the way it shines, 0 for every way; w: cos of its cone's edge
};

// How far round the sun's light wraps past the line where a surface turns
// away from it, so the shaded side is never cut off hard.
const float sun_wrap = 0.25;

vec3 hemisphere_light(vec3 normal) {
    return mix(ground_light.rgb, sky_light.rgb, normal.y * 0.5 + 0.5);
}

// How much sun reaches ground whose horizon is `horizon_a`, `horizon_b`
// (render_terrain_vertex::horizon): 0 behind the land, 1 in the open, soft
// at the edge.
float sun_shadow(vec4 horizon_a, vec4 horizon_b) {
    float horizon = dot(horizon_a, sun_horizon_a) + dot(horizon_b, sun_horizon_b);
    return smoothstep(horizon - shadow.w, horizon + shadow.w, sun_direction.y);
}

// The light on a surface facing `normal` that sees `ao` (0..1) of the sky
// and `sun` (0..1) of the sun.
vec3 surface_light(vec3 normal, float ao, float sun) {
    vec3 n = normalize(normal);
    float wrapped = max((dot(n, sun_direction.xyz) + sun_wrap) / (1.0 + sun_wrap), 0.0);
    return hemisphere_light(n) * ao + sun_color.rgb * wrapped * sun;
}

vec3 surface_light(vec3 normal, float ao) {
    return surface_light(normal, ao, 1.0);
}

// The same for a thin sheet seen from either side: it is lit on whichever
// side faces the sun.
vec3 two_sided_light(vec3 normal) {
    vec3 n = normalize(normal);
    return surface_light(dot(n, sun_direction.xyz) < 0.0 ? -n : n, 1.0);
}

// Light `i` reaching `world`, before which way the surface faces; `towards`
// is the way to it.
vec3 light_reaching(int i, vec3 world, out vec3 towards) {
    vec3 to = light_position[i].xyz - world;
    float distance_to = length(to);
    towards = to / max(distance_to, 0.001);
    float reach = clamp(1.0 - distance_to * distance_to / max(light_position[i].w * light_position[i].w, 0.0001), 0.0, 1.0);
    float cone = smoothstep(light_cone[i].w, light_color[i].w, dot(-towards, light_cone[i].xyz));
    return light_color[i].rgb * reach * reach * cone;
}

// The night lights on a surface at `world` facing `normal`.
vec3 local_light(vec3 world, vec3 normal) {
    vec3 n = normalize(normal);
    vec3 sum = vec3(0.0);
    for (int i = 0; i < max_lights; ++i) {
        vec3 towards;
        vec3 light = light_reaching(i, world, towards);
        sum += light * max((dot(n, towards) + sun_wrap) / (1.0 + sun_wrap), 0.0);
    }
    return sum;
}

// The night lights on an unlit thing at `world`, whichever way it faces.
vec3 local_light(vec3 world) {
    vec3 sum = vec3(0.0);
    for (int i = 0; i < max_lights; ++i) {
        vec3 towards;
        sum += light_reaching(i, world, towards);
    }
    return sum;
}

// What lights an unlit thing that sees `sun` (0..1) of the sun: all the
// light there is, never less than its `glow`, so the ball and flag stay
// readable in the dark; past 1 it glows of itself and blooms.
vec3 flat_light(float glow, float sun) {
    return max(min(sky_light.rgb + sun_color.rgb * sun, vec3(1.0)), vec3(glow));
}

vec3 flat_light(float glow) {
    return flat_light(glow, 1.0);
}

// The shine of wet ground at `world` facing `normal`: the sky's light at a
// glancing angle, and the sun's and the night lights' glints off it.
vec3 wet_gloss(vec3 world, vec3 normal, float sun) {
    vec3 n = normalize(normal);
    vec3 view = normalize(haze_eye.xyz - world);
    float facing = max(dot(n, view), 0.0);
    float fresnel = 0.04 + 0.96 * pow(1.0 - facing, 5.0);
    vec3 glints = sun_color.rgb * sun * pow(max(dot(reflect(-sun_direction.xyz, n), view), 0.0), wet.z);
    for (int i = 0; i < max_lights; ++i) {
        vec3 towards;
        vec3 light = light_reaching(i, world, towards);
        glints += light * pow(max(dot(reflect(-towards, n), view), 0.0), wet.z);
    }
    return wet.y * (sky_light.rgb * fresnel + glints);
}

float haze_at(vec3 world) {
    float far = min(distance(world, haze_eye.xyz) * haze_eye.w, 1.0);
    return haze_color.a * far * far;
}

vec3 hazed(vec3 color, vec3 world) {
    return mix(color, haze_color.rgb, haze_at(world));
}
