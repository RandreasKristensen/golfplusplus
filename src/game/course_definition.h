#pragma once

#include <string>
#include <vector>

// A course manifest (assets/courses/*.json).
struct course_definition {
    std::string id;
    std::string name;
    // Hole references: a path relative to the asset root, or a bare hole id
    // meaning holes/<id>.json. See course_hole_path.
    std::vector<std::string> holes;
    // Optional course world (hub) path relative to the asset root.
    std::string world;
    // A single hole from the hole picker: completing it does not complete a course.
    bool practice = false;
};
