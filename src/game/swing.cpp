#include "game/swing.h"

#include <algorithm>

swing_tempo swing_tempo_for(const club_definition& club, const swing_tuning& tuning) {
    if (club.club_class == "putter") {
        return swing_tempo{tuning.putt_backswing_seconds, tuning.putt_downswing_seconds};
    }
    return swing_tempo{tuning.backswing_seconds, tuning.downswing_seconds};
}

float swing_impact_seconds(const swing_tempo& tempo) {
    return tempo.back_seconds + tempo.down_seconds;
}

float swing_club_raise(const float elapsed, const float backswing, const swing_tempo& tempo) {
    if (elapsed <= 0.0f) {
        return 0.0f;
    }
    if (elapsed < tempo.back_seconds) {
        // Easing out of address and into the top.
        const float t = elapsed / tempo.back_seconds;
        return backswing * t * t * (3.0f - 2.0f * t);
    }
    const float down = tempo.down_seconds > 0.0f ? std::min(1.0f, (elapsed - tempo.back_seconds) / tempo.down_seconds) : 1.0f;
    // Gathering speed all the way down.
    return backswing * (1.0f - down * down);
}
