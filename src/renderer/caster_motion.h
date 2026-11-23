#pragma once

// The tracked points of the moving casters (figures, carts, flags) as they stood
// when a shadow image was drawn, to tell whether the image still shows them:
// it does while every one is within a tolerance of where it was drawn, however
// many frames that takes, so a flag's flutter or a step too small to show costs
// no redraw and a slow walk is caught once it has gone far enough. GL-free.
//
// A list is given as `each`, a callable taking a function and calling it with
// every tracked position in order: a triangle's vertices, and for an instance
// the corners of its mesh's box placed (mesh_instance.h placed_corners), which
// move at least as far as any of its vertices, so the frame's casters are
// compared where they lie.

#include <cstddef>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

class caster_motion {
public:
    // True when the list is the held one with no vertex more than `tolerance` metres from its held place.
    template <typename each_position>
    bool within(each_position&& each, const float tolerance) const {
        const float limit = tolerance * tolerance;
        std::size_t index = 0;
        bool same = true;
        each([&](const glm::vec3& position) {
            if (!same) {
                return;
            }
            if (index >= held_.size()) {
                same = false;
                return;
            }
            const glm::vec3 apart = position - held_[index++];
            same = glm::dot(apart, apart) <= limit;
        });
        return same && index == held_.size();
    }
    template <typename each_position>
    void hold(each_position&& each) {
        held_.clear();
        each([&](const glm::vec3& position) { held_.push_back(position); });
    }
    void forget() { held_.clear(); }

private:
    std::vector<glm::vec3> held_;
};
