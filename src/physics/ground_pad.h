#pragma once

// Flat pads levelled into the ground (a tee box's, game/tee_box.h), as
// bunkers and ponds are carved into it (build_ground). A pad is level at the
// mean of the uncarved ground it covers: on a plane that is the height at its
// middle, so as much is cut on the high side as filled on the low, and of all
// flat heights it is the nearest to the land (least squares). It stays level
// a little past its edge (`flat_outset`), so no triangle over it tilts, then
// eases back to the land over a blend distance (smoothstep: no step or kink
// at either end), like a real tee's gently sloped banks. Pure.

#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

struct ground_pad {
    glm::vec3 center{0.0f};
    glm::vec3 along{0.0f, 0.0f, 1.0f};  // horizontal unit vector along its length
    float half_width = 0.0f;   // across
    float half_length = 0.0f;  // along
};

// Points over `pad`'s footprint, its edges included, at most `spacing` apart.
std::vector<glm::vec3> pad_footprint_points(const ground_pad& pad, float spacing);

struct levelled_pad {
    ground_pad pad;
    float level = 0.0f;
    float flat_outset = 0.0f;
};

// `height`, the ground's at `position`, with `pads` levelled in over
// `blend_distance`. On a pad only its own level counts; where pads' banks
// meet, each pulls towards its level the harder the nearer it is.
float levelled_height(const std::vector<levelled_pad>& pads, const glm::vec3& position, float height, float blend_distance);

// Half the XZ box round `pad` that its levelling reaches.
glm::vec2 levelled_reach(const levelled_pad& pad, float blend_distance);
