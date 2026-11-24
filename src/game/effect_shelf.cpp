#include "game/effect_shelf.h"

#include <algorithm>
#include <utility>

namespace {
// The running effects of one source.
struct source_group {
    effect_source source;
    std::vector<item_effect> effects;
    std::int64_t latest_start_ms = 0;
    std::int64_t latest_end_ms = 0;
};

std::vector<source_group> group_by_source(const std::vector<active_effect>& effects, const std::int64_t now_ms) {
    std::vector<source_group> groups;
    for (const active_effect& running : effects) {
        if (!effect_running(running, now_ms)) {
            continue;
        }
        auto group = std::find_if(groups.begin(), groups.end(),
                                  [&running](const source_group& each) { return each.source == running.source; });
        if (group == groups.end()) {
            groups.push_back(source_group{running.source, {}, running.started_at_ms, running.ends_at_ms});
            group = groups.end() - 1;
        }
        group->effects.push_back(running.effect);
        group->latest_start_ms = std::max(group->latest_start_ms, running.started_at_ms);
        group->latest_end_ms = std::max(group->latest_end_ms, running.ends_at_ms);
    }
    return groups;
}

std::optional<int> seconds_until(const std::int64_t end_ms, const std::int64_t now_ms) {
    if (end_ms == effect_never_ends) {
        return std::nullopt;
    }
    return static_cast<int>((end_ms - now_ms + 999) / 1000);
}
}

std::vector<effect_square> effect_shelf(const std::vector<active_effect>& effects, const item_catalog& items,
                                        const effect_catalog& events, const std::int64_t now_ms) {
    std::vector<source_group> groups = group_by_source(effects, now_ms);
    std::stable_sort(groups.begin(), groups.end(),
                     [](const source_group& a, const source_group& b) { return a.latest_start_ms > b.latest_start_ms; });

    std::vector<effect_square> squares;
    for (source_group& group : groups) {
        effect_square square;
        if (group.source.kind == effect_source_kind::item) {
            const item_definition* item = items.find(group.source.id);
            if (item == nullptr) {
                continue;
            }
            square.icon = item_kind_name(item->kind);
            square.name = item->name;
        } else {
            const event_effect_definition* event = events.find(group.source.id);
            if (event == nullptr) {
                continue;
            }
            square.icon = event->icon;
            square.harmful = event->harmful;
            square.name = event->name;
            square.description = event->description;
        }
        square.source = std::move(group.source);
        square.effects = std::move(group.effects);
        square.seconds_left = seconds_until(group.latest_end_ms, now_ms);
        squares.push_back(std::move(square));
    }
    return squares;
}
