#pragma once

// A scored course manifest. Environment belongs to place_definition; only
// courses have hole files and a round.
#include "game/place_definition.h"

struct course_definition : place_definition {
    std::vector<std::string> holes;
};
