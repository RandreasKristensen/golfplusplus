#include "core/render_online.h"

#include "core/render_frame.h"
#include "core/render_gear.h"

#include "game/address_view.h"
#include "game/gear_paint.h"
#include "game/group_round.h"
#include "game/loadout.h"
#include "game/social_dap.h"
#include "renderer/dap_pose.h"
#include "game/mode_dispatch.h"
#include "game/remote_players.h"
#include "game/text_ids.h"
#include "physics/vector_math.h"
#include "renderer/remote_avatar_batch.h"

#include <algorithm>
#include <cstdint>
#include <optional>

namespace {
// How far above the feet a name tag floats: over the head on foot, over
// the canopy in a cart (in eye heights).
constexpr float tag_height_on_foot = 1.30f;
constexpr float tag_height_seated = 1.55f;

float trail_alpha(const remote_shot& shot, const game_tuning& tuning) {
    const float after = shot.elapsed - shot.result.duration;
    if (after <= 0.0f) {
        return tuning.flight_path.alpha;
    }
    const float fade = tuning.net.remote_trail_fade_seconds;
    return fade > 0.0f ? tuning.flight_path.alpha * std::max(0.0f, 1.0f - after / fade) : 0.0f;
}

std::optional<render_item_use> shown_use(const game_state& game, const std::optional<item_use_shown>& use) {
    const item_definition* item = use ? game.items.find(use->item) : nullptr;
    return item != nullptr ? std::optional<render_item_use>(render_item_use{item->model, use->elapsed}) : std::nullopt;
}

// The club in another player's hand, as their bag's row has it; none before the row.
std::string shown_club_model(const game_state& game, const std::uint64_t account) {
    const auto bag = game.online.bags.find(account);
    const club_definition* club = bag != game.online.bags.end() ? find_club(game.clubs, bag->second.club_in_hand) : nullptr;
    return club != nullptr ? club->model : std::string();
}

// Another player as drawn: what they smoke and drink, their bag on their
// back when it is, and the club in their hand: carried, and while they
// address their ball held at it, with them stood to it in their hand.
render_remote_avatar shown_avatar(const game_state& game, const std::uint64_t account, const remote_avatar& avatar,
                                  const room_player& player, const prop_catalog& props) {
    render_remote_avatar shown;
    shown.position = avatar.position;
    shown.yaw = avatar.yaw;
    shown.seated = in_cart(avatar.mode);
    shown.at_wheel = avatar.mode == motion_mode::cart;
    shown.player_id = account;
    shown.group_id = player.group_id;
    shown.smoking = shown_use(game, avatar.smoking);
    shown.drinking = shown_use(game, avatar.drinking);
    shown.crouch = avatar.crouch;
    shown.left_handed = player.hand == handedness::left;
    shown.carried_club_model = shown_club_model(game, account);
    const auto bag_row = game.online.bags.find(account);
    if (const club_definition* club = bag_row != game.online.bags.end() ? find_club(game.clubs, bag_row->second.club_in_hand) : nullptr) {
        shown.carried_club_loft_degrees = club->spec.static_loft_degrees;
    }
    if (const std::optional<shown_bag> bag = remote_bag(game, account); bag && bag->place.where == bag_where::on_back) {
        shown.bag_model = bag->model;
        shown.bag_paint = gear_paint_of(game, account);
    }
    const std::optional<remote_address> address = shown.seated ? std::nullopt : remote_address_pose(game, account);
    if (address) {
        const address_stance stance = stance_at_address(game.area, address->setup, game.tuning);
        shown.swing = club_pose_at(game, address->setup, address->club_raise, shown_club_model(game, account), props);
        shown.position = stance.position;
        shown.yaw = stance.facing;
    }
    return shown;
}
}

std::optional<shown_bag> remote_bag(const game_state& game, const std::uint64_t account) {
    const auto bag = is_online(game) ? game.online.bags.find(account) : game.online.bags.end();
    const auto equipped = game.online.loadouts.find(account);
    if (bag == game.online.bags.end() || equipped == game.online.loadouts.end()) {
        return std::nullopt;
    }
    // An empty slot is the default bag, as their own game has it.
    const item_definition* item = game.items.find(fitted_loadout(equipped->second, game.default_loadout, game.items).bag);
    return item != nullptr ? std::optional<shown_bag>(shown_bag{bag->second.place, item->model}) : std::nullopt;
}

void add_online_render_data(render_data& data, const game_state& game, const text_assets& text, const prop_catalog& props) {
    if (game.notice) {
        data.notice_label = lookup_text(text, game.notice->text_key.c_str());
    }
    if (!is_online(game)) {
        return;
    }
    if (const auto pair = active_dap(game, game.online.account_id); pair && game.mode == game_mode::walking && !game.shot) {
        data.dap_hand = paired_dap_pose(*pair, game.online.account_id, game.hand, game.online.server_now, game.tuning);
        if (data.my_figure) {
            data.my_figure->player_id = game.online.account_id;
            data.my_figure->dap = data.dap_hand; data.my_figure->reach = data.dap_hand->hand;
            data.my_figure->yaw = data.dap_hand->yaw; data.my_figure->carried_club_model.clear();
        }
    }
    const float eye_height = game.tuning.camera.walking_eye_height;
    for (const auto& [account, avatar] : game.remote_avatars) {
        const auto player = game.online.players.find(account);
        if (player == game.online.players.end()) {
            continue;
        }
        data.remote_avatars.push_back(shown_avatar(game, account, avatar, player->second, props));
        if (avatar.handling && !data.remote_avatars.back().seated && !active_dap(game, account)) {
            add_handling(data, data.remote_avatars.back(), game, *avatar.handling, account, bag_model_of(game, account), props);
        }
        if (const auto pair = active_dap(game, account)) {
            auto& shown = data.remote_avatars.back();
            shown.dap = paired_dap_pose(*pair, account, player->second.hand, game.online.server_now, game.tuning);
            shown.position = account == pair->first ? pair->first_position : pair->second_position;
            shown.yaw = shown.dap->yaw; shown.reach = shown.dap->hand; shown.crouch = 0.0f;
            shown.seated = false; shown.at_wheel = false;
            shown.smoking.reset(); shown.drinking.reset();
            shown.swing.reset(); shown.carried_club_model.clear();
        }
        const float tag_height = (data.remote_avatars.back().seated ? tag_height_seated : tag_height_on_foot) * eye_height;
        data.name_tags.push_back(render_name_tag{data.remote_avatars.back().position + world_up * tag_height, player->second.name,
                                                 relationship_to(game, player->second)});
    }
    for (const auto& [account, bag] : game.online.bags) {
        const std::optional<shown_bag> shown = account != game.online.account_id ? remote_bag(game, account) : std::nullopt;
        const auto avatar = game.remote_avatars.find(account);
        const bool moving = avatar != game.remote_avatars.end() && avatar->second.handling &&
            avatar->second.handling->kind == handling_kind::bag;
        if (shown && shown->place.where == bag_where::on_ground && !moving) {
            data.ground_bags.push_back(
                render_ground_bag{shown->model, shown->place.position, shown->place.yaw, gear_paint_of(game, account)});
        }
    }
    for (const shown_ball& ball : remote_balls(game)) {
        data.remote_balls.push_back(render_remote_ball{drawn_ball_center(ball.position, game.tuning.scale, game.area.cups), ball.account_id});
    }
    for (const auto& [account, shot] : game.remote_shots) {
        if (shot.trail.size() >= 2) {
            data.remote_trails.push_back(render_trail{shot.trail, trail_alpha(shot, game.tuning)});
        }
    }
    if (waiting_for_group(game) && data.notice_label.empty()) {
        data.notice_label = lookup_text(text, text_scorecard_waiting_for_group);
    }
}
