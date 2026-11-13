#include "core/app.h"
#include "core/render_frame.h"
#include "core/tee_book.h"
#include "game/model_kinds.h"
#include "renderer/cart_path_mesh.h"
#include "renderer/sky_visibility.h"
#include "renderer/terrain_render_mesh.h"

void app::refresh_render_meshes() {
    // The bus board follows the booking, so its labels are compared every frame;
    // a rebuild happens only when one changes.
    refresh_render_hole_signs(hole_signs_, game_.area, hole_sign_texts(game_),
                              prop_boards(game_.area, props_, prop_board_labels(game_, catalog_, text_)), text_,
                              lighting_.colors, props_.at(hole_sign_model), hole_signs_.revision + 1);
    if (render_meshes_revision_ == game_.terrain_render_revision) {
        return;
    }
    const std::uint64_t revision = game_.terrain_render_revision;
    const scene_colors& colors = lighting_.colors;
    const ground_light_bake light = bake_ground_light(game_.area.ground);
    terrain_render_mesh_ = make_terrain_render_mesh({&game_.area.ground}, revision, colors.ground, light);
    material_overlay_render_mesh_ =
        with_cart_paths(make_terrain_render_mesh({&game_.area.material_overlay}, revision, colors.ground, light),
                        game_.area, colors.ground.cart_path, light);
    fences_ = build_render_fences(game_.area, revision, props_.at(fence_model).color("post"), game_.tuning.fence.pole_radius);
    water_ = build_render_water(game_.area, revision);
    floodlights_ = build_render_floodlights(game_.area, revision, props_.at(floodlight_model));
    render_meshes_revision_ = revision;
}
