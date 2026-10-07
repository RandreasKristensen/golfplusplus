#pragma once

#include <string>
#include <vector>

#include <glm/vec3.hpp>

// What is drawn behind a course (renderer/backdrop_pass.h): a sky panorama, a
// land panorama (far ground, hills and treelines, transparent where the sky
// shows) over it, and the haze they and the drawn scene fade into with
// distance. Both images are relative to the asset root and drawn by
// tooling/art/make_art.py, which also takes the sky's horizon from
// `haze_color` so the far land melts into it.
struct course_backdrop {
    std::string sky;
    std::string land;
    glm::vec3 haze_color{0.0f};
    // Scales the haze drawn into the land panorama and onto the scene: 1 is
    // as drawn, 0 clear.
    float haze_amount = 0.0f;
    // Metres over which the drawn ground fades to the far land's haze (the
    // renderer never takes more than the play area's extent, so the edge of
    // the drawn ground always meets the backdrop fully hazed).
    float haze_distance = 0.0f;
};

// A course manifest (assets/courses/*.json).
struct course_definition {
    std::string id;
    std::string name;
    // Hole references: a path relative to the asset root, or a bare hole id
    // meaning holes/<id>.json. See course_hole_path.
    std::vector<std::string> holes;
    // Optional course world (hub) path relative to the asset root.
    std::string world;
    // Every course file has one; empty images only on a practice hole no
    // course plays.
    course_backdrop backdrop;
    // A single hole from the hole picker: completing it does not complete a course.
    bool practice = false;
};
