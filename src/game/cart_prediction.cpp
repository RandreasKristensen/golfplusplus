#include "game/golf_cart.h"

#include "game/game_state.h"
#include "game/movement_step.h"
#include "game/world_sounds.h"

void step_predicted_cart(game_state& state, const cart_controls& controls, const cart_spec& spec, const float grip_scale,
                  const std::vector<solid>& others, frame_profile* profile) {
    cart_state& cart = state.cart;
    const cart_move moved = step_cart_mover(cart_mover{cart.pose, cart.motion, cart.vertical, cart.against_obstacle}, controls,
                                            cart_world{state.area, state.world.cart_roads, state.tuning, spec,
                                                       state.weather.wetness, grip_scale, others, profile});
    cart.pose = moved.after.pose;
    cart.motion = moved.after.motion;
    cart.vertical = moved.after.vertical;
    cart.against_obstacle = moved.after.against_obstacle;
    if (moved.crash_speed >= state.tuning.cart.crash_sound_speed) {
        push_audio_event(state, audio_event_type::cart_crash, cart.pose.position);
    }
}
