#pragma once

#include <optional>
#include <string>
#include <vector>

struct quest_choice {
    std::string label;
    std::string next;
};

struct quest_step {
    std::string id;
    std::string text;
    std::vector<quest_choice> choices;
};

struct quest_reward {
    int money = 0;
    std::optional<std::string> unlock;
};

struct quest_definition {
    std::string id;
    std::string title;
    std::vector<quest_step> steps;
    quest_reward reward;
};
