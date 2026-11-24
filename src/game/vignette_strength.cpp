#include "game/vignette_strength.h"

#include "physics/vector_math.h"

float vignette_strength(const std::int64_t started_at_ms, const std::int64_t now_ms, const float seconds) {
    if (now_ms < started_at_ms || !(seconds > 0.0f)) {
        return 0.0f;
    }
    const float elapsed = static_cast<float>(now_ms - started_at_ms) / 1000.0f;
    return 1.0f - clamp01(elapsed / seconds);
}

std::vector<vignette_look> running_vignettes(const std::vector<active_effect>& effects, const effect_catalog& catalog,
                                             const std::int64_t now_ms) {
    std::vector<vignette_look> looks;
    for (const auto& [id, event] : catalog.effects) {
        if (!event.vignette) {
            continue;
        }
        const std::optional<std::int64_t> started = event_started_at(effects, id, now_ms);
        const float strength = started ? vignette_strength(*started, now_ms, event.vignette->seconds) : 0.0f;
        if (strength > 0.0f) {
            looks.push_back(vignette_look{event.vignette->colour, strength});
        }
    }
    return looks;
}
