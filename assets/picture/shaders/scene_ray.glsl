// The scene's depth read back as how far each pixel's ray goes from the eye
// (renderer/sun_shaft_pass.h). Needs lighting.glsl's haze_eye.

uniform sampler2D u_depth;  // the scene's depth, unfiltered
uniform mat4 u_inverse_view_proj;
uniform float u_reach;  // metres: no ray goes further, the sky's to here

// The way (xyz, unit length) from the eye through the scene's pixel `texel`
// and how far (w, metres) along it what it shows lies, out to u_reach.
vec4 scene_ray(ivec2 texel) {
    vec2 uv = (vec2(texel) + 0.5) / vec2(textureSize(u_depth, 0));
    float depth = texelFetch(u_depth, texel, 0).r;
    vec4 point = u_inverse_view_proj * vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec3 to = point.xyz / point.w - haze_eye.xyz;
    float meters = depth < 1.0 ? length(to) : u_reach;
    return vec4(normalize(to), min(meters, u_reach));
}
