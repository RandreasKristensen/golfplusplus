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
// smear the reflection along the screen's vertical instead, as real
// reflections streak, so rough water blurs it rather than flattening. The reflection walks its rippled ray in steps that grow
// with distance (water_reflection_step_growth), looking for where it meets
// the screen's own pixels (beneath copy), else falls back on the sky. Mirrors
// renderer/water_light.h. From under the surface it is Snell's window: what
// lies above where the view is within the critical angle of straight up,
// the water's own murk beyond it (total internal reflection). The water
// between the eye and the surface is the underwater pass's, not this one's.
// REFLECTION_STEPS comes from water_reflection_steps.

#define REFLECTION_REFINES 4
#define SMEAR_SAMPLES 4

in vec3 v_world;
in float v_fetch;

out vec4 frag_color;

uniform sampler2D u_beneath_color;  // the scene before the water
uniform sampler2D u_beneath_depth;
uniform mat4 u_view_proj;
uniform mat4 u_inverse_view_proj;
uniform float u_time;
uniform vec3 u_absorption;        // per metre, per channel
uniform vec3 u_scatter_color;
uniform vec3 u_water;             // x: longest path; y: refraction pixels; z: reflection reach
uniform vec4 u_ripples;           // x: slope; y: wavelength; z: speed; w: fade distance
uniform vec2 u_glint;             // x: strength; y: sharpness
uniform vec2 u_wind;              // m/s at its reference height, x and z
uniform float u_rain;             // 0 dry .. 1 heaviest
uniform vec4 u_wind_rough;        // x: the wind's roughness of open water; y: calm share; z: full fetch; w: fine slope
uniform vec4 u_gusts;             // x: gust cell metres; y: gust share; z: rain ring slope; w: rain ring cell metres
uniform vec2 u_march;             // x: the reflection's first step in metres; y: each step's growth

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
// The undrawn waves' streaks: how fine across the view, how long along it.
const float streak_across_meters = 0.5;
const float streak_along_meters = 6.0;

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

// The light along `way` (unit length) the sky sends: the haze at the horizon,
// the zenith overhead. Where the screen's own pixels are tried first, this
// is what is left to fall back on.
vec3 sky_reflection(vec3 way) {
    return mix(haze_toward(way), sky_zenith.rgb, smoothstep(0.0, 0.6, way.y));
}

vec3 world_at(vec2 uv, float depth) {
    vec4 point = u_inverse_view_proj * vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    return point.xyz / point.w;
}

const float no_surface = -1.0e9;

// The part of the NDC segment p0 -> p1 (as shares of it) inside the screen;
// low > high when none is. Mirrors water_reflection_screen_span.
vec2 screen_span(vec2 p0, vec2 p1) {
    vec2 d = p1 - p0;
    d = mix(d, vec2(1.0e-6), lessThan(abs(d), vec2(1.0e-6)));
    vec2 first = (-1.0 - p0) / d;
    vec2 second = (1.0 - p0) / d;
    vec2 low = min(first, second);
    vec2 high = max(first, second);
    return vec2(max(max(low.x, low.y), 0.0), min(min(high.x, high.y), 1.0));
}

// The reflection ray, projected: from the surface (clip c0, world w0) to its
// end (c1, w1), walked in screen space.
struct reflection_ray {
    vec3 w0;
    vec3 w1;
    vec2 p0;
    vec2 p1;
    float k0;  // 1 / clip w at each end, to interpolate perspective-correctly
    float k1;
};

// Where along the ray (a share of w0 -> w1) the screen position at share `u`
// of the segment lies, the pixel there, and how far behind the picture's own
// surface there the ray is, from the eye (negative: in front; sky counts as far in front).
float ray_gap(reflection_ray ray, float u, out float along, out vec2 uv, out float eye_meters) {
    along = u * ray.k1 / mix(ray.k0, ray.k1, u);
    vec3 point = mix(ray.w0, ray.w1, along);
    eye_meters = distance(haze_eye.xyz, point);
    uv = mix(ray.p0, ray.p1, u) * 0.5 + 0.5;
    ivec2 size = textureSize(u_beneath_depth, 0);
    float depth = texelFetch(u_beneath_depth, clamp(ivec2(uv * vec2(size)), ivec2(0), size - 1), 0).r;
    if (depth >= 1.0) {
        return no_surface;
    }
    return eye_meters - distance(haze_eye.xyz, world_at(uv, depth));
}

// The sky, or where the ray `way` from the surface meets the screen's pixels,
// faded out toward the picture's edge, the ray's reach and rays that come back
// at the eye. The hit's colour already carries its haze, so the surface's own
// is taken out for the caller to put back once. The steps are spaced along
// the ray in metres, growing, not across the screen: a ray running away from
// the eye covers metres in a pixel, and screen-even steps there jump whole
// crowns.
vec3 reflected_color(vec3 world, vec3 way, float smear, vec3 air, float clear) {
    vec3 sky = sky_reflection(way);
    float reach = u_water.z;
    vec4 c0 = u_view_proj * vec4(world, 1.0);
    vec4 c1 = u_view_proj * vec4(world + way * reach, 1.0);
    float kept = 1.0;  // the share of the reach in front of the eye
    if (c0.w <= 0.05) {
        return sky;
    }
    if (c1.w < 0.05) {
        kept = (c0.w - 0.05) / (c0.w - c1.w);
        c1 = mix(c0, c1, kept);
    }
    reflection_ray ray;
    ray.w0 = world;
    ray.w1 = world + way * reach * kept;
    ray.p0 = c0.xy / c0.w;
    ray.p1 = c1.xy / c1.w;
    ray.k0 = 1.0 / c0.w;
    ray.k1 = 1.0 / c1.w;
    vec2 span = screen_span(ray.p0, ray.p1);
    if (span.x >= span.y) {
        return sky;
    }
    // A per-pixel start within each step turns banding into fine grain.
    float jitter = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
    float length_kept = reach * kept;
    float walked = 0.0;
    float step_meters = u_march.x;
    float previous_u = span.x;
    float previous_eye = distance(haze_eye.xyz, world);
    float hit_u = -1.0;
    for (int i = 0; i < REFLECTION_STEPS; ++i) {
        float share = min((walked + step_meters * (1.0 - jitter)) / length_kept, 1.0);
        walked += step_meters;
        step_meters *= u_march.y;
        // The share of the screen segment at this share of the ray.
        float u = share * ray.k0 / (share * ray.k0 + (1.0 - share) * ray.k1);
        if (u > span.y) {
            break;
        }
        if (u < span.x) {
            continue;
        }
        float along;
        vec2 uv;
        float eye_meters;
        float gap = ray_gap(ray, u, along, uv, eye_meters);
        // A step cannot skip a thin trunk, yet nothing smears behind itself:
        // the thickness is the eye distance the ray covers in one step.
        float thickness = max(0.25, 1.5 * abs(eye_meters - previous_eye));
        if (gap > 0.0 && gap < thickness) {
            float near_u = previous_u;
            float far_u = u;
            for (int j = 0; j < REFLECTION_REFINES; ++j) {
                float mid = 0.5 * (near_u + far_u);
                if (ray_gap(ray, mid, along, uv, eye_meters) > 0.0) {
                    far_u = mid;
                } else {
                    near_u = mid;
                }
            }
            hit_u = far_u;
            break;
        }
        previous_u = u;
        previous_eye = eye_meters;
        if (share >= 1.0) {
            break;
        }
    }
    if (hit_u < 0.0) {
        return sky;
    }
    float hit_along;
    vec2 hit_uv;
    float hit_eye;
    ray_gap(ray, hit_u, hit_along, hit_uv, hit_eye);
    float hit_t = hit_along * kept * reach;
    ivec2 size = textureSize(u_beneath_color, 0);
    ivec2 base = clamp(ivec2(floor(hit_uv * vec2(size) - 0.5)), ivec2(0), size - 2);
    float depth = texelFetch(u_beneath_depth, base, 0).r;
    // What lies below the water plane is the bed seen through the copy.
    if (depth < 1.0 && world_at((vec2(base) + 0.5) / vec2(size), depth).y < world.y - 0.05) {
        return sky;
    }
    // The waves not drawn tilt the ray up and down by about `smear`: the
    // picture is spread along where the hit moves on screen as the ray
    // tilts, over the samples, what lies under the water taken as sky.
    vec4 tilted = u_view_proj * vec4(world + normalize(way + vec3(0.0, smear, 0.0)) * hit_t, 1.0);
    vec2 spread = tilted.w > 0.05 ? (tilted.xy / tilted.w * 0.5 + 0.5) - hit_uv : vec2(0.0);
    spread *= min(1.0, 0.15 / max(length(spread), 1.0e-6));
    vec3 hit = vec3(0.0);
    for (int i = 0; i < SMEAR_SAMPLES; ++i) {
        vec2 at = hit_uv + spread * ((float(i) + jitter) / float(SMEAR_SAMPLES) - 0.5);
        ivec2 texel = clamp(ivec2(at * vec2(size)), ivec2(0), size - 1);
        float at_depth = texelFetch(u_beneath_depth, texel, 0).r;
        bool under = at_depth < 1.0 && world_at((vec2(texel) + 0.5) / vec2(size), at_depth).y < world.y - 0.05;
        hit += under ? sky * clear + air : texelFetch(u_beneath_color, texel, 0).rgb;
    }
    hit /= float(SMEAR_SAMPLES);
    vec2 edge = smoothstep(0.0, 0.1, min(hit_uv, 1.0 - hit_uv));
    float confidence = min(edge.x, edge.y) * (1.0 - smoothstep(0.6, 1.0, hit_t / reach)) *
        smoothstep(0.0, 0.3, dot(way, -normalize(haze_eye.xyz - world)));
    vec3 seen = max(hit - air, vec3(0.0)) / max(clear, 0.05);
    return mix(sky, seen, confidence);
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
    float fresnel = water_f0 + (1.0 - water_f0) * pow(1.0 - max(dot(normal, view), 0.0), 5.0);
    vec3 bounce = reflect(-view, normal);
    // The waves not drawn still tilt each spot's ray up or down. Seen at a
    // low angle a wave's face runs across the view and is long along it, so
    // the tilt is noise fine across the view and long along it, drifting
    // with the wind: the reflection frays into vertical streaks.
    vec2 along_view = normalize(-view.xz + vec2(1.0e-5));
    vec2 drifted = v_world.xz - 0.3 * u_wind * u_time;
    float tilt = value_noise(vec2(dot(drifted, vec2(-along_view.y, along_view.x)) / streak_across_meters,
                                  dot(drifted, along_view) / streak_along_meters)) * 2.0 - 1.0;
    bounce.y += 2.0 * smear * tilt;
    // A wave tilted past the view would send the ray into the water.
    vec3 way = normalize(vec3(bounce.x, max(bounce.y, 0.002), bounce.z));
    vec3 glint = sun_color.rgb * u_glint.x * smoothstep(0.0, 0.1, sun_direction.y) *
        pow(max(dot(reflect(-sun_direction.xyz, normal), view), 0.0), u_glint.y);
    vec3 surface = mix(scatter, reflected_color(v_world, way, smear, air, clear), fresnel) + glint;
    frag_color = vec4(surface * clear + air + (1.0 - fresnel) * through, 1.0);
}
