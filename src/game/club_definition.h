#pragma once

#include "physics/club_stats.h"

#include <string>

struct club_definition {
    std::string id;
    std::string name;
    std::string label;
    int bag_order = 0;
    club_stats stats;
};
