#pragma once

// Cropping the mirrored capture of a pond to what its surface looks up, GL-free.
// water.frag finds a surface point's reflection at that point's own position in
// the picture (a point on the water plane projects the same through the mirror),
// moved by the tilt of the resolved waves and spread by the unresolved ones. So
// the capture needs only the picture rectangle the water covers, widened by the
// most the shader can move a lookup, which this derives from the shader's own
// constants (checked against water.frag in tests) and the style and weather.
// Where a finite rectangle cannot be proven the whole picture is captured.

#include <optional>

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include "game/lighting_loader.h"
#include "renderer/water_batch.h"

// The most water.frag shifts a reflection lookup.
struct water_lookup_reach {
    // Metres a lookup point moves along the surface per metre of eye distance.
    float tilt = 0.0f;
    // Largest offset of a smear tap, as a share of the picture, per axis.
    glm::vec2 smear{0.0f};
};

// At most this far, whatever the fetch, gust or wave phase: the wind's roughness
// grown to the full fetch, every resolved wave at its steepest, the finest
// noise at its steepest, and a rain ring.
water_lookup_reach water_reflection_reach(const water_style& style, float wind_meters_per_second, float rain);

// The picture rectangle (u0, v0, u1, v1, within 0..1) that the lookups of one
// cluster's surface can reach, or nullopt when none of it shows. The whole
// picture when the reach cannot be bounded, such as water the lookup can carry
// behind the eye. `view_proj` is the picture's, never a mirror's.
enum class water_cover { outside, rectangle };
struct water_cluster_cover {
    water_cover kind = water_cover::outside;
    glm::vec4 rectangle{0.0f, 0.0f, 1.0f, 1.0f};
};
water_cluster_cover water_cluster_coverage(const water_cluster& cluster, float level, const glm::mat4& view_proj,
                                           const glm::vec3& eye, const water_lookup_reach& reach);

// The quarter-scale capture of a rectangle, in whole texels of the full
// `full_width` x `full_height` picture, with a guard for bilinear filtering.
struct reflection_crop {
    int x = 0;
    int y = 0;
    int width = 1;
    int height = 1;
    int full_width = 1;
    int full_height = 1;
};
reflection_crop snap_reflection_crop(const glm::vec4& rectangle, int full_width, int full_height);

// Clip-space matrix taking the full picture's projection to the crop's, which
// the capture renders into a viewport of the crop's size.
glm::mat4 reflection_crop_matrix(const reflection_crop& crop);
// The cropped clip space of the capture to the clip space of its texture, whose
// first texels the crop is drawn into (the texture is the full picture's size).
glm::mat4 reflection_texture_matrix(const reflection_crop& crop);
// Clamp range (u_min, v_min, u_max, v_max) of a lookup in that texture: a half
// texel inside the full picture's edges, as clamp-to-edge filtering behaves.
glm::vec4 reflection_lookup_range(const reflection_crop& crop);
