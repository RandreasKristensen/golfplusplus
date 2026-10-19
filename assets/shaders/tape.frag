#version 330 core

// VHS playback at scene resolution (renderer/tape_pass.h). Lengths arrive in
// scene pixels, worked out from picture.json's fractions of the picture height.

out vec4 frag_color;

uniform sampler2D u_source;
uniform float u_seconds;
uniform float u_tracking;        // 0..1: the rolling band of tracking noise
uniform vec4 u_blur;             // luma sigma, chroma sigma, chroma delay, peaking
uniform vec4 u_tone;             // black lift, highlight knee, saturation, fields per second
uniform vec4 u_noise;            // grain, grain width, chroma noise, chroma noise size
uniform vec4 u_wobble;           // wobble, wobble span, head-switch height, head-switch skew
uniform vec4 u_faults;           // head-switch noise, dropout chance per line and frame, dropout length
uniform vec4 u_tracking_style;   // band height, speed in pixels per second, noise, skew

const int max_radius = 24;
const mat3 rgb_to_yiq = mat3(0.299, 0.596, 0.211,
                             0.587, -0.274, -0.523,
                             0.114, -0.322, 0.312);
const mat3 yiq_to_rgb = mat3(1.0, 1.0, 1.0,
                             0.956, -0.272, -1.106,
                             0.621, -0.647, 1.703);

uvec3 pcg3d(uvec3 v) {
    v = v * 1664525u + 1013904223u;
    v.x += v.y * v.z;
    v.y += v.z * v.x;
    v.z += v.x * v.y;
    v ^= v >> 16u;
    v.x += v.y * v.z;
    v.y += v.z * v.x;
    v.z += v.x * v.y;
    return v;
}

// 0..1
float hash(uvec3 v) {
    return float(pcg3d(v).x >> 8) / 16777216.0;
}

// -1..1, smooth along x between whole cells.
float line_noise(float x, uint line, uint field, uint salt) {
    float cell = floor(x);
    float t = x - cell;
    uint c = uint(int(cell) + 65536);
    float a = hash(uvec3(c, line, field ^ salt));
    float b = hash(uvec3(c + 1u, line, field ^ salt));
    return mix(a, b, t * t * (3.0 - 2.0 * t)) * 2.0 - 1.0;
}

// -1..1, smooth in x, y and time.
float value_noise(vec3 p, uint salt) {
    vec3 cell = floor(p);
    vec3 t = p - cell;
    t = t * t * (3.0 - 2.0 * t);
    uvec3 c = uvec3(ivec3(cell) + 65536);
    float result = 0.0;
    for (int corner = 0; corner < 8; ++corner) {
        uvec3 offset = uvec3(corner & 1, (corner >> 1) & 1, (corner >> 2) & 1);
        vec3 w = mix(1.0 - t, t, vec3(offset));
        result += w.x * w.y * w.z * hash((c + offset) ^ uvec3(salt));
    }
    return result * 2.0 - 1.0;
}

vec3 source_yiq(int x, int y, ivec2 size) {
    return rgb_to_yiq * texelFetch(u_source, ivec2(clamp(x, 0, size.x - 1), y), 0).rgb;
}

int radius_for(float sigma) {
    return min(int(ceil(2.5 * sigma)), max_radius);
}

void main() {
    ivec2 size = textureSize(u_source, 0);
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    uint line = uint(size.y - 1 - pixel.y);
    uint field = uint(u_seconds * u_tone.w);
    uint frame = field / 2u;

    // How far this line is pulled sideways: timebase wobble, the head-switch
    // skew at the bottom and the tracking band.
    float shift = u_wobble.x * (0.7 * value_noise(vec3(float(line) / u_wobble.y, 0.0, float(field)), 11u) +
                                0.3 * (hash(uvec3(line, field, 12u)) * 2.0 - 1.0));
    float head_switch = clamp(1.0 - (float(pixel.y) + 0.5) / max(u_wobble.z, 0.001), 0.0, 1.0);
    shift += u_wobble.w * head_switch * head_switch * (0.75 + 0.5 * hash(uvec3(field, 13u, 0u)));
    float band_height = u_tracking_style.x;
    float band_center = mod(u_seconds * u_tracking_style.y, float(size.y) + band_height) - 0.5 * band_height;
    float band_distance = (float(line) - band_center) / (0.5 * band_height);
    float band = u_tracking * max(1.0 - band_distance * band_distance, 0.0);
    shift += u_tracking_style.w * band * (0.5 + hash(uvec3(line, field, 14u)));

    // Brightness: a narrow Gaussian, and a wider one for the peaking halo.
    float center = float(pixel.x) - shift;
    int base = int(floor(center));
    float luma_sigma = max(u_blur.x, 0.05);
    float wide_sigma = luma_sigma * 2.5;
    int luma_radius = radius_for(wide_sigma);
    float luma = 0.0;
    float luma_weight = 0.0;
    float wide = 0.0;
    float wide_weight = 0.0;
    for (int i = -luma_radius; i <= luma_radius + 1; ++i) {
        float d = float(base + i) - center;
        float y = source_yiq(base + i, pixel.y, size).x;
        float w = exp(-d * d / (2.0 * luma_sigma * luma_sigma));
        float ww = exp(-d * d / (2.0 * wide_sigma * wide_sigma));
        luma += y * w;
        luma_weight += w;
        wide += y * ww;
        wide_weight += ww;
    }
    luma /= luma_weight;
    wide /= wide_weight;
    luma += u_blur.w * (luma - wide);

    // Colour: a much wider Gaussian, delayed to the right, averaged with the
    // line above.
    float chroma_center = center - u_blur.z;
    int chroma_base = int(floor(chroma_center));
    float chroma_sigma = max(u_blur.y, 0.05);
    int chroma_radius = radius_for(chroma_sigma);
    int above = min(pixel.y + 1, size.y - 1);
    vec2 chroma = vec2(0.0);
    float chroma_weight = 0.0;
    for (int i = -chroma_radius; i <= chroma_radius + 1; ++i) {
        float d = float(chroma_base + i) - chroma_center;
        float w = exp(-d * d / (2.0 * chroma_sigma * chroma_sigma));
        chroma += (source_yiq(chroma_base + i, pixel.y, size).yz + source_yiq(chroma_base + i, above, size).yz) * w;
        chroma_weight += 2.0 * w;
    }
    chroma /= chroma_weight;

    // Tone: soft shoulder, lifted blacks, less colour.
    float knee = u_tone.y;
    if (luma > knee && knee < 1.0) {
        luma = knee + (1.0 - knee) * (1.0 - exp(-(luma - knee) / (1.0 - knee)));
    }
    luma = u_tone.x + (1.0 - u_tone.x) * luma;
    chroma *= u_tone.z;

    // Noise from playback.
    float x = float(pixel.x);
    luma += u_noise.x * line_noise(x / max(u_noise.y, 0.5), line, field, 21u);
    vec3 chroma_p = vec3(x / u_noise.w, float(line) / (0.5 * u_noise.w), u_seconds * 3.0);
    chroma += u_noise.z * vec2(value_noise(chroma_p, 22u), value_noise(chroma_p, 23u));

    float switch_streak = 0.5 + 0.5 * line_noise(x / max(3.0 * u_noise.y, 0.5), line, field, 24u);
    float switch_amount = u_faults.x * smoothstep(0.0, 0.35, head_switch);
    luma = mix(luma, switch_streak, switch_amount);
    chroma *= 1.0 - switch_amount;

    if (hash(uvec3(line, frame, 26u)) < u_faults.y) {
        float start = hash(uvec3(line, frame, 27u)) * float(size.x);
        float streak_length = u_faults.z * (0.5 + hash(uvec3(line, frame, 28u)));
        float along = (x - start) / max(streak_length, 1.0);
        float streak = smoothstep(0.0, 0.15, along) * (1.0 - smoothstep(0.6, 1.0, along));
        luma = mix(luma, 1.0, streak * (0.7 + 0.3 * hash(uvec3(pixel.x, line, 29u))));
        chroma *= 1.0 - streak;
    }

    float speck = line_noise(x / max(u_noise.y * 2.0, 0.5), line, field, 30u);
    float speck_amount = u_tracking_style.z * band * smoothstep(0.2, 0.9, speck);
    luma = mix(luma, 0.9, speck_amount);
    chroma *= 1.0 - u_tracking_style.z * band;

    vec3 rgb = yiq_to_rgb * vec3(luma, chroma);
    if (center < -0.5) {
        rgb = vec3(0.0);
    }
    frag_color = vec4(clamp(rgb, 0.0, 1.0), 1.0);
}
