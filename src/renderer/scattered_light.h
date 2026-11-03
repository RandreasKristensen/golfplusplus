#pragma once

// The air and the night lights on the CPU, as lighting.glsl works them out:
// the haze at a point, the low mist in it, and its colour, lit by the sun,
// looking a way; the light reaching a point; and the light the air between
// the eye and a point scatters towards the eye (what `hazed` adds), so tests can say what a lamp
// lights, how its beam shows in the fog, where the haze glows and what the
// sun shafts and the floodlights' shadows take out of it. GL-free.

#include "renderer/lighting_uniforms.h"

#include <cstddef>
#include <functional>

#include <glm/vec3.hpp>

struct reaching_light {
    glm::vec3 light{0.0f};
    glm::vec3 towards{0.0f};  // the way to the light
};

// The share of the light the low mist takes out per metre at `world`.
float mist_at(const frame_lighting_block& block, const glm::vec3& world);

// The low mist's optical depth along `way` (unit length) from the block's
// eye out to `meters`, no further than the haze's full distance.
float mist_depth(const frame_lighting_block& block, const glm::vec3& way, float meters);

// How much of what is `meters` from the block's eye along `way` (unit
// length) the haze, the low mist with it, hides, 0..1.
float haze_over(const frame_lighting_block& block, const glm::vec3& way, float meters);

// How much of a thing at `world` the haze hides from the block's eye, 0..1.
float haze_at(const frame_lighting_block& block, const glm::vec3& world);

// The sun's light the haze sends along `way` (unit length), per unit of haze.
glm::vec3 haze_sun_glow(const frame_lighting_block& block, const glm::vec3& way);

// The haze's colour looking along `way` (unit length), the sun's light in it.
glm::vec3 haze_toward(const frame_lighting_block& block, const glm::vec3& way);

// What sun_shafts.frag takes out of a pixel: the sun's glow from the haze
// that lies in shadow along `way` from the eye out to `meters`, in `steps`
// steps each sampling `lit_at` (the sun the shadow map lets through, 0..1)
// `offset` (0..1, the pixel's dither) of the way through it. Each step's
// haze is exact, so a ray all in shadow takes out all of its glow.
glm::vec3 shaft_shade(const frame_lighting_block& block, const std::function<float(const glm::vec3&)>& lit_at,
                      const glm::vec3& way, float meters, int steps, float offset);

// The most shaft_shade could take out of any pixel out to `reach_meters`: a
// ray all in shadow straight at the sun, through the low mist as thick as on
// the lowest ground all the way, in the sun's brightest channel.
float shaft_peak(const frame_lighting_block& block, float reach_meters);

// Light `i` of `block` reaching `world`, before which way a surface there faces.
reaching_light light_reaching(const frame_lighting_block& block, std::size_t i, const glm::vec3& world);

// Light `i` of `block` reaching the air at `world`: by the inverse square of
// the distance, matching light_reaching at scatter.z of its reach, softened
// over a housing scatter.w across.
reaching_light air_reaching(const frame_lighting_block& block, std::size_t i, const glm::vec3& world);

// Light `i`'s light the air along `way` (unit length) from the block's eye
// out to `far` metres scatters towards the eye.
glm::vec3 scattered_from(const frame_lighting_block& block, std::size_t i, const glm::vec3& way, float far);

// The night lights' light the air between the block's eye and `world`
// scatters towards the eye: scattered_from summed over every light.
glm::vec3 scattered_light(const frame_lighting_block& block, const glm::vec3& world);

// What sun_shafts.frag takes out of a pixel for lamp `i`, whose beam its
// shadow map shades: of the light scattered_from gives along `way` out to
// `meters`, the share whose air `lit_at` (the lamp's light its map lets
// through, 0..1) says lies in shadow, found in `steps` steps evenly in the
// angle the lamp sees the run under, each `offset` (0..1, the pixel's
// dither) of the way through its step. A run all in shadow takes out all
// of the beam's light; one all lit, none.
glm::vec3 lamp_shaft_shade(const frame_lighting_block& block, std::size_t i,
                           const std::function<float(const glm::vec3&)>& lit_at, const glm::vec3& way, float meters,
                           int steps, float offset);
