#pragma once

#include <string>
#include <vector>

#include <glm/vec3.hpp>

// What is drawn behind a course (renderer/backdrop_pass.h): a sky panorama, a
// land panorama (far ground, hills and treelines, transparent where the sky
// shows) over it, and the haze they and the drawn scene fade into with
// distance. Both images are relative to the asset root (course_in_folder) and drawn by
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

// The sky a course is played under (game/sky_clock.h): the day of the year
// it always is there, in the sky clock's year, while the time of day runs on
// the course's own clock.
struct course_sky {
    int month = 1;  // 1..12
    int day = 1;    // 1..31
    // The clock the datestamp shows there: hours ahead of UTC (Danish summer
    // time is 2; longitude / 15 is local solar time).
    float utc_offset_hours = 0.0f;
    // Degrees: the sun never sinks below this, so a course can have the
    // white nights of a place further north. -90 leaves it where the
    // latitude and date put it.
    float lowest_sun_deg = -90.0f;
};

// A course manifest (assets/world/courses/<course>/course.json). Its file
// references are written relative to its folder; course_in_folder makes
// them relative to the asset root.
struct course_definition {
    std::string id;
    std::string name;
    // Hole references: a path, or a bare hole id meaning holes/<id>.json.
    // See course_hole_path.
    std::vector<std::string> holes;
    // The course world.
    std::string world;
    course_backdrop backdrop;
    course_sky sky;
};
