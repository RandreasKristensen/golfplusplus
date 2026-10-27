#include "game/effects.h"

#include <algorithm>
#include <cmath>

bool effect_running(const active_effect& effect, const std::int64_t now_ms) {
    return effect.started_at_ms <= now_ms && now_ms < effect.ends_at_ms;
}

float effective_value(const float base, const std::vector<std::string>& targets, const std::vector<active_effect>& effects,
                      const std::int64_t now_ms) {
    float flat = 0.0f;
    float increased = 0.0f;
    float more = 1.0f;
    for (const active_effect& running : effects) {
        if (!effect_running(running, now_ms) ||
            std::find(targets.begin(), targets.end(), running.effect.target) == targets.end()) {
            continue;
        }
        switch (running.effect.bucket) {
        case effect_bucket::flat:
            flat += running.effect.value;
            break;
        case effect_bucket::increased:
            increased += running.effect.value;
            break;
        case effect_bucket::more:
            more *= 1.0f + running.effect.value;
            break;
        }
    }
    return (base + flat) * (1.0f + increased) * more;
}

float effective_value(const float base, const std::string& target, const std::vector<active_effect>& effects,
                      const std::int64_t now_ms) {
    return effective_value(base, std::vector<std::string>{target}, effects, now_ms);
}

xp_reward effective_reward(const xp_reward& reward, const std::vector<active_effect>& effects, const std::int64_t now_ms) {
    const float xp = effective_value(static_cast<float>(reward.xp),
                                     {effect_target_xp_prefix + reward.skill_id, effect_target_all_xp}, effects, now_ms);
    return xp_reward{reward.skill_id, std::max(0, static_cast<int>(std::lround(xp)))};
}

std::vector<active_effect> running_effects(std::vector<active_effect> effects, const std::int64_t now_ms) {
    effects.erase(std::remove_if(effects.begin(), effects.end(),
                                 [now_ms](const active_effect& effect) { return effect.ends_at_ms <= now_ms; }),
                  effects.end());
    return effects;
}
