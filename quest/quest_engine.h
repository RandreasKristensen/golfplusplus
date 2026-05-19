#pragma once

#include "game/save_data.h"
#include "quest/quest_definition.h"

#include <optional>
#include <string>

struct quest_session {
    quest_definition quest;
    std::string current_step_id;
    bool completed = false;
    bool reward_applied = false;
};

struct quest_outcome {
    bool completed = false;
    int money = 0;
    std::optional<std::string> unlock;
};

quest_session start_quest(const quest_definition& quest);
const quest_step* current_quest_step(const quest_session& session);
quest_outcome advance_quest(quest_session& session, int choice_index);
bool apply_quest_completion_once(save_data& save, quest_session& session, const quest_outcome& outcome);
