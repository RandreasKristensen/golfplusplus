#pragma once

// Planar mirrors, GL-free geometry: one real scene projection per visible
// water level, clipped to the part of the world above that plane and cropped to
// the part of the picture the water's lookups reach (renderer/water_crop.h).
#include <cstddef>
#include <vector>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include "renderer/draw_target.h"
#include "renderer/water_batch.h"
#include "renderer/water_crop.h"

// `projection` is the cropped, surface-clipped one the scene is drawn with;
// `sky_projection` is the same crop without the oblique clip plane, for the sky.
// The capture is drawn into a viewport of the crop's size at the target's corner.
struct water_reflection_view {
    glm::mat4 view{1.0f};
    glm::mat4 projection{1.0f};
    glm::mat4 sky_projection{1.0f};
    glm::vec3 eye{0.0f};
    float level = 0.0f;
    reflection_crop crop;  // the texels of the quarter-scale picture this capture holds
    draw_target target;    // set by reflection_pass: the capture's framebuffer and crop-sized viewport
};

glm::mat4 water_mirror(float level);
// `crop` is a clip-space matrix applied after the projection (the identity
// captures the whole picture); the oblique clip plane is built before it.
water_reflection_view reflect_water_view(const glm::mat4& view, const glm::mat4& projection,
                                          const glm::vec3& eye, float level,
                                          const glm::mat4& crop = glm::mat4(1.0f));
// Indices into `water.levels` (ascending level) with a cluster in the frustum.
std::vector<std::size_t> visible_water_levels(const render_water& water, const glm::mat4& view_proj);

// A level whose surface may show in this view. `query` is a GL occlusion query
// issued this frame for it, or 0 when none was; the mirror and the surface are
// drawn under it as a conditional (never earlier than this frame's result).
// `coverage` is the picture rectangle (u0, v0, u1, v1) its reflection is looked
// up in (water_level_coverage); the whole picture when it cannot be bounded.
struct water_candidate {
    std::size_t index = 0;  // into render_water::levels
    float level = 0.0f;
    unsigned int query = 0;
    glm::vec4 coverage{0.0f, 0.0f, 1.0f, 1.0f};
};
// The levels whose surface can show in `view_proj` from `eye`, with the
// rectangle each one's lookups reach. A level whose clusters all fall outside
// the picture is no candidate, except seen from under it, which needs no mirror.
std::vector<water_candidate> water_candidates(const render_water& water, const glm::mat4& view_proj, const glm::vec3& eye,
                                              const water_lookup_reach& reach);

// The capture as the surface samples it: `view_proj` takes the world to the
// capture's texture, `lookup_range` bounds a lookup within it.
struct water_reflection {
    float level = 0.0f;
    unsigned int texture = 0;
    glm::mat4 view_proj{1.0f};
    glm::vec4 lookup_range{0.0f, 0.0f, 1.0f, 1.0f};
};

struct render_data;
struct render_remote_avatar;
// The same world seen by the mirror, including my physical figure and its
// gear through the ordinary figure path, without the camera's viewmodel.
render_data make_reflected_scene(const render_data& scene,const glm::vec3& eye);
// My physical figure as the mirror shows it: smoking and drinking what I do.
render_remote_avatar mirrored_figure(const render_data& scene);
