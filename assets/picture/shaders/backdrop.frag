#version 330 core

#include "lighting.glsl"

// The course's backdrop behind everything: each pixel looks up the panoramas
// in the direction it sees, so the horizon stays put as the camera turns. The
// sky goes down first, then the land over it, the land fading into the haze
// colour as far as its alpha says. Both follow the time of day: the sky gives
// way to one shading from the haze up to the palette's zenith (keeping the
// panorama's clouds as brightness), glowing along the horizon towards the
// sun, the clouds catching the low sun's colour, with the sun, the moon and
// the stars in it; the land is as bright as the light there is. Cloud cover
// greys it (the palette's key under the weather, game/weather_lighting.h),
// hiding the sun, moon and stars, mottling the panorama's clouds and
// filling the sky with a cloud deck as far as the cover goes; thick weather
// fades it into the haze; a sun shower hangs a rainbow opposite the sun;
// lightning strikes through it all. Everything in the sky is sized
// in angles.

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
// The cloud deck: cells per unit, drift per second, the threshold with no
// cover and under full cover; then its softness, its edges' brightness and
// its middles' share of that.
uniform vec4 u_cloud_deck;
uniform vec4 u_cloud_deck_look;
// A lightning strike: towards the top of its bolt and its flash (0 none);
// its seed, the bolt's brightness (0 for a flash inside the cloud), width
// and wander (radians); its colour and how much it lights the clouds within
// u_lightning_glow_radius of its top.
uniform vec4 u_lightning;
uniform vec4 u_lightning_bolt;
uniform vec4 u_lightning_glow;
uniform float u_lightning_glow_radius;
// The rainbow: the primary's radius and width, the secondary's radius and
// width; then the secondary's share, the bow's brightness in the sun's
// light, the glow inside it and the darkening between the two.
uniform vec4 u_rainbow;
uniform vec4 u_rainbow_look;

// The pictures wrap once around the horizon and span these elevations from
// their bottom row to their top row; land never shows less than this through
// the haze. Keep in step with tooling/art/make_art.py.
const float bottom_elevation = radians(-30.0);
const float top_elevation = radians(60.0);
const float min_land_visibility = 0.15;
const float full_turn = 6.28318530718;
const float quarter_turn = 1.57079632679;
// A bolt wanders in this many straight runs from its top to the horizon.
const int bolt_runs = 12;
const float bolt_zigzags = 40.0;
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

float hash12(vec2 cell) {
    vec3 p = fract(vec3(cell.xyx) * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return fract((p.x + p.y) * p.z);
}

float value_noise(vec2 at) {
    vec2 cell = floor(at);
    vec2 t = at - cell;
    t = t * t * (3.0 - 2.0 * t);
    return mix(mix(hash12(cell), hash12(cell + vec2(1.0, 0.0)), t.x),
               mix(hash12(cell + vec2(0.0, 1.0)), hash12(cell + vec2(1.0, 1.0)), t.x), t.y);
}

// The cloud deck's noise looking along `direction`, 0..1: on a ceiling one
// unit over the camera, drifting; none below the horizon.
float deck_noise(vec3 direction) {
    vec2 at = direction.xz / (max(direction.y, 0.0) + 0.1) * u_cloud_deck.x + vec2(u_cloud_deck.y * u_seconds, 0.0);
    float n = 0.5 * value_noise(at) + 0.25 * value_noise(at * 2.03 + 17.0) + 0.125 * value_noise(at * 4.01 + 31.0)
        + 0.0625 * value_noise(at * 8.05 + 47.0);
    return n / 0.9375;
}

// How much of the view along `direction` the deck covers, 0..1, and its
// colour there: bright at its edges, darker where it is thick, as bright as
// the sky overhead is, so it follows the light. It thins into the haze
// towards the horizon.
vec4 cloud_deck(vec3 direction, float elevation, float cover) {
    float threshold = mix(u_cloud_deck.z, u_cloud_deck.w, cover);
    float n = deck_noise(direction);
    float softness = u_cloud_deck_look.x;
    float amount = smoothstep(threshold, threshold + softness, n) * smoothstep(0.0, 0.12, elevation);
    float thick = smoothstep(threshold + softness, threshold + 4.0 * softness, n);
    vec3 top = vec3(brightness(sky_zenith.rgb)) * u_cloud_deck_look.y;
    vec3 color = mix(top, top * u_cloud_deck_look.z, thick);
    color = mix(haze_color.rgb, color, smoothstep(0.0, 0.5, elevation));
    return vec4(color, amount);
}

// Clouds (`cloud`: how much of the view is cloud, 0..1) lit by the low
// sun as far as the horizon glow shows: warm in the glow's colour towards
// the sun, shaded blue-grey away from it.
vec3 light_clouds(vec3 sky, vec3 direction, float cloud_amount) {
    float cloud = cloud_amount * horizon_glow.a;
    vec3 glow = horizon_glow.rgb / max(max(horizon_glow.r, horizon_glow.g), max(horizon_glow.b, 0.01));
    float towards = 1.0 - smoothstep(0.0, u_cloud_sun_width, angle_between(direction, sun_direction.xyz));
    vec3 tint = mix(u_cloud_shade, glow * u_cloud_sun_gain, towards * towards);
    return mix(sky, sky * tint, cloud);
}

// 0 outside 0..1 across a bow, red towards 1, violet towards 0.
vec3 spectrum(float t) {
    vec3 centre = vec3(0.82, 0.52, 0.2);
    vec3 d = (vec3(t) - centre) / 0.24;
    vec3 c = max(vec3(1.0) - d * d, vec3(0.0));
    // Violet: a little red back in at the inner edge.
    float violet = max(1.0 - pow((t - 0.04) / 0.12, 2.0), 0.0);
    return (c + vec3(0.45, 0.0, 0.2) * violet) * step(0.0, t) * step(t, 1.0);
}

// A sun shower's rainbow round the point opposite the sun, as strong as
// weather.y: the primary red outside, the secondary reversed, the sky
// lighter inside the primary and darker between the two. In the sun's
// light, so it goes with the sun.
vec3 add_rainbow(vec3 sky, vec3 direction) {
    float angle = angle_between(direction, -sun_direction.xyz);
    float r1 = u_rainbow.x;
    float w1 = u_rainbow.y;
    float r2 = u_rainbow.z;
    float w2 = u_rainbow.w;
    vec3 bow = spectrum((angle - (r1 - 0.5 * w1)) / w1) + u_rainbow_look.x * spectrum(((r2 + 0.5 * w2) - angle) / w2);
    float inside = 1.0 - smoothstep(r1 - w1, r1 - 0.5 * w1, angle);
    float between = smoothstep(r1, r1 + 0.5 * w1, angle) * (1.0 - smoothstep(r2 - w2, r2 - 0.5 * w2, angle));
    float strength = weather.y;
    sky *= 1.0 + strength * (inside * u_rainbow_look.z - between * u_rainbow_look.w);
    return sky + bow * sun_color.rgb * (u_rainbow_look.y * strength);
}

float bolt_hash(float seed, float i) {
    return fract(sin(seed * 12.9898 + i * 78.233) * 43758.5453);
}

// A lightning strike: the clouds lit round the top of its bolt (more where
// there is more `cloud`) and its bolt, jagged runs down to the horizon, far
// past white for the camcorder to bloom and smear.
vec3 add_lightning(vec3 sky, vec3 direction, float elevation, float pixel, float cloud) {
    vec3 top = u_lightning.xyz;
    float glow = max(1.0 - angle_between(direction, top) / u_lightning_glow_radius, 0.0);
    glow = glow * glow * u_lightning_glow.a * (0.35 + 0.65 * cloud);

    float top_elevation = max(asin(clamp(top.y, -1.0, 1.0)), 0.01);
    float down = 1.0 - elevation / top_elevation;  // 0 at its top, 1 at the horizon
    float along = clamp(down, 0.0, 1.0) * float(bolt_runs);
    float run = min(floor(along), float(bolt_runs - 1));
    float from = 0.0;
    float to = 0.0;
    for (int i = 0; i < bolt_runs; ++i) {
        float turn = (bolt_hash(u_lightning_bolt.x, float(i)) * 2.0 - 1.0) * u_lightning_bolt.w;
        from += float(i) < run ? turn : 0.0;
        to += float(i) <= run ? turn : 0.0;
    }
    // A fine zigzag over the big turns.
    float fine = down * bolt_zigzags;
    float zig = floor(fine);
    float zigzag = mix(bolt_hash(u_lightning_bolt.x + 7.0, zig), bolt_hash(u_lightning_bolt.x + 7.0, zig + 1.0), fine - zig);
    float wander = mix(from, to, along - run) + (zigzag * 2.0 - 1.0) * 0.3 * u_lightning_bolt.w;
    vec2 flat_top = normalize(top.xz + vec2(0.00001));
    vec2 flat_view = normalize(direction.xz + vec2(0.00001));
    float across = atan(flat_view.x * flat_top.y - flat_view.y * flat_top.x, dot(flat_view, flat_top)) * cos(elevation);
    float off = abs(across - wander);
    float width = u_lightning_bolt.z;
    float core = clamp((width + 0.5 * pixel - off) / pixel, 0.0, 1.0);
    float halo = max(1.0 - off / (width * 10.0), 0.0);
    float on_bolt = step(0.0, down) * step(down, 1.0) * (core + 0.25 * halo * halo);
    return sky + u_lightning_glow.rgb * (glow + on_bolt * u_lightning_bolt.y) * u_lightning.w;
}

// The sun's disc, far brighter than white in the glare's colour for the
// camcorder to clip, bloom and smear, blooming white past its edge (in sun
// radii) and glaring in the glow's colour further out.
vec3 add_sun(vec3 sky, vec3 direction, float elevation, float pixel, float clear) {
    float angle = angle_between(direction, sun_direction.xyz);
    float above = clamp(elevation / pixel + 0.5, 0.0, 1.0) * clear;
    float halo = max(1.0 - angle / u_sun_halo_radius, 0.0);
    vec3 glare = mix(vec3(1.0), horizon_glow.rgb, horizon_glow.a);
    sky = mix(sky, glare, halo * halo * halo * u_sun_halo_strength * above);
    float bloom = exp(-max(angle - u_sun_radius, 0.0) / u_sun_radius) * u_sun_halo_strength;
    sky = mix(sky, vec3(1.0), bloom * above);
    return mix(sky, glare * u_sun_brightness, disc_cover(angle, u_sun_radius, pixel) * above);
}

// The moon, lit on the side towards the sun as much as its phase says,
// covering what is behind it as far as the night has come and the clouds
// let it (`clear`).
vec3 add_moon(vec3 sky, vec3 direction, float elevation, float pixel, float clear) {
    vec3 moon = moon_direction.xyz;
    float cover = disc_cover(angle_between(direction, moon), u_moon_radius, pixel) * clamp(elevation / pixel + 0.5, 0.0, 1.0) * clear;
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
    float clear = 1.0 - weather.z;
    float shade = max(1.0 + (clouds - 1.0) * mix(1.0, weather.x, weather.z), 0.0);
    vec3 gradient = mix(haze_color.rgb, sky_zenith.rgb, sqrt(clamp(elevation / top_elevation, 0.0, 1.0)));
    vec3 clouded = min(gradient * shade, vec3(1.0));
    vec3 sky = mix(drawn_sky, clouded, sky_zenith.a);
    vec4 deck = cloud_deck(direction, elevation, weather.z);
    sky = mix(sky, deck.rgb, deck.a);
    sky = light_clouds(sky, direction, max(smoothstep(u_cloud_threshold, u_cloud_threshold + u_cloud_softness, clouds), deck.a));
    sky = mix(sky, horizon_glow.rgb, horizon_glow_at(direction, elevation));
    sky = add_rainbow(sky, direction);
    sky = add_stars(sky, direction, elevation, pixel, clamp(1.5 - clouds, 0.0, 1.0) * (1.0 - deck.a) * clear);
    // A cloud of the deck in front of the sun or moon hides it.
    float behind = clear * (1.0 - deck.a);
    sky = add_moon(sky, direction, elevation, pixel, behind);
    sky = add_sun(min(sky, vec3(1.0)), direction, elevation, pixel, behind);
    // Thick weather (rain's curtains, fog) hides the sky, the sun too, in its haze.
    sky = mix(sky, haze_color.rgb, weather.w * weather.w);
    sky = add_lightning(sky, direction, elevation, pixel, max(deck.a, weather.z));

    // Any land at all covers the sky; what of it the haze hides shows the
    // haze colour instead.
    float coverage = clamp(land.a / min_land_visibility, 0.0, 1.0);
    float haze = mix(min((coverage - land.a) * u_haze_amount, coverage), coverage, weather.w);
    vec3 color = sky * (1.0 - coverage) + haze_color.rgb * haze + land.rgb * flat_light(0.0) * (coverage - haze);
    frag_color = vec4(color, 1.0);
}
