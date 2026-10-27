#include "renderer/shadow_casters.h"

#include "renderer/bag_batch.h"
#include "renderer/remote_avatar_batch.h"

void build_shadow_casters(world_marker_batch& figures, prop_batch& models, const render_data& data, const scene_colors& colors,
                          const prop_catalog& props) {
    figures.clear();
    models.clear();
    append_carts_and_bags(models, data, props, colors.glow);
    if (data.my_figure) {
        append_remote_avatar(figures, *data.my_figure, data.avatar_eye_height, data.ball_visual_radius_meters, colors);
        append_figure_gear(models, *data.my_figure, data.avatar_eye_height, props, colors.glow);
    }
    for (const render_remote_avatar& avatar : data.remote_avatars) {
        append_remote_avatar(figures, avatar, data.avatar_eye_height, data.ball_visual_radius_meters, colors);
        append_figure_gear(models, avatar, data.avatar_eye_height, props, colors.glow);
    }
}
