#include "core/render_putting.h"

#include "game/green_read.h"
#include "game/rangefinder.h"
#include "game/text_ids.h"
#include "physics/vector_math.h"

#include <algorithm>
#include <string>
#include <vector>

namespace {
// The count rising and fading as a shot's number does, bigger as each
// pace lands.
render_shot_number pace_number_row(const game_state& game, const pace_count& count, const text_assets& text) {
    const shot_number_tuning& tuning = game.tuning.shot_numbers;
    const pacing_tuning& pacing = game.tuning.pacing;
    const float units = 1.0f / std::max(0.0001f, game.tuning.scale.meters_per_world_unit);
    const float progress = clamp01(count.age / std::max(0.001f, tuning.lifetime_seconds));
    render_shot_number row;
    row.position = first_person_eye(game) + yaw_direction(view_yaw(game)) * (pacing.popup_ahead_meters * units) -
        world_up * (pacing.popup_drop_meters * units);
    row.label = format_text(text, text_hud_paces, {{"paces", std::to_string(count.paces)}});
    row.half_height = tuning.half_height * (count.age < tuning.crit_pop_seconds ? tuning.crit_pop_size : 1.0f);
    row.rise = tuning.rise * progress;
    const float fade_from = 1.0f - tuning.fade_share;
    row.alpha = progress > fade_from ? clamp01((1.0f - progress) / std::max(0.001f, tuning.fade_share)) : 1.0f;
    return row;
}
}

void add_putting_read(render_data& data, const game_state& game, const text_assets& text) {
    if (game.pace_count) {
        data.pace_number = pace_number_row(game, *game.pace_count, text);
    }
    if (game.crouch <= 0.0f) {
        return;
    }
    std::vector<glm::vec3> keep_clear;
    if (data.show_ball) {
        keep_clear.push_back(data.ball_position);
    }
    if (data.show_hole) {
        keep_clear.push_back(data.pin_position);
    }
    for (const green_read_dot& dot : green_read_dots(game.area, game.player.position, game.green_read_seconds, keep_clear,
                                                     game.tuning.green_read, game.tuning.scale)) {
        data.green_read_dots.push_back(render_read_dot{dot.position, dot.alpha * game.crouch});
    }
}
