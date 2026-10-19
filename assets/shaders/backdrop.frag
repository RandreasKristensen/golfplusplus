#version 330 core

#include "lighting.glsl"

// The course's backdrop behind everything: each pixel looks up the panoramas
// in the direction it sees, so the horizon stays put as the camera turns. The
// sky goes down first, then the land over it, the land fading into the haze
// colour as far as its alpha says. Both follow the time of day: the sky gives
// way to one shading from the haze up to the palette's zenith (keeping the
// panorama's clouds as brightness), glowing along the horizon towards the
// sun, the clouds catching the low sun's colour, with the sun, the moon and
// the stars in it; the land is as bright as the light there is. Everything
// in the sky is sized in angles.

in vec2 v_ndc;

out vec4 frag_color;

// The camera's rotation and projection, inverted (no translation: the
// panoramas are infinitely far away).
uniform mat4 u_inverse_view_proj;
uniform sampler2D u_sky;
// The sky panorama's mean brightness.
uniform float u_sky_brightness;
// RGB is the land's own colour, alpha how much of it shows through the haze:
// 0 where the sky shows, otherwise at least min_land_visibility.
uniform sampler2D u_land;
// Scales the drawn haze: 1 as drawn, 0 clear.
uniform float u_haze_amount;
uniform float u_seconds;

// The palette's sky_style (game/lighting_loader.h), angles in radians.
uniform float u_sun_radius;
uniform float u_sun_brightness;
uniform float u_sun_halo_radius;
uniform float u_sun_halo_strength;
uniform float u_moon_radius;
uniform vec3 u_moon_color;
uniform float u_moon_earthshine;
uniform float u_glow_width;
uniform float u_glow_height;
uniform float u_star_spacing;
uniform float u_star_density;
uniform float u_star_radius;
uniform float u_star_brightness;
uniform float u_star_twinkle;
uniform float u_star_twinkle_hz;
uniform float u_star_horizon_fade;
uniform float u_cloud_threshold;
uniform float u_cloud_softness;
uniform float u_cloud_sun_gain;
uniform float u_cloud_sun_width;
uniform vec3 u_cloud_shade;

// The pictures wrap once around the horizon and span these elevations from
// their bottom row to their top row; land never shows less than this through
// the haze. Keep in step with tooling/art/make_art.py.
const float bottom_elevation = radians(-30.0);
const float top_elevation = radians(60.0);
const float min_land_visibility = 0.15;
const float full_turn = 6.28318530718;
const float quarter_turn = 1.57079632679;
// How soft the moon's terminator is, as a share of its radius.
const float terminator_softness = 0.08;
// Stars sit this far inside their cell, as a share of it, so none is cut by its edge.
const float star_margin = 0.25;

float brightness(vec3 color) {
    return dot(color, vec3(0.2126, 0.7152, 0.0722));
}

// The angle between two unit vectors, exact for small angles.
float angle_between(vec3 a, vec3 b) {
    return 2.0 * asin(min(length(a - b) * 0.5, 1.0));
}

// How much of a pixel `pixel` wide lies inside a disc of `radius` whose
// centre is `angle` away.
float disc_cover(float angle, float radius, float pixel) {
    return clamp((radius - angle) / pixel + 0.5, 0.0, 1.0);
}

vec4 hash4(vec3 cell) {
    vec4 p = fract(vec4(cell.xyzx) * vec4(0.1031, 0.1030, 0.0973, 0.1099));
    p += dot(p, p.wzxy + 33.33);
    return fract((p.xxyz + p.yzzw) * p.zywx);
}

// The glow along the horizon towards the sun, 0..1.
float horizon_glow_at(vec3 direction, float elevation) {
    vec2 sun_flat = sun_direction.xz;
    vec2 view_flat = direction.xz;
    float around = length(sun_flat) > 0.0001 && length(view_flat) > 0.0001
        ? acos(clamp(dot(normalize(view_flat), normalize(sun_flat)), -1.0, 1.0))
        : 0.0;
    float sideways = 1.0 - smoothstep(0.0, u_glow_width, around);
    float upward = 1.0 - smoothstep(0.0, u_glow_height, max(elevation, 0.0));
    return sideways * sideways * upward * horizon_glow.a;
}

// Clouds (`clouds`: the panorama's brightness over its mean) lit by the low
// sun as far as the horizon glow shows: warm in the glow's colour towards
// the sun, shaded blue-grey away from it.
vec3 light_clouds(vec3 sky, vec3 direction, float clouds) {
    float cloud = smoothstep(u_cloud_threshold, u_cloud_threshold + u_cloud_softness, clouds) * horizon_glow.a;
    vec3 glow = horizon_glow.rgb / max(max(horizon_glow.r, horizon_glow.g), max(horizon_glow.b, 0.01));
    float towards = 1.0 - smoothstep(0.0, u_cloud_sun_width, angle_between(direction, sun_direction.xyz));
    vec3 tint = mix(u_cloud_shade, glow * u_cloud_sun_gain, towards * towards);
    return mix(sky, sky * tint, cloud);
}

// The sun's disc, far brighter than white in the glare's colour for the
// camcorder to clip, bloom and smear, blooming white past its edge (in sun
// radii) and glaring in the glow's colour further out.
vec3 add_sun(vec3 sky, vec3 direction, float elevation, float pixel) {
    float angle = angle_between(direction, sun_direction.xyz);
    float above = clamp(elevation / pixel + 0.5, 0.0, 1.0);
    float halo = max(1.0 - angle / u_sun_halo_radius, 0.0);
    vec3 glare = mix(vec3(1.0), horizon_glow.rgb, horizon_glow.a);
    sky = mix(sky, glare, halo * halo * halo * u_sun_halo_strength * above);
    float bloom = exp(-max(angle - u_sun_radius, 0.0) / u_sun_radius) * u_sun_halo_strength;
    sky = mix(sky, vec3(1.0), bloom * above);
    return mix(sky, glare * u_sun_brightness, disc_cover(angle, u_sun_radius, pixel) * above);
}

// The moon, lit on the side towards the sun as much as its phase says,
// covering what is behind it as far as the night has come.
vec3 add_moon(vec3 sky, vec3 direction, float elevation, float pixel) {
    vec3 moon = moon_direction.xyz;
    float cover = disc_cover(angle_between(direction, moon), u_moon_radius, pixel) * clamp(elevation / pixel + 0.5, 0.0, 1.0);
    if (cover <= 0.0) {
        return sky;
    }
    vec3 towards_sun = sun_direction.xyz - moon * dot(sun_direction.xyz, moon);
    towards_sun = length(towards_sun) > 0.0001 ? normalize(towards_sun) : normalize(cross(moon, vec3(0.0, 0.0, 1.0)));
    vec3 across = cross(moon, towards_sun);
    vec3 offset = direction - moon * dot(direction, moon);
    vec2 disc = vec2(dot(offset, towards_sun), dot(offset, across)) / sin(u_moon_radius);
    float terminator = (1.0 - 2.0 * moon_direction.w) * sqrt(max(1.0 - disc.y * disc.y, 0.0));
    float lit = smoothstep(-terminator_softness, terminator_softness, disc.x - terminator);
    vec3 light = u_moon_color * mix(u_moon_earthshine, 1.0, lit);
    return sky * (1.0 - cover * sun_direction.w) + light * cover;
}

// Stars at random in cells of an equal-angle cube around the camera, at
// least a pixel across so the tape's blur cannot lose them.
vec3 add_stars(vec3 sky, vec3 direction, float elevation, float pixel, float clear) {
    float shown = sun_direction.w * smoothstep(0.0, u_star_horizon_fade, elevation) * clear;
    if (shown <= 0.0) {
        return sky;
    }
    vec3 a = abs(direction);
    float face;
    vec3 basis;  // the face's axis and two across it
    vec2 on_face;
    if (a.x >= a.y && a.x >= a.z) {
        face = direction.x > 0.0 ? 0.0 : 1.0;
        on_face = direction.yz / a.x;
        basis = vec3(sign(direction.x), 0.0, 0.0);
    } else if (a.y >= a.z) {
        face = direction.y > 0.0 ? 2.0 : 3.0;
        on_face = direction.xz / a.y;
        basis = vec3(0.0, sign(direction.y), 0.0);
    } else {
        face = direction.z > 0.0 ? 4.0 : 5.0;
        on_face = direction.xy / a.z;
        basis = vec3(0.0, 0.0, sign(direction.z));
    }
    // Equal angles across the face, a quarter turn from edge to edge.
    vec2 angles = atan(on_face);
    vec2 cell = floor(angles / u_star_spacing);
    vec4 random = hash4(vec3(cell, face));
    if (random.x >= u_star_density) {
        return sky;
    }
    vec2 star_angles = (cell + star_margin + (1.0 - 2.0 * star_margin) * random.yz) * u_star_spacing;
    if (any(greaterThan(abs(star_angles), vec2(quarter_turn * 0.5)))) {
        return sky;
    }
    vec2 star_on_face = tan(star_angles);
    vec3 star = basis.x != 0.0 ? vec3(basis.x, star_on_face.x, star_on_face.y)
              : basis.y != 0.0 ? vec3(star_on_face.x, basis.y, star_on_face.y)
                               : vec3(star_on_face.x, star_on_face.y, basis.z);
    float angle = angle_between(direction, normalize(star));
    float radius = max(u_star_radius, 0.6 * pixel);
    float core = clamp((radius + 0.5 * pixel - angle) / pixel, 0.0, 1.0);
    float twinkle = 1.0 - u_star_twinkle * (0.5 + 0.5 * sin(full_turn * (u_star_twinkle_hz * u_seconds + random.w)));
    float magnitude = mix(0.3, 1.0, random.w * random.w * random.w);
    vec3 tint = mix(vec3(0.80, 0.86, 1.0), vec3(1.0, 0.92, 0.78), random.y);
    return sky + tint * (core * magnitude * twinkle * u_star_brightness * shown);
}

void main() {
    vec4 far_point = u_inverse_view_proj * vec4(v_ndc, 1.0, 1.0);
    vec3 direction = normalize(far_point.xyz / far_point.w);
    float pixel = max(length(fwidth(direction)), 0.00001);
    float elevation = asin(clamp(direction.y, -1.0, 1.0));
    float u = atan(direction.x, -direction.z) / full_turn + 0.5;
    float v = (elevation - bottom_elevation) / (top_elevation - bottom_elevation);
    vec3 drawn_sky = texture(u_sky, vec2(u, v)).rgb;
    vec4 land = texture(u_land, vec2(u, v));

    float clouds = brightness(drawn_sky) / max(u_sky_brightness, 0.01);
    vec3 gradient = mix(haze_color.rgb, sky_zenith.rgb, sqrt(clamp(elevation / top_elevation, 0.0, 1.0)));
    vec3 clouded = min(gradient * clouds, vec3(1.0));
    vec3 sky = mix(drawn_sky, clouded, sky_zenith.a);
    sky = light_clouds(sky, direction, clouds);
    sky = mix(sky, horizon_glow.rgb, horizon_glow_at(direction, elevation));
    sky = add_stars(sky, direction, elevation, pixel, clamp(1.5 - clouds, 0.0, 1.0));
    sky = add_moon(sky, direction, elevation, pixel);
    sky = add_sun(min(sky, vec3(1.0)), direction, elevation, pixel);

    // Any land at all covers the sky; what of it the haze hides shows the
    // haze colour instead.
    float coverage = clamp(land.a / min_land_visibility, 0.0, 1.0);
    float haze = min((coverage - land.a) * u_haze_amount, coverage);
    vec3 color = sky * (1.0 - coverage) + haze_color.rgb * haze + land.rgb * flat_light(0.0) * (coverage - haze);
    frag_color = vec4(color, 1.0);
}
