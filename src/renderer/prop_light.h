#pragma once

// The light on a model's piece (assets/picture/shaders/prop.frag) worked out
// on the CPU, the same sum lighting.glsl makes, so tests can say what a prop
// looks like at a time of day or beside a lamp. GL-free.

#include "renderer/lighting_uniforms.h"

#include <glm/vec3.hpp>

// The light reaching a surface at `world` facing `normal` (turned to face
// `eye`, as a thin panel is lit) that sees `sun` (0..1) of the sun, never
// below `glow`: what prop.frag multiplies its colour by, before the haze.
glm::vec3 prop_light(const frame_lighting_block& block, const glm::vec3& world, const glm::vec3& normal, float sun,
                     float glow);
