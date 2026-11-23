#pragma once

// The frame's lights and shadows, once its light (data.lighting) is known:
// the night lights (round the ball, carts' headlights, every floodlight's
// lamp, game/floodlights.h), the glow floors and lamp faces risen for the
// night, and blob shadows on the ground as the game samples it: the ball's
// cast along the sun, the rest at their feet. GL-free.

#include "game/game_state.h"
#include "game/lighting_loader.h"
#include "game/play_area.h"
#include "physics/terrain.h"
#include "renderer/render_data.h"

#include <cstdint>
#include <vector>

#include <glm/vec3.hpp>

// Every tree's and floodlight pole's blob, a faint darkening at its foot (its
// shadow is the shadow map's), rebuilt only when the trees change.
struct tree_shadow_cache {
    std::vector<render_blob_shadow> blobs;
    std::uint64_t revision = 0;  // game_state::static_anchors' when built
    bool built = false;
};

void refresh_tree_shadows(tree_shadow_cache& cache, const game_state& game, const shadow_style& shadows);

// Where each blob caster of the last frame fell on the ground: a figure, cart
// or ball that has not moved falls where it did, so it is not found again
// (the ground queries dominate with a crowd). Casters are kept in frame order,
// each remembering what its answer was for; a change in any of it asks again.
struct blob_ground_memo {
    struct entry {
        glm::vec3 source{0.0f};
        glm::vec3 bottom{0.0f};
        glm::vec3 towards_sun{0.0f};
        float sun_share = 0.0f;
        float max_stretch = 0.0f;
        terrain_sample ground;
        float height = 0.0f;  // the caster's bottom over the ground under it
    };
    std::vector<entry> entries;
    std::uint64_t revision = 0;  // game_state::static_anchors' and terrain_render_revision, combined, when built
};

// Fills data.glow, data.lighting.lamp_glow, data.lights and the blob shadows
// from what `data` shows;
// the trees' are borrowed from `trees`.
void add_frame_lights(render_data& data, const game_state& game, const lighting_palette& palette,
                      const tree_shadow_cache& trees, blob_ground_memo& memo);
// A single frame, with nothing remembered from the last (a test's or a still's).
void add_frame_lights(render_data& data, const game_state& game, const lighting_palette& palette,
                      const tree_shadow_cache& trees);

// Where a blob of something at `caster` falls on the ground: cast along the
// sun (`towards_sun`, never lower than 1 / max_stretch) as far as
// `sun_share`, else straight down.
terrain_sample cast_blob_onto_ground(const play_area& area, const glm::vec3& caster, const glm::vec3& towards_sun,
                                     float sun_share, float max_stretch);
