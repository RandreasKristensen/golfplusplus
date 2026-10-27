#include "core/render_online.h"

#include "core/render_frame.h"

#include "game/group_round.h"
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

// The club in another player's hand, as their bag's row has it, else the bag's first.
std::string shown_club_model(const game_state& game, const std::uint64_t account) {
    const auto bag = game.online.bags.find(account);
    const club_definition* club = bag != game.online.bags.end() ? find_club(game.clubs, bag->second.club_in_hand) : nullptr;
    if (club == nullptr && !game.clubs.empty()) {
        club = &game.clubs.front();
    }
    return club != nullptr ? club->model : std::string();
}

// Another player as drawn: what they smoke and drink, their bag on their
// back when it is, and the club in their hand: carried, and while they
// address their ball held at it, with them stood to it in their hand.
render_remote_avatar shown_avatar(const game_state& game, const std::uint64_t account, const remote_avatar& avatar,
                                  const room_player& player) {
    render_remote_avatar shown;
    shown.position = avatar.position;
    shown.yaw = avatar.yaw;
    shown.seated = in_cart(avatar.mode);
    shown.at_wheel = avatar.mode == motion_mode::cart;
    shown.player_id = account;
    shown.group_id = player.group_id;
    shown.smoking = shown_use(game, avatar.smoking);
    shown.drinking = shown_use(game, avatar.drinking);
    shown.left_handed = player.hand == handedness::left;
    shown.carried_club_model = shown_club_model(game, account);
    if (const std::optional<shown_bag> bag = remote_bag(game, account); bag && bag->place.where == bag_where::on_back) {
        shown.bag_model = bag->model;
    }
    const std::optional<remote_address> address = shown.seated ? std::nullopt : remote_address_pose(game, account);
    if (address) {
        // Their face and ball position aren't seen until the shot: they
        // stand to their aim at the stock face and ball position.
        shot_setup setup;
        setup.ball_start = address->ball_position;
        setup.aim_angle = address->aim_angle;
        setup.hand = player.hand;
        shown.swing = club_pose_at(game, setup, address->club_raise, shown_club_model(game, account));
        shown.position = address_stance_position(game.area, setup, game.tuning);
        shown.yaw = address_facing_yaw(setup);
    }
    return shown;
}
}

std::optional<shown_bag> remote_bag(const game_state& game, const std::uint64_t account) {
    const auto bag = is_online(game) ? game.online.bags.find(account) : game.online.bags.end();
    if (bag == game.online.bags.end()) {
        return std::nullopt;
    }
    const auto equipped = game.online.loadouts.find(account);
    const item_definition* item =
        game.items.find(equipped != game.online.loadouts.end() ? equipped->second.bag : game.default_loadout.bag);
    if (item == nullptr) {
        item = game.items.find(game.default_loadout.bag);
    }
    return item != nullptr ? std::optional<shown_bag>(shown_bag{bag->second.place, item->model}) : std::nullopt;
}

void add_online_render_data(render_data& data, const game_state& game, const text_assets& text) {
    if (game.notice) {
        data.notice_label = lookup_text(text, game.notice->text_key.c_str());
    }
    if (!is_online(game)) {
        return;
    }
    const float eye_height = game.tuning.camera.walking_eye_height;
    for (const auto& [account, avatar] : game.remote_avatars) {
        const auto player = game.online.players.find(account);
        if (player == game.online.players.end()) {
            continue;
        }
        data.remote_avatars.push_back(shown_avatar(game, account, avatar, player->second));
        const float tag_height = (data.remote_avatars.back().seated ? tag_height_seated : tag_height_on_foot) * eye_height;
        data.name_tags.push_back(render_name_tag{data.remote_avatars.back().position + world_up * tag_height, player->second.name,
                                                 relationship_to(game, player->second)});
    }
    for (const auto& [account, bag] : game.online.bags) {
        const std::optional<shown_bag> shown = account != game.online.account_id ? remote_bag(game, account) : std::nullopt;
        if (shown && shown->place.where == bag_where::on_ground) {
            data.ground_bags.push_back(render_ground_bag{shown->model, shown->place.position, shown->place.yaw});
        }
    }
    for (const shown_ball& ball : remote_balls(game)) {
        data.remote_balls.push_back(render_remote_ball{drawn_ball_center(ball.position, game.tuning.scale), ball.account_id});
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
