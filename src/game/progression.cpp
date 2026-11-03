#include "game/progression.h"

#include <algorithm>
#include <limits>

int level_at(const level_curve& curve, const std::int64_t xp) {
    const auto reached = std::upper_bound(curve.level_xp.begin(), curve.level_xp.end(), xp);
    return std::max(1, static_cast<int>(reached - curve.level_xp.begin()));
}

std::int64_t xp_to_next_level(const level_curve& curve, const std::int64_t xp) {
    const std::size_t level = static_cast<std::size_t>(level_at(curve, xp));
    return level >= curve.level_xp.size() ? 0 : curve.level_xp[level] - xp;
}

std::int64_t skill_xp(const skill_progression& progression, const std::string& skill_id) {
    const auto it = progression.find(skill_id);
    return it == progression.end() ? 0 : std::max<std::int64_t>(0, it->second.xp);
}

add_skill_xp_result add_skill_xp(skill_progression& progression, const std::string& skill_id, const std::int64_t amount) {
    add_skill_xp_result result;
    result.before_xp = skill_xp(progression, skill_id);
    result.after_xp = result.before_xp;
    if (skill_id.empty() || amount <= 0) {
        return result;
    }
    constexpr std::int64_t most = std::numeric_limits<std::int64_t>::max();
    result.after_xp = amount > most - result.before_xp ? most : result.before_xp + amount;
    progression[skill_id].xp = result.after_xp;
    result.applied_xp = result.after_xp - result.before_xp;
    return result;
}
