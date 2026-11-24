#pragma once

// A real lightning strike's bolt as geometry in the world: a jagged channel
// from high over the struck golfer down to their feet, with a few short
// branches, as camera-facing ribbons of triangles. Its shape comes from the
// strike's seed, so the same strike is the same bolt every frame. The
// renderer (world_bolt_renderer.h) draws it in the scene, depth-tested, so
// the ground and trees hide what they stand in front of. GL-free.

#include "renderer/render_data.h"

#include <vector>

#include <glm/vec3.hpp>

// `across` is -1 .. 1 over the ribbon (0 on its axis, its core a third of
// the way out); `strength` is how bright that part of the bolt is, 0 .. 1.
struct world_bolt_vertex {
    glm::vec3 position{0.0f};
    float across = 0.0f;
    float strength = 0.0f;
};

// Triangles; empty when `lightning` has no real strike showing (no `point`,
// or no flash). `camera` is where the ribbons face.
std::vector<world_bolt_vertex> make_world_bolt(const render_lightning& lightning, const glm::vec3& camera);
