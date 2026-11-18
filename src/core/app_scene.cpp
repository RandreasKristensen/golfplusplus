#include "core/app.h"
#include "core/render_frame.h"
#include "core/scene_signs.h"
#include "core/vcr_frame.h"
#include "game/model_kinds.h"
#include "renderer/cart_path_mesh.h"
#include "renderer/sky_visibility.h"
#include "renderer/terrain_render_mesh.h"

void app::refresh_render_meshes() {
    // The bus board follows the booking and the clocks the hub's time, so
    // they are checked every frame; the faces are painted again only on a new
    // terrain revision or a new label.
    refresh_scene_hole_signs(hole_signs_, game_, catalog_, text_, lighting_.colors, props_,
                             place_clock_text(text_, game_, shown_sky(), game_.local_unix_ms));
    if (render_meshes_revision_ == game_.terrain_render_revision) {
        return;
    }
    const std::uint64_t revision = game_.terrain_render_revision;
    const scene_colors& colors = lighting_.colors;
    const ground_light_bake light = bake_ground_light(game_.area.ground);
    terrain_render_mesh_ = make_ground_render_mesh(game_.area, revision, colors.ground, light);
    material_overlay_render_mesh_ =
        with_cart_paths(make_terrain_render_mesh({&game_.area.material_overlay}, revision, colors.ground, light, &game_.area, true),
                        game_.area, colors.ground.cart_path, light);
    grass_field_ = bake_grass_field(terrain_render_mesh_, material_overlay_render_mesh_, game_.area.tee_boxes);
    fences_ = build_render_fences(game_.area, revision, props_.at(fence_model).color("post"), game_.tuning.fence.pole_radius);
    water_ = build_render_water(game_.area, revision);
    floodlights_ = build_render_floodlights(game_.area, revision, props_.at(floodlight_model));
    render_meshes_revision_ = revision;
}
