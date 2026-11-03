// The scene's light and haze this frame: the one home of both. Every scene
// shader pulls this in with `#include "lighting.glsl"`; the renderer fills
// the block once per frame (renderer/lighting_uniforms.h) from the palette
// (assets/visuals/lighting.json).
//
//   lit:   albedo * (hemisphere(N) * ao + sun_color * wrap(N.L) * sun_shadow + local_light)
//   unlit: albedo * (max(sky_light + sun_color, glow) + local_light)
//   then:  hazed with distance toward haze_color, lit by the sun looking
//          towards it, plus the night lights' light the air between
//          scatters towards the eye.

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
    // x: the share of the light it meets the air scatters, per metre; y: how
    // much of that keeps on forwards (Henyey-Greenstein's g); z: the share of
    // a light's reach out to which the air takes the light a surface does,
    // more nearer and less further by the inverse square; w: the size of the
    // lamp's housing, over which that softens, in metres (air_reaching).
    vec4 scatter;
    // x: how much of the sun's light the haze sends towards the eye, at
    // Henyey-Greenstein's average; y: how much keeps on forwards (its g);
    // z: the mist, the haze's growth with distance from its square (0)
    // towards straight (1).
    vec4 air;
    // The mist lying low: x the world height it lies on, the area's lowest
    // ground; y how fast it thins upwards, 1 / the metres it takes to thin
    // by e; z the share of the light it takes out per metre on that ground.
    vec4 mist;
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

// Light `i` reaching the air at `world` (or a drop of rain in it), as a real
// lamp's: by the inverse square of the distance, matching a surface's
// light_reaching at scatter.z of its reach, softened over a housing scatter.w
// across so it never peaks to a point.
vec3 air_reaching(int i, vec3 world, out vec3 towards) {
    vec3 light = light_reaching(i, world, towards);
    float near = scatter.z * light_position[i].w;
    vec3 to = light_position[i].xyz - world;
    return light * (near * near / (dot(to, to) + scatter.w * scatter.w));
}

// The night lights on the air at `world`.
vec3 air_light(vec3 world) {
    vec3 sum = vec3(0.0);
    for (int i = 0; i < max_lights; ++i) {
        vec3 towards;
        sum += air_reaching(i, world, towards);
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

// The share of the light the low mist takes out per metre at `world`.
float mist_at(vec3 world) {
    return mist.z * exp((mist.x - world.y) * mist.y);
}

// How much light the low mist takes out along `way` (unit length) from the
// eye out to `meters`, no further than the haze's full distance (where the
// backdrop's land takes over): its optical depth, mist_at integrated in
// closed form, mist.z (e^-h0 - e^-h1) / (way.y mist.y) with each height h
// in e-folds over the lowest ground. Close to level the ends' difference
// cancels, so there it takes the series: level, the even mist at the eye.
// A long look down far below the lowest ground (the backdrop's land) caps
// its far end's e-folds, so the mist there is thick but never infinite.
float mist_depth(vec3 way, float meters) {
    float run = min(meters, 1.0 / haze_eye.w);
    float rise = way.y * run * mist.y;
    float from_eye = (mist.x - haze_eye.y) * mist.y;
    float level = exp(from_eye) * (1.0 - rise * (0.5 - rise / 6.0));
    float climbing = (exp(from_eye) - exp(min(from_eye - rise, 60.0))) / (abs(rise) < 0.01 ? 1.0 : rise);
    return mist.z * run * (abs(rise) < 0.01 ? level : climbing);
}

// How much of what is `meters` from the eye along `way` (unit length) the
// haze hides, 0..1: the share of the light the air between takes out and
// puts its own in place of. The low mist lets through e^-mist_depth of what
// the rest of the haze leaves.
float haze_over(vec3 way, float meters) {
    float far = min(meters * haze_eye.w, 1.0);
    float haze = haze_color.a * mix(far * far, far, air.z);
    return 1.0 - (1.0 - haze) * exp(-mist_depth(way, meters));
}

float haze_at(vec3 world) {
    vec3 to = world - haze_eye.xyz;
    float meters = length(to);
    return haze_over(to / max(meters, 0.001), meters);
}

// The sun's light the haze sends along `way` (unit length), per unit of
// haze: brightest looking towards the sun.
vec3 haze_sun_glow(vec3 way) {
    float g = air.y;
    float turn = 1.0 + g * g - 2.0 * g * dot(way, sun_direction.xyz);
    return sun_color.rgb * (air.x * (1.0 - g * g) / (turn * sqrt(turn)));
}

// The haze looking along `way` (unit length): its colour, and the sun's light
// it sends that way.
vec3 haze_toward(vec3 way) {
    return haze_color.rgb + haze_sun_glow(way);
}

// Where the line from the eye along `way` (unit length) runs through light
// `i`'s reach before `far`: (start, end), start >= end when it misses.
vec2 through_reach(int i, vec3 way, float far) {
    vec3 from_light = haze_eye.xyz - light_position[i].xyz;
    float nearest = -dot(from_light, way);
    float apart = dot(from_light, from_light) - nearest * nearest;
    float radius = light_position[i].w;
    float half_chord = sqrt(max(radius * radius - apart, 0.0));
    return vec2(max(nearest - half_chord, 0.0), min(nearest + half_chord, far));
}

bool in_cone(int i, vec3 world) {
    vec3 out_of = world - light_position[i].xyz;
    return dot(out_of, light_cone[i].xyz) >= light_cone[i].w * length(out_of);
}

// The part of `span` along `way` inside light `i`'s cone, out to its edge.
// A cone narrower than a half-space is convex, so that is one run, found
// from where the line crosses the cone's surface.
vec2 through_cone(int i, vec3 way, vec2 span) {
    vec3 axis = light_cone[i].xyz;
    float edge = light_cone[i].w;
    if (span.x >= span.y || dot(axis, axis) < 0.5 || edge <= 0.0) {
        return span;
    }
    vec3 from_light = haze_eye.xyz - light_position[i].xyz;
    float way_along = dot(way, axis);
    float from_along = dot(from_light, axis);
    float a = way_along * way_along - edge * edge;
    float b = 2.0 * (way_along * from_along - edge * edge * dot(way, from_light));
    float c = from_along * from_along - edge * edge * dot(from_light, from_light);
    float first = span.x;
    float second = span.x;
    float roots = b * b - 4.0 * a * c;
    if (abs(a) > 1.0e-6 && roots >= 0.0) {
        float root = sqrt(roots);
        float one = (-b - root) / (2.0 * a);
        float other = (-b + root) / (2.0 * a);
        first = clamp(min(one, other), span.x, span.y);
        second = clamp(max(one, other), span.x, span.y);
    } else if (abs(a) <= 1.0e-6 && abs(b) > 1.0e-6) {
        first = clamp(-c / b, span.x, span.y);
        second = first;
    }
    vec2 inside = vec2(span.y, span.x);
    vec3 cuts = vec3(span.x, first, second);
    vec3 ends = vec3(first, second, span.y);
    for (int k = 0; k < 3; ++k) {
        if (ends[k] > cuts[k] && in_cone(i, haze_eye.xyz + way * (0.5 * (cuts[k] + ends[k])))) {
            inside = vec2(min(inside.x, cuts[k]), max(inside.y, ends[k]));
        }
    }
    return inside;
}

// Three-point Gauss-Legendre.
const vec3 gauss_at = vec3(0.1127016654, 0.5, 0.8872983346);
const vec3 gauss_weight = vec3(0.2777777778, 0.4444444444, 0.2777777778);

// The run of the line from the eye along `way` out to `far` inside light
// `i`'s reach and cone, as the lamp sees it: x how far along the line it
// passes nearest the lamp, y how far from it (taking in the housing), z and
// w the angles it sees the run's ends under; z >= w when the line misses.
vec4 beam_run(int i, vec3 way, float far) {
    vec2 run = through_cone(i, way, through_reach(i, way, far));
    vec3 from_light = haze_eye.xyz - light_position[i].xyz;
    float nearest = -dot(from_light, way);
    float apart = sqrt(max(dot(from_light, from_light) - nearest * nearest, 0.0) + scatter.w * scatter.w);
    if (run.y <= run.x) {
        return vec4(nearest, apart, 0.0, 0.0);
    }
    return vec4(nearest, apart, atan((run.x - nearest) / apart), atan((run.y - nearest) / apart));
}

// The point `along` (0..1) of the way through `beam`'s angles.
vec3 beam_point(vec4 beam, vec3 way, float along) {
    return haze_eye.xyz + way * (beam.x + beam.y * tan(mix(beam.z, beam.w, along)));
}

// Light `i` the air at `point` sends along `way` (Henyey-Greenstein,
// scatter.y), as much as the haze lets through: the share it scatters per
// metre, the air's (scatter.x) and the low mist's there.
vec3 beam_air(int i, vec3 point, vec3 way) {
    vec3 towards;
    vec3 light = light_reaching(i, point, towards);
    float g = scatter.y;
    float turn = 1.0 + g * g - 2.0 * g * dot(towards, way);
    return light * ((scatter.x + mist_at(point)) * (1.0 - g * g) / (4.0 * turn * sqrt(turn)) * (1.0 - haze_at(point)));
}

// Light `i`'s light the air along `way` out to `far` scatters towards the
// eye: air_reaching through the run inside its reach and cone, times the
// share the air scatters (beam_air). Lit by a light, a surface takes albedo
// times its light as light_reaching gives it; the air takes pi times its
// phase of it. The inverse square peaks where the line passes the lamp, so
// the run is sampled evenly in the angle the lamp sees it under, where that
// peak is flat: the integral of near² / (apart² + s²) ds is near² / apart
// times the angle, apart taking in the housing.
vec3 scattered_from(int i, vec3 way, float far) {
    vec4 beam = beam_run(i, way, far);
    if (beam.w <= beam.z) {
        return vec3(0.0);
    }
    vec3 along = vec3(0.0);
    for (int k = 0; k < 3; ++k) {
        along += beam_air(i, beam_point(beam, way, gauss_at[k]), way) * gauss_weight[k];
    }
    float near = scatter.z * light_position[i].w;
    return along * (near * near * (beam.w - beam.z) / beam.y);
}

// The night lights' light the air between the eye and `world` scatters
// towards the eye.
vec3 scattered_light(vec3 world) {
    vec3 to = world - haze_eye.xyz;
    float far = length(to);
    vec3 way = to / max(far, 0.001);
    vec3 sum = vec3(0.0);
    for (int i = 0; i < max_lights; ++i) {
        sum += scattered_from(i, way, far);
    }
    return sum;
}

// `color` at `world` faded into the haze, without the night lights' light in
// the air: for a vertex shader whose fragment shader adds scattered_light,
// which changes too fast across a triangle to take from its corners.
vec3 faded(vec3 color, vec3 world) {
    vec3 to = world - haze_eye.xyz;
    return mix(color, haze_toward(to / max(length(to), 0.001)), haze_at(world));
}

vec3 hazed(vec3 color, vec3 world) {
    return faded(color, world) + scattered_light(world);
}
