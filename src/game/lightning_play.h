#pragma once

// The offline game's lightning: each lightning.check_seconds of the shared
// clock the pure rule (game/lightning_rules.h) rolls for me alone, from a
// session seed (deterministic given it, not secret). A strike shows its bolt
// (game_state::strike_points) and dazes me (the dazed event effect) or knocks
// me out (game/knockout.h), the only knockout offline. Online the server rolls.

struct game_state;

// The god menu's STRIKE ME: lightning strikes me now, dazing me or knocking me
// out. Offline it goes through the same strike a check's roll does; online it
// is the god_strike command, which the server applies as it does a real one
// (only where the server allows it).
void strike_me(game_state& state, bool knock_out);

// Called every frame the offline world plays; does work once a check passes.
void update_offline_lightning(game_state& state);
