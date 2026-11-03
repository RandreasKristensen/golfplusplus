#include "game/world_sounds.h"

#include "game/cart_seats.h"
#include "game/golf_cart.h"
#include "physics/vector_math.h"

#include <algorithm>
#include <cmath>

namespace {
// Slower than this a cart's motor is still.
constexpr float cart_engine_idle_speed = 0.05f;
}

void push_audio_event(game_state& state, const audio_event_type type, const glm::vec3& position) {
    audio_event event;
    event.type = type;
    event.position = position;
    state.audio_events.push_back(event);
}

glm::vec3 mouth_position(const game_tuning& tuning, const glm::vec3& standing) {
    return standing + world_up * tuning.camera.walking_eye_height;
}

void push_item_use(game_state& state, const item_definition& item, const glm::vec3& standing) {
    audio_event sound;
    sound.type = audio_event_type::item_use;
    sound.sound = item.use->sound;
    sound.position = mouth_position(state.tuning, standing);
    state.audio_events.push_back(sound);
}

void push_club_hit(game_state& state, const club_definition& club, const shot_result& result, const glm::vec3& ball_start) {
    const bool pure = result.strike_centredness >= state.tuning.swing.pure_strike_centredness;
    audio_event hit;
    hit.type = audio_event_type::club_hit;
    hit.sound = pure ? club.hit_sound : club.mishit_sound;
    hit.position = ball_start;
    state.audio_events.push_back(hit);
}

std::size_t push_shot_events(game_state& state, const shot_result& result, const glm::vec3& correction, std::size_t next_event,
                             const float elapsed) {
    const std::vector<shot_event>& events = result.events;
    for (; next_event < events.size() && events[next_event].time <= elapsed; ++next_event) {
        const shot_event& event = events[next_event];
        const float progress = result.duration > 0.0f ? std::min(1.0f, event.time / result.duration) : 1.0f;
        audio_event sound;
        sound.type = event.kind == shot_event_kind::land ? audio_event_type::ball_land : audio_event_type::ball_hit;
        sound.hit = event.kind;
        sound.material = event.material;
        sound.position = shot_position_at(result, event.time) + correction * progress;
        state.audio_events.push_back(sound);
    }
    return next_event;
}

std::vector<running_motor> running_motors(const game_state& state) {
    std::vector<running_motor> motors;
    const cart_tuning& tuning = state.tuning.cart;
    for (const cart_in_world& cart : carts_in_world(state)) {
        if (!cart.driven || std::abs(cart.speed) <= cart_engine_idle_speed) {
            continue;
        }
        float pitch = 0.0f;
        if (cart.owner == 0) {
            pitch = motor_pitch(state);
        } else {
            // Others' load isn't sent: their motor by its speed alone.
            const float top_speed = std::max(game_top_speed(cart_spec_of(state, cart.item), tuning), 0.1f);
            pitch = motor_pitch_at(tuning, std::abs(cart.speed) * state.tuning.scale.meters_per_world_unit / top_speed, 0.0f);
        }
        motors.push_back(running_motor{cart.owner, cart.pose.position, pitch});
    }
    return motors;
}
