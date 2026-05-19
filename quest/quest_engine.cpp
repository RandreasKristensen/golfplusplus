#include "quest/quest_engine.h"

#include <algorithm>

namespace {
const quest_step* find_step(const quest_definition& quest, const std::string& step_id) {
    for (const quest_step& step : quest.steps) {
        if (step.id == step_id) {
            return &step;
        }
    }
    return nullptr;
}

bool contains_string(const std::vector<std::string>& values, const std::string& value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}
}

quest_session start_quest(const quest_definition& quest) {
    quest_session session;
    session.quest = quest;
    if (!quest.steps.empty()) {
        session.current_step_id = quest.steps.front().id;
    }
    session.completed = quest.steps.empty();
    return session;
}

const quest_step* current_quest_step(const quest_session& session) {
    if (session.completed) {
        return nullptr;
    }
    return find_step(session.quest, session.current_step_id);
}

quest_outcome advance_quest(quest_session& session, const int choice_index) {
    quest_outcome outcome;
    if (session.completed) {
        return outcome;
    }

    const quest_step* step = current_quest_step(session);
    if (step == nullptr || choice_index < 0 || choice_index >= static_cast<int>(step->choices.size())) {
        return outcome;
    }

    const quest_choice& choice = step->choices[static_cast<std::size_t>(choice_index)];
    if (choice.next == "END") {
        session.completed = true;
        outcome.completed = true;
        outcome.money = session.quest.reward.money;
        outcome.unlock = session.quest.reward.unlock;
        return outcome;
    }

    if (find_step(session.quest, choice.next) == nullptr) {
        return outcome;
    }
    session.current_step_id = choice.next;
    return outcome;
}

bool apply_quest_completion_once(save_data& save, quest_session& session, const quest_outcome& outcome) {
    if (!outcome.completed || !session.completed || session.reward_applied) {
        return false;
    }

    if (contains_string(save.completed_quest_ids, session.quest.id)) {
        session.reward_applied = true;
        return false;
    }

    save.money = std::max(0, save.money + outcome.money);
    if (outcome.unlock && !contains_string(save.unlocked_items, *outcome.unlock)) {
        save.unlocked_items.push_back(*outcome.unlock);
    }
    save.completed_quest_ids.push_back(session.quest.id);
    session.reward_applied = true;
    return true;
}
