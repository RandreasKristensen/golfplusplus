#version 330 core

#include "lighting.glsl"
#include "noise.glsl"

// A pond's surface, opaque: what lies under it (the copy of the scene drawn
// so far) seen through the water's depth, absorbed and tinted by it, mixed
// with what it reflects by Fresnel, and the sun's glint on its ripples; then
// hazed. How rough it is follows the wind (water_roughness in
// renderer/water_light.h): a mirror in still air, broken by the waves the
// wind raises over the open water upwind (the fetch) and by gusts sweeping
// across, with rain rings on top. The waves too far or too small to draw
// smear the real planar reflection. Mirrors renderer/water_light.h.
// From below the surface it is Snell's window; the underwater pass owns
// the water between the eye and the surface.
#define SMEAR_SAMPLES 4

in vec3 v_world;
in float v_fetch;

out vec4 frag_color;

uniform sampler2D u_beneath_color;  // the scene before the water
uniform sampler2D u_beneath_depth;
uniform sampler2D u_reflection;
uniform mat4 u_reflection_view_proj;  // world to the mirror texture's clip space (its crop of the picture)
uniform vec4 u_reflection_range;      // the lookup's clamp in that texture: u min, v min, u max, v max
uniform mat4 u_view_proj;
uniform mat4 u_inverse_view_proj;
uniform float u_time;
uniform vec3 u_absorption;        // per metre, per channel
uniform vec3 u_scatter_color;
uniform vec2 u_water;             // x: longest path; y: refraction pixels
uniform vec4 u_ripples;           // x: slope; y: wavelength; z: speed; w: fade distance
uniform vec2 u_glint;             // x: strength; y: sharpness
uniform vec2 u_wind;              // m/s at its reference height, x and z
uniform float u_rain;             // 0 dry .. 1 heaviest
uniform vec4 u_wind_rough;        // x: the wind's roughness of open water; y: calm share; z: full fetch; w: fine slope
uniform vec4 u_gusts;             // x: gust cell metres; y: gust share; z: rain ring slope; w: rain ring cell metres

const float water_f0 = 0.02;
const float water_index = 1.33;
// Cosine of the critical angle, asin(1 / n) from straight up, and how far
// either side of it the window's edge blurs.
const float window_edge_cosine = 0.6593;
const float window_edge_blur = 0.04;

// Waves off the wind's way: x the angle off it, y the share of the longest
// wavelength, z the share of the slope. Each fades to flat by its own share
// of the fade distance, so none aliases, and what fades goes to the fine
// smear. Shorter waves run slower, as the square root of their length.
const int wave_count = 5;
const vec3 waves[wave_count] = vec3[wave_count](vec3(0.0, 1.0, 0.34), vec3(0.6, 0.63, 0.24), vec3(-0.5, 0.41, 0.18),
                                                vec3(1.1, 0.26, 0.14), vec3(-1.0, 0.17, 0.10));
const float tau = 6.2831853;

// calm share .. 1: the wind's roughness grown over the fetch here, the gusts
// moving with the wind. Mirrors water_roughness.
float roughness_at(vec3 world) {
    float gust = value_noise((world.xz - u_wind * u_time) / u_gusts.x);
    float gusting = u_gusts.y + (1.0 - u_gusts.y) * gust;
    float fetch_share = sqrt(clamp(v_fetch / u_wind_rough.z, 0.0, 1.0));
    return u_wind_rough.y + (1.0 - u_wind_rough.y) * u_wind_rough.x * fetch_share * gusting;
}

// The slope of the rain's rings here, `near` of it (fading with distance):
// in each cell a drop falls at a random spot in a random second, as often
// as the rain is heavy, its ring spreading and dying away over that second.
vec2 rain_slope(vec3 world, float near) {
    if (u_rain <= 0.0 || near <= 0.0) {
        return vec2(0.0);
    }
    vec2 grid = world.xz / u_gusts.w;
    vec2 id = floor(grid);
    float t = u_time + 7.0 * hash12(id);
    float round_of = floor(t);
    float age = t - round_of;
    if (hash12(id + round_of * 13.17) > u_rain) {
        return vec2(0.0);
    }
    vec2 centre = id + 0.25 + 0.5 * vec2(hash12(id + 3.7 + round_of), hash12(id + 9.1 + round_of));
    vec2 out_way = grid - centre;
    float r = length(out_way);
    float x = clamp((r - 0.3 * age) / 0.12, -1.0, 1.0);
    return out_way / max(r, 1.0e-4) * (-sin(3.14159265 * x) * u_gusts.z * (1.0 - age) * near);
}

// The fine ripples' slope, 0 .. about 1 each way: the gradient of noise in
// cells of fine_cell_meters, drifting with the wind.
const float fine_cell_meters = 0.35;

vec2 fine_slope_at(vec2 ground) {
    const float e = 0.15;
    return vec2(value_noise(ground + vec2(e, 0.0)) - value_noise(ground - vec2(e, 0.0)),
                value_noise(ground + vec2(0.0, e)) - value_noise(ground - vec2(0.0, e))) / (3.0 * e);
}

// The surface's normal, and in `smear` the slope of the waves not drawn
// (too far or too fine), which spreads the reflection instead.
vec3 ripple_normal(vec3 world, float eye_meters, float rough, out float smear) {
    vec2 wind_way = dot(u_wind, u_wind) > 1.0e-4 ? normalize(u_wind) : vec2(0.8, 0.6);
    vec2 slope = vec2(0.0);
    float lost = 0.0;
    for (int i = 0; i < wave_count; ++i) {
        float c = cos(waves[i].x);
        float s = sin(waves[i].x);
        vec2 way = vec2(wind_way.x * c - wind_way.y * s, wind_way.x * s + wind_way.y * c);
        float k = tau / (u_ripples.y * waves[i].y);
        float phase = k * (dot(way, world.xz) - u_ripples.z * sqrt(waves[i].y) * u_time) + 1.7 * float(i);
        float fade = 1.0 - smoothstep(0.0, u_ripples.w * waves[i].y, eye_meters);
        slope += way * (cos(phase) * waves[i].z * fade);
        lost += waves[i].z * (1.0 - fade);
    }
    float fine_fade = 1.0 - smoothstep(0.0, 0.15 * u_ripples.w, eye_meters);
    vec2 fine = fine_slope_at((world.xz - 0.3 * u_wind * u_time) / fine_cell_meters);
    slope = rough * (slope * u_ripples.x + fine * (u_wind_rough.w * fine_fade));
    smear = rough * (u_wind_rough.w * (1.0 - fine_fade) + u_ripples.x * lost);
    // Rings too small to draw far off roughen the surface as a whole.
    smear += 0.5 * u_rain * u_gusts.z * (1.0 - fine_fade);
    slope += rain_slope(world, 1.0 - smoothstep(0.0, u_ripples.w, eye_meters));
    return normalize(vec3(-slope.x, 1.0, -slope.y));
}

// Four unresolved wave faces, weighted by their area seen by the eye.
// Filtering only the reflected picture leaves Fresnel on a flat mirror as
// ripples fade. Carry their slope into the surface response too. Mirrors
// water_filtered_fresnel; this is a small quadrature, not a reflection cap.
float filtered_fresnel(float cosine, float slope) {
    float c = clamp(cosine, 0.0, 1.0);
    float side = slope * sqrt(max(1.0 - c * c, 0.0));
    vec4 facing = clamp(vec4(c + side, c - side, c, c) / sqrt(1.0 + slope * slope), 0.0, 1.0);
    vec4 f = water_f0 + (1.0 - water_f0) * pow(1.0 - facing, vec4(5.0));
    float area = dot(facing, vec4(1.0));
    return area > 0.0 ? dot(f, facing) / area : 1.0;
}

// The share of the surface whose reflected ray, `up` its height before the
// undrawn waves of `slope` tilt it, points down into the next wave.
float water_masked_share(float up, float slope) {
    float reach = 2.0 * slope + 0.02;
    return 1.0 - smoothstep(-reach, reach, up);
}

// Project the surface into its own mirror. Tilting the lookup displaces the
// reflected image; unresolved slopes broaden it without changing the scene.
// The mirror holds only the part of the picture the water covers, widened by
// how far the lookup can move (water_reflection_reach, renderer/water_crop.h
// mirrors the 0.08, the spread and the slopes here). A point on the surface
// is at its own picture position in its mirror, and the picture's offsets are
// the texture's, because the texture is as many texels as the whole picture.
vec3 reflected_color(vec3 world, vec3 normal, float smear, vec3 air, float clear) {
    vec4 clip = u_reflection_view_proj * vec4(world, 1.0);
    vec2 uv = clip.xy / clip.w * 0.5 + 0.5;
    vec4 tilted = u_reflection_view_proj * vec4(world + vec3(normal.x,0,normal.z) *
                                               (0.08 * distance(haze_eye.xyz,world)),1);
    vec2 distortion = (tilted.xy / max(tilted.w,0.01) - clip.xy / clip.w) * 0.5;
    vec2 spread = vec2(0.003, 0.04) * smear;
    vec3 color = vec3(0);
    for (int i = 0; i < SMEAR_SAMPLES; ++i) {
        vec2 offset = spread * (float(i) / float(SMEAR_SAMPLES - 1) - 0.5);
        color += texture(u_reflection, clamp(uv + distortion + offset, u_reflection_range.xy, u_reflection_range.zw)).rgb;
    }
    // The mirrored scene already carries the light along the reflected ray.
    return max(color / float(SMEAR_SAMPLES) - air, vec3(0)) / max(clear, 0.05);
}

void main() {
    vec3 to_eye = haze_eye.xyz - v_world;
    float eye_meters = length(to_eye);
    vec3 view = to_eye / max(eye_meters, 0.001);
    bool below = haze_eye.y < v_world.y;
    float smear;
    vec3 normal = ripple_normal(v_world, eye_meters, roughness_at(v_world), smear);
    vec3 light = flat_light(0.0);

    ivec2 size = textureSize(u_beneath_color, 0);
    ivec2 texel = ivec2(gl_FragCoord.xy);
    float depth = texelFetch(u_beneath_depth, min(texel, size - 1), 0).r;
    // The ripples bend the view; a sample that lies in front of the surface
    // is something above the water, so it is left out.
    float wobble = u_water.y * (1.0 - smoothstep(0.0, u_ripples.w, eye_meters)) / max(u_ripples.x, 0.0001);
    ivec2 bent = clamp(texel + ivec2(round(normal.xz * wobble)), ivec2(0), size - 1);
    float bent_depth = texelFetch(u_beneath_depth, bent, 0).r;
    if (bent_depth < gl_FragCoord.z) {
        bent = texel;
        bent_depth = depth;
    }
    vec3 beneath = texelFetch(u_beneath_color, min(bent, size - 1), 0).rgb;

    if (below) {
        // The ripples tilt the window's edge a little.
        float up = max(dot(-view, normal), 0.0);
        float window = smoothstep(window_edge_cosine - window_edge_blur, window_edge_cosine + window_edge_blur, up);
        float through_cosine = sqrt(max(1.0 - water_index * water_index * (1.0 - up * up), 0.0));
        float lost = water_f0 + (1.0 - water_f0) * pow(1.0 - through_cosine, 5.0);
        vec3 murk = u_scatter_color * light * exp(-u_absorption * (v_world.y - haze_eye.y));
        frag_color = vec4(mix(murk, beneath, window * (1.0 - lost)), 1.0);
        return;
    }

    float path = u_water.x;
    if (bent_depth < 1.0) {
        vec4 point = u_inverse_view_proj * vec4((vec2(bent) + 0.5) / vec2(size) * 2.0 - 1.0, bent_depth * 2.0 - 1.0, 1.0);
        path = min(distance(v_world, point.xyz / point.w), u_water.x);
    }
    // What lies beneath was hazed when drawn: only the light the surface adds
    // is hazed here, and the beneath's own haze is taken out before the water
    // absorbs it.
    vec3 air = hazed(vec3(0.0), v_world);
    vec3 through = max(beneath - air, vec3(0.0)) * exp(-u_absorption * path);
    vec3 scatter = u_scatter_color * light * (1.0 - exp(-u_absorption * path));
    float clear = 1.0 - haze_at(v_world);
    float fresnel = filtered_fresnel(dot(normal, view), smear);
    vec3 bounce = reflect(-view, normal);
    // A wave face tilted past the view sends its ray down into the next wave,
    // which shows the water's own light, not the bright horizon. The waves
    // not drawn tilt the ray up to twice their slope either way: the share of
    // them that does, never a per-pixel pick, which speckles distant water.
    // Mirrors water_masked_share.
    float masked = water_masked_share(bounce.y, smear);
    // Gaussian approximation to convolving the Phong lobe with the undrawn
    // slopes (reflection doubles their angle): broaden it and preserve its
    // integrated energy, rather than turning distant waves into a flat glint.
    float glint_power = u_glint.y / (1.0 + 4.0 * u_glint.y * smear * smear);
    vec3 glint = sun_color.rgb * u_glint.x * smoothstep(0.0, 0.1, sun_direction.y) *
        ((glint_power + 1.0) / (u_glint.y + 1.0)) *
        pow(max(dot(reflect(-sun_direction.xyz, normal), view), 0.0), glint_power);
    vec3 reflected = masked < 1.0 ? reflected_color(v_world, normal, smear, air, clear) : scatter;
    vec3 surface = mix(scatter, mix(reflected, scatter, masked), fresnel) + glint;
    frag_color = vec4(surface * clear + air + (1.0 - fresnel) * through, 1.0);
}
