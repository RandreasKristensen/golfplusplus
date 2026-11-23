#include "renderer/prop_frame.h"

#include "renderer/bag_batch.h"
#include "renderer/dap_pose.h"
#include "renderer/held_props.h"
#include "renderer/remote_avatar_batch.h"
#include "renderer/swing_club.h"
#include "renderer/water_reflection.h"

namespace {
const prop_model* find_prop(const prop_catalog& props, const std::string& id) {
    const auto found = props.find(id);
    return found != props.end() ? &found->second : nullptr;
}

// What an avatar swings and holds, drawn in front of or beside it.
void append_in_hand(prop_batch& batch, const render_remote_avatar& avatar, const render_data& data, const prop_catalog& props,
                    const scene_colors& colors) {
    if (avatar.swing) {
        if (const prop_model* club = find_prop(props, avatar.swing->club_model)) {
            append_swing_club(batch, *avatar.swing, data.ball_visual_radius_meters, *club, colors.glow);
        }
    }
    if (const std::optional<prop_holder> holder = remote_prop_holder(avatar, data.avatar_eye_height, props)) {
        append_held_props(batch, *holder, colors.glow);
    }
}
}

void prop_frame::close(const group finished) {
    for (const bool blended : {false, true}) {
        for (const prop_mesh mesh : {prop_mesh::cylinder, prop_mesh::sphere, prop_mesh::quad}) {
            ends_[finished][blended ? 1 : 0][static_cast<std::size_t>(mesh)] = batch_.instances(blended, mesh).size();
        }
    }
}

void prop_frame::build(const render_data& data, const prop_catalog& props, const scene_colors& colors, const float door_progress) {
    batch_.clear();
    append_carts_and_bags(batch_, data, props, colors);
    close(carts);
    for (const render_remote_avatar& avatar : data.remote_avatars) {
        append_figure_gear(batch_, avatar, data.avatar_eye_height, props, colors);
    }
    close(others_gear);
    if (data.world_props) { append_bus_doors(batch_, *data.world_props, props, colors.glow, door_progress); }
    close(doors);
    for (const render_remote_avatar& avatar : data.remote_avatars) {
        append_in_hand(batch_, avatar, data, props, colors);
    }
    close(others_held);
    if (data.dap_hand) {
        append_dap_hand(batch_, *data.dap_hand, colors.figures.skin,
                        remote_tint(colors.figures, data.my_figure ? data.my_figure->player_id : 0));
    }
    if (data.my_club) {
        if (const prop_model* club = find_prop(props, data.my_club->club_model)) {
            append_swing_club(batch_, *data.my_club, data.ball_visual_radius_meters, *club, colors.glow);
        }
    }
    close(mine_in_view);
    if (data.my_figure) {
        append_figure_gear(batch_, *data.my_figure, data.avatar_eye_height, props, colors);
    }
    close(mine_gear);
    if (data.my_figure) {
        append_in_hand(batch_, mirrored_figure(data), data, props, colors);
    }
    close(mine_in_mirror);
}

prop_range prop_frame::span(const group first, const group last, const bool blended, const std::size_t mesh) const {
    const std::size_t list = blended ? 1 : 0;
    const std::size_t from = first == 0 ? 0 : ends_[first - 1][list][mesh];
    return prop_range{from, ends_[last][list][mesh] - from};
}

prop_ranges prop_frame::ranges(const prop_view view, const bool blended) const {
    prop_ranges out;
    for (std::size_t mesh = 0; mesh < prop_mesh_count; ++mesh) {
        const auto add = [&](const prop_range range) {
            if (range.count > 0) { out[mesh].run[static_cast<std::size_t>(out[mesh].count++)] = range; }
        };
        switch (view) {
        case prop_view::scene:
            add(span(carts, mine_in_view, blended, mesh));
            break;
        case prop_view::mirror:
            add(span(carts, others_held, blended, mesh));
            add(span(mine_gear, mine_in_mirror, blended, mesh));
            break;
        case prop_view::sun_shadow:
            add(span(carts, others_gear, blended, mesh));
            add(span(mine_gear, mine_gear, blended, mesh));
            break;
        }
    }
    return out;
}
