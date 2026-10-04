#include "core/render_online.h"

#include "game/mode_dispatch.h"
#include "game/remote_players.h"
#include "game/scorecard.h"
#include "physics/vector_math.h"

#include <algorithm>

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
        const bool cart = in_cart(avatar.mode);
        data.remote_avatars.push_back(render_remote_avatar{avatar.position, avatar.yaw, cart, account, player->second.group_id});
        const float tag_height = (cart ? tag_height_in_cart : tag_height_on_foot) * eye_height;
        data.name_tags.push_back(render_name_tag{avatar.position + world_up * tag_height, player->second.name});
    }
    for (const shown_ball& ball : remote_balls(game)) {
        data.remote_balls.push_back(render_remote_ball{ball.position, ball.account_id});
    }
    for (const auto& [account, shot] : game.remote_shots) {
        if (shot.trail.size() >= 2) {
            data.remote_trails.push_back(render_trail{shot.trail, trail_alpha(shot, game.tuning)});
        }
    }
    data.controls.show_group_key = true;
    if (data.show_scorecard) {
        data.group_scorecard = build_group_scorecard(game, text.strings);
    }
}
