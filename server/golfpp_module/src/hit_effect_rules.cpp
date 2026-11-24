#include "hit_effect_rules.h"

hit_effect_change effects_started_by_hit(const std::vector<running_effect_ref>& rows, const event_effect_definition& event,
                                         const std::int64_t hit_ms) {
    hit_effect_change change;
    const effect_source source = event_source(event.id);
    for (const running_effect_ref& row : rows) {
        if (!(row.effect.source == source)) {
            continue;
        }
        if (row.effect.started_at_ms >= hit_ms) {
            change.remove_ids.push_back(row.id);
        } else if (row.effect.ends_at_ms > hit_ms) {
            change.end_at.emplace_back(row.id, hit_ms);
        }
    }
    change.add = effects_after_event({}, event, hit_ms);
    return change;
}
