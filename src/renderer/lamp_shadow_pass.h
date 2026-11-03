#pragma once

// The floodlights' shadow maps: the trees and floodlights seen from each of
// the shaft_style::lamps floodlights nearest the camera (lamp_shadows.h), in
// tiles side by side in one depth texture, which the sun shaft pass steps
// to take the shadowed part of those beams out of the haze. Floodlights and
// trees never move, so a map is drawn only when its tile takes a new lamp or
// the trees or floodlights are rebuilt, never per frame; players and carts
// cast nothing into them. Carts' headlights are never shadowed.

#include "game/lighting_loader.h"
#include "physics/tree_body.h"
#include "profiling/profiling.h"
#include "renderer/floodlight_renderer.h"
#include "renderer/gl_timer.h"
#include "renderer/lamp_shadows.h"
#include "renderer/render_data.h"
#include "renderer/shader.h"
#include "renderer/tree_renderer.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

class lamp_shadow_pass {
public:
    // shadow_map.vert and shadow_map.frag.
    bool init(const std::string& vertex_path, const std::string& fragment_path, const shaft_style& style);
    void shutdown();
    // Picks this frame's shadowed lamps among `lights` (the scene's, as
    // frame_lighting holds them) seen from `eye`, and draws the maps of those
    // new to their tiles. Leaves its framebuffer bound when it draws: the
    // caller binds its own.
    void update(const std::vector<render_light>& lights, const glm::vec3& eye, tree_renderer& tree_batch,
                const std::vector<tree_body>& trees, std::uint64_t trees_revision, floodlight_renderer& floodlight_batch,
                const render_floodlights* floodlights, gl_timer_pool& timers, frame_profile* profile);
    // How many lamps' beams take shadows this frame.
    std::size_t count() const { return slots_.size(); }
    // Binds the maps to `unit` and tells `receiver` (sun_shafts.frag with
    // LAMP_SHADOWS count(), in use) which light and tile each lamp is.
    void bind(const shader_program& receiver, int unit) const;

private:
    shader_program shader_;
    shaft_style style_;
    unsigned int fbo_ = 0;
    unsigned int depth_texture_ = 0;
    std::vector<std::optional<lamp_view>> held_;  // per tile
    std::vector<lamp_slot> slots_;
    std::vector<glm::mat4> view_projs_;  // per slot
    std::uint64_t trees_revision_ = 0;
    std::uint64_t floodlights_revision_ = 0;
    bool casters_known_ = false;
};
