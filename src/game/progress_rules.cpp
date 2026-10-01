#include "game/progress_rules.h"

#include <algorithm>
#include <cmath>

namespace {
bool contains(const std::vector<std::string>& values, const std::string& value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

void add_xp(progress_update& update, const std::string& skill_id, const int xp) {
    const add_skill_xp_result result = add_skill_xp(update.progress.skills, skill_id, xp);
    if (result.applied_xp > 0) {
        update.awarded.push_back(awarded_xp{skill_id, result.applied_xp});
    }
}

bool requirement_met(const save_data& progress, const course_world_collectible_requirement& requirement) {
    if (!requirement.skill_id.empty() && skill_level(skill_xp(progress.skills, requirement.skill_id)) < requirement.min_level) {
        return false;
    }
    if (!requirement.required_world_flag.empty() && !contains(progress.world_flags, requirement.required_world_flag)) {
        return false;
    }
    return requirement.required_completed_course_id.empty() ||
        contains(progress.completed_course_ids, requirement.required_completed_course_id);
}
}

progress_update award_xp(const save_data& progress, const xp_reward& reward) {
    progress_update update{progress, {}};
    add_xp(update, reward.skill_id, reward.xp);
    return update;
}

movement_xp_update award_movement_xp(const save_data& progress,
                                     const movement_xp_rate& rate,
                                     const float remainder_meters,
                                     const float meters) {
    movement_xp_update result{{progress, {}}, remainder_meters + std::max(0.0f, meters)};
    if (rate.meters_per_xp <= 0.0f) {
        return result;
    }
    const int xp = static_cast<int>(std::floor(result.remainder_meters / rate.meters_per_xp));
    if (xp > 0) {
        add_xp(result.update, rate.skill_id, xp);
        result.remainder_meters -= static_cast<float>(xp) * rate.meters_per_xp;
    }
    return result;
}

bool collectible_available(const save_data& progress, const course_world_collectible& collectible) {
    if (!requirement_met(progress, collectible.requirement)) {
        return false;
    }
    if (!collectible.repeatable) {
        return !contains(progress.collected_ids, collectible.id);
    }

    const auto it = progress.repeatable_collectibles.find(collectible.id);
    if (it == progress.repeatable_collectibles.end() || !it->second.claimed_at_holes_completed) {
        return true;
    }
    return progress.holes_completed - *it->second.claimed_at_holes_completed >= collectible.repeatable_cooldown_holes;
}

claim_update claim_collectible(const save_data& progress, const course_world_collectible& collectible) {
    claim_update result{false, {progress, {}}};
    if (!collectible_available(progress, collectible)) {
        return result;
    }

    result.claimed = true;
    save_data& next = result.update.progress;
    for (const course_world_skill_reward& reward : collectible.skill_rewards) {
        add_xp(result.update, reward.skill_id, reward.xp);
    }
    if (!collectible.world_flag.empty() && !contains(next.world_flags, collectible.world_flag)) {
        next.world_flags.push_back(collectible.world_flag);
    }
    if (collectible.repeatable) {
        repeatable_collectible_state& state = next.repeatable_collectibles[collectible.id];
        ++state.claim_count;
        state.claimed_at_holes_completed = next.holes_completed;
    } else {
        next.collected_ids.push_back(collectible.id);
    }
    return result;
}

save_data apply_hole_completed(const save_data& progress) {
    save_data next = progress;
    ++next.holes_completed;
    return next;
}

save_data apply_course_completed(const save_data& progress, const std::string& course_id) {
    save_data next = progress;
    if (!course_id.empty() && !contains(next.completed_course_ids, course_id)) {
        next.completed_course_ids.push_back(course_id);
    }
    return next;
}
