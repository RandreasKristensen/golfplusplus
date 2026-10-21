#include "renderer/shadow_casters.h"

#include "renderer/cart_batch.h"
#include "renderer/remote_avatar_batch.h"

void build_shadow_casters(world_marker_batch& batch, const render_data& data, const scene_colors& colors) {
    batch.clear();
    for (const render_cart& cart : data.carts) {
        append_cart_model(batch, cart, data.cart_seat_offset, colors.cart);
    }
    if (data.my_figure) {
        append_remote_avatar(batch, *data.my_figure, data.avatar_eye_height, data.ball_visual_radius_meters, colors);
    }
    for (const render_remote_avatar& avatar : data.remote_avatars) {
        append_remote_avatar(batch, avatar, data.avatar_eye_height, data.ball_visual_radius_meters, colors);
    }
}
