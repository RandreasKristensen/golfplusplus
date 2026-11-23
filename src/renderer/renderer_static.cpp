#include "renderer/renderer.h"

#include "renderer/fence_batch.h"
#include "renderer/marker_scene.h"
#include "renderer/hole_sign_batch.h"
#include "renderer/prop_glow.h"
#include "renderer/shadow_casters.h"

#include <cstring>

namespace {
// What the sign and fence casters were built for: their geometry changes only
// with the course (a sign's words repaint its face, not its triangles).
std::uint64_t static_casters_key(const render_data& data) {
    std::uint64_t key = 1469598103934665603ULL;
    const auto mix = [&key](const std::uint64_t value) { key = (key ^ value) * 1099511628211ULL; };
    if (data.hole_signs != nullptr) {
        mix(data.hole_signs->terrain_revision);
        mix(data.hole_signs->vertices.size());
    }
    if (data.fences != nullptr) {
        mix(data.fences->revision);
        mix(data.fences->posts.size());
    }
    return key;
}
}

namespace {
// What the cups were built from: where every cup is and how big.
std::uint64_t cups_key_of(const world_marker_scene& scene) {
    std::uint64_t key = 1469598103934665603ULL;
    const auto mix = [&key](const float value) {
        std::uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        key = (key ^ bits) * 1099511628211ULL;
    };
    const auto mix_point = [&mix](const glm::vec3& point) {
        mix(point.x);
        mix(point.y);
        mix(point.z);
    };
    if (scene.show_hole) { mix_point(scene.pin_position); }
    if (scene.pin_markers != nullptr) {
        for (const glm::vec3& pin : *scene.pin_markers) { mix_point(pin); }
    }
    mix(scene.show_hole ? 1.0f : 0.0f);
    mix(scene.cup_radius_meters);
    mix(scene.cup_depth_meters);
    return key;
}
}

bool renderer::refresh_static_geometry(const render_data& data, frame_profile* profile) {
    const bool new_course = !bus_door_seeded_ || world_props_revision_ != data.trees_revision;
    if (new_course) {
        prop_batch placed;
        if (data.world_props) { append_world_props(placed, *data.world_props, props_, glow_floor_slots()); }
        prop_renderer_.set_static(placed, data.trees_revision, profile);
        world_props_revision_ = data.trees_revision;
    }
    const std::uint64_t key = static_casters_key(data);
    if (new_course || key != static_casters_key_ || !shadow_map_pass_.static_casters_held()) {
        shadow_map_pass_.set_static_casters(static_caster_corners(data.hole_signs, data.fences), profile);
        static_casters_key_ = key;
    }
    // The tee boxes' tiles take the markers' colours, so a new look builds them again.
    const std::uint64_t tee_key = data.trees_revision * 1000003ULL + looks_revision_;
    if (new_course || tee_key != tee_boxes_key_ || tee_box_batch_.empty()) {
        build_tee_box_batch(tee_box_batch_, data.tee_boxes, colors_.markers);
        tee_box_renderer_.stage(tee_box_batch_, profile, upload_kind::cached);
        tee_boxes_key_ = tee_key;
    }
    // The cups' openings: kept while the pins stand where they did, with the glow floors as slots.
    const world_marker_scene scene = marker_scene_for(data, picture_.height);
    const std::uint64_t cups_key = cups_key_of(scene) * 1000003ULL + tee_key;
    if (new_course || cups_key != cups_key_ || cup_batch_.empty()) {
        scene_colors slotted = colors_;
        slotted.glow = glow_floor_slots();
        build_cup_batch(cup_batch_, scene, slotted);
        cup_renderer_.stage(cup_batch_, profile, upload_kind::cached);
        cups_key_ = cups_key;
    }
    return new_course;
}
