// The camcorder's metering (renderer/camcorder_pass.h): what the iris lets in,
// from the eased log of the scene's mean brightness in the adaptation
// texture's alpha.

uniform sampler2D u_adapt;
uniform vec4 u_iris;  // middle grey, the least and most the iris scales by, the time of day's exposure

const vec3 luma_weights = vec3(0.2126, 0.7152, 0.0722);

float camera_exposure() {
    float mean = exp(texelFetch(u_adapt, ivec2(0), 0).a);
    return u_iris.w * clamp(u_iris.x / max(mean, 0.0001), u_iris.y, u_iris.z);
}
