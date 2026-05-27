#pragma once

#include <string>
#include <vector>

struct course_definition {
    std::string id;
    std::string name;
    int hole_count = 0;
    std::string world;
    std::vector<std::string> holes;
};
