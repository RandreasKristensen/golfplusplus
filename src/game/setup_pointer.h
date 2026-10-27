#pragma once

// What the wheel changes at address, by what the view's centre is on (there
// is no cursor): the clubface turns the face, the ball moves it in the
// stance, my feet or the alignment stick turn the stance, anything else sets
// the backswing. The renderer outlines the part it names. Pure.

#include "game/address_view.h"

#include <glm/vec3.hpp>

enum class setup_part {
    none,  // the backswing
    face,
    ball,
    stance
};

// The part within `radius_degrees` of the view from `eye` along unit
// `direction`, the nearest winning.
setup_part pointed_part(const glm::vec3& eye, const glm::vec3& direction, const address_marks& marks,
                        float radius_degrees);
