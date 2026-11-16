// Hash and smooth value noise, shared by the scene shaders.

// 0..1, fixed for a point.
float hash12(vec2 p) {
    vec3 q = fract(vec3(p.xyx) * 0.1031);
    q += dot(q, q.yzx + 33.33);
    return fract((q.x + q.y) * q.z);
}

// Smooth 0..1 noise over cells one unit across.
float value_noise(vec2 p) {
    vec2 cell = floor(p);
    vec2 f = fract(p);
    vec2 t = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash12(cell), hash12(cell + vec2(1.0, 0.0)), t.x),
               mix(hash12(cell + vec2(0.0, 1.0)), hash12(cell + vec2(1.0, 1.0)), t.x), t.y);
}
