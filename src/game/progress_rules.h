#pragma once

// Pure rules over the progress record (save_data): no game_state, no I/O.
// Offline play applies them to the local save; the server applies the same
// functions to its own copy.

#include "game/course_world_definition.h"
#include "game/effects.h"
#include "game/reward_rules.h"
#include "game/save_data.h"

#include <cstdint>
#include <string>
#include <vector>

struct awarded_xp {
    std::string skill_id;
    int xp = 0;  // actually applied (0 at the cap)
};

struct progress_update {
    save_data progress;
    std::vector<awarded_xp> awarded;
};

// What changes the XP earned now: the item effects running (game/effects.h),
// at `now_ms` on the shared clock. Every XP award goes through them.
struct xp_effects {
    std::vector<active_effect> effects;
    std::int64_t now_ms = 0;
};

progress_update award_xp(const save_data& progress, const xp_reward& reward, const xp_effects& boosts);

// Movement XP is earned per `rate.meters_per_xp` meters; the leftover meters
// carry over in `remainder_meters`.
struct movement_xp_update {
    progress_update update;
    float remainder_meters = 0.0f;
};
movement_xp_update award_movement_xp(const save_data& progress,
                                     const movement_xp_rate& rate,
                                     float remainder_meters,
                                     float meters,
                                     const xp_effects& boosts);

bool collectible_available(const save_data& progress, const course_world_collectible& collectible);

// The rangefinder is an item (game/rangefinder.h); everything is unlocked today.
bool owns_rangefinder(const save_data& progress);

struct claim_update {
    bool claimed = false;  // false when the collectible is not available
    progress_update update;
};
claim_update claim_collectible(const save_data& progress, const course_world_collectible& collectible,
                               const xp_effects& boosts);

// Advances the hole counter that collectible cooldowns use.
save_data apply_hole_completed(const save_data& progress);
save_data apply_course_completed(const save_data& progress, const std::string& course_id);
