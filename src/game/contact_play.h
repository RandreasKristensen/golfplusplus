#pragma once

// The online client's side of contacts with balls in flight (see
// contact_prediction.h): my own steps logged as the server will have them, a
// contact I predict from them shown at once on the flight playing here (mine or
// another's), and whether a ball in flight passes near enough for movement to
// go at net.contact_send_interval_seconds (game/motion_sync.h). Presentation
// only: it changes the ball drawn, never a result or an effect.

#include <cstdint>

struct game_state;

// Online: logs where I stand (my feet, or my cart when I drive it) at `time_ms`
// on the shared clock. Called after each fixed step and each frame.
void note_my_path(game_state& state, std::int64_t time_ms);

// Online, each frame: finds flights playing here that my path meets, shows each
// with my predicted contact (sound and bounce, blending to it), retracts one the
// server has not confirmed in time, and tells motion_sync_state::near_flight.
void update_contact_prediction(game_state& state);
