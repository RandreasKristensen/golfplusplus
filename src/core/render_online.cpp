#include "core/render_online.h"

#include "core/render_frame.h"

#include "game/group_round.h"
#include "game/mode_dispatch.h"
#include "game/remote_players.h"
#include "game/scorecard.h"
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
constexpr float tag_height_in_cart = 1.55f;

bool in_cart(const motion_mode mode) {
    return mode == motion_mode::cart || mode == motion_mode::drift;
}

float trail_alpha(const remote_shot& shot, const game_tuning& tuning) {
    const float after = shot.elapsed - shot.result.duration;
    if (after <= 0.0f) {
        return tuning.flight_path.alpha;
    }
    const float fade = tuning.net.remote_trail_fade_seconds;
    return fade > 0.0f ? tuning.flight_path.alpha * std::max(0.0f, 1.0f - after / fade) : 0.0f;
}

std::optional<float> emote_elapsed(const emote_state& emote) {
    return emote.active ? std::optional<float>(emote.elapsed) : std::nullopt;
}

// Another player as drawn: their emotes, and while they address their ball
// their club, with them stood at it holding the club.
render_remote_avatar shown_avatar(const game_state& game, const std::uint64_t account, const remote_avatar& avatar,
                                  const room_player& player) {
    render_remote_avatar shown;
    shown.position = avatar.position;
    shown.yaw = avatar.yaw;
    shown.in_cart = in_cart(avatar.mode);
    shown.player_id = account;
    shown.group_id = player.group_id;
    shown.smoke_elapsed = emote_elapsed(avatar.smoke_emote);
    shown.drink_elapsed = emote_elapsed(avatar.drink_emote);
    const std::optional<remote_address> address = shown.in_cart ? std::nullopt : remote_address_pose(game, account);
    if (address) {
        const glm::vec3 ball = drawn_ball_center(address->ball_position, game.tuning.scale);
        shown.swing = render_remote_swing{ball, address->aim_angle, address->club_power};
        const figure_stance stance = figure_address_stance(ball, game.tuning.scale.ball_visual_radius_meters, address->aim_angle);
        shown.position = stance.position;
        shown.yaw = stance.yaw;
    }
    return shown;
}
}

void add_online_render_data(render_data& data, const game_state& game, const text_assets& text) {
    if (game.notice) {
        data.notice_label = lookup_text(text, game.notice->text_key.c_str());
    }
    if (!is_online(game)) {
        return;
    }
    const float eye_height = game.tuning.camera.walking_eye_height;
    data.avatar_eye_height = eye_height;
    for (const auto& [account, avatar] : game.remote_avatars) {
        const auto player = game.online.players.find(account);
        if (player == game.online.players.end()) {
            continue;
        }
        data.remote_avatars.push_back(shown_avatar(game, account, avatar, player->second));
        const float tag_height = (data.remote_avatars.back().in_cart ? tag_height_in_cart : tag_height_on_foot) * eye_height;
        data.name_tags.push_back(render_name_tag{data.remote_avatars.back().position + world_up * tag_height, player->second.name,
                                                 relationship_to(game, player->second)});
    }
    for (const shown_ball& ball : remote_balls(game)) {
        data.remote_balls.push_back(render_remote_ball{drawn_ball_center(ball.position, game.tuning.scale), ball.account_id});
    }
    for (const auto& [account, shot] : game.remote_shots) {
        if (shot.trail.size() >= 2) {
            data.remote_trails.push_back(render_trail{shot.trail, trail_alpha(shot, game.tuning)});
        }
    }
    data.controls.show_group_key = true;
    if (waiting_for_group(game) && data.notice_label.empty()) {
        data.notice_label = lookup_text(text, text_scorecard_waiting_for_group);
    }
    if (data.show_scorecard || data.show_course_results) {
        data.group_scorecard = build_group_scorecard(game, text.strings);
    }
}
