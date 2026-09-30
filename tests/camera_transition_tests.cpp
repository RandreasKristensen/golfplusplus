#include "doctest.h"

#include "renderer/camera_transition.h"

#include <cmath>

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

namespace {

camera_view make_view(const glm::vec3& position, const glm::vec3& target, const float fov = 60.0f) {
    camera_view view;
    view.position = position;
    view.target = target;
    view.fov_degrees = fov;
    return view;
}

bool near(const glm::vec3& a, const glm::vec3& b, const float epsilon = 0.001f) {
    return glm::length(a - b) <= epsilon;
}

camera_transition_settings settings() {
    camera_transition_settings result;
    result.duration_seconds = 0.5f;
    result.jump_distance = 3.0f;
    return result;
}

} // namespace

TEST_CASE("camera transition snaps on the first frame") {
    const camera_view walking = make_view(glm::vec3(0.0f, 1.65f, 0.0f), glm::vec3(0.0f, 1.65f, 10.0f));
    const camera_transition_state state = update_camera_transition(camera_transition_state{},
                                                                   camera_rig::walking,
                                                                   walking,
                                                                   0.016f,
                                                                   settings());
    CHECK(near(state.shown.position, walking.position));
    CHECK(near(state.shown.target, walking.target));
}

TEST_CASE("camera transition eases between rigs and finishes within the duration") {
    const camera_view walking = make_view(glm::vec3(0.0f, 1.65f, 0.0f), glm::vec3(0.0f, 1.65f, 10.0f));
    const camera_view aiming = make_view(glm::vec3(4.0f, 1.55f, -2.0f), glm::vec3(4.0f, 1.05f, 12.0f), 50.0f);
    camera_transition_state state = snap_camera_transition(camera_rig::walking, walking);

    state = update_camera_transition(state, camera_rig::aiming, aiming, 0.016f, settings());
    // The switch frame still shows the old view: no cut.
    CHECK(near(state.shown.position, walking.position));

    float previous_distance = glm::length(state.shown.position - aiming.position);
    for (int i = 0; i < 10; ++i) {
        state = update_camera_transition(state, camera_rig::aiming, aiming, 0.02f, settings());
        const float distance = glm::length(state.shown.position - aiming.position);
        CHECK(distance < previous_distance);
        // Each frame moves only a fraction of the way.
        CHECK(distance > 0.0f);
        previous_distance = distance;
    }
    CHECK(state.shown.fov_degrees < 60.0f);
    CHECK(state.shown.fov_degrees > 50.0f);

    for (int i = 0; i < 30; ++i) {
        state = update_camera_transition(state, camera_rig::aiming, aiming, 0.02f, settings());
    }
    CHECK(near(state.shown.position, aiming.position));
    CHECK(near(state.shown.target, aiming.target));
    CHECK(std::abs(state.shown.fov_degrees - 50.0f) < 0.001f);
}

TEST_CASE("camera transition follows a moving rig without blending") {
    camera_transition_state state = snap_camera_transition(
        camera_rig::walking, make_view(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 10.0f)));
    const camera_view moved = make_view(glm::vec3(0.0f, 0.0f, 0.5f), glm::vec3(0.0f, 0.0f, 10.5f));
    state = update_camera_transition(state, camera_rig::walking, moved, 0.016f, settings());
    CHECK(near(state.shown.position, moved.position));
}

TEST_CASE("camera transition blends when a rig teleports") {
    camera_transition_state state = snap_camera_transition(
        camera_rig::walking, make_view(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 10.0f)));
    const camera_view teleported = make_view(glm::vec3(50.0f, 0.0f, 0.0f), glm::vec3(50.0f, 0.0f, 10.0f));
    state = update_camera_transition(state, camera_rig::walking, teleported, 0.016f, settings());
    CHECK(near(state.shown.position, glm::vec3(0.0f)));
    state = update_camera_transition(state, camera_rig::walking, teleported, 0.1f, settings());
    CHECK(state.shown.position.x > 0.0f);
    CHECK(state.shown.position.x < 50.0f);
}

TEST_CASE("camera transition restarts from the shown view when interrupted") {
    const camera_view walking = make_view(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 10.0f));
    const camera_view aiming = make_view(glm::vec3(2.0f, 0.0f, 0.0f), glm::vec3(2.0f, 0.0f, 10.0f));
    camera_transition_state state = snap_camera_transition(camera_rig::walking, walking);
    state = update_camera_transition(state, camera_rig::aiming, aiming, 0.0f, settings());
    state = update_camera_transition(state, camera_rig::aiming, aiming, 0.25f, settings());
    const glm::vec3 midway = state.shown.position;
    CHECK(midway.x > 0.0f);

    state = update_camera_transition(state, camera_rig::walking, walking, 0.016f, settings());
    CHECK(near(state.shown.position, midway));
}

TEST_CASE("camera blend turns the look direction instead of sliding the target") {
    const camera_view near_target = make_view(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 10.0f));
    const camera_view far_target = make_view(glm::vec3(0.0f), glm::vec3(200.0f, 0.0f, 0.0f));
    const camera_view half = blend_camera_views(near_target, far_target, 0.5f);
    const glm::vec3 direction = glm::normalize(half.target - half.position);
    CHECK(std::abs(direction.x - std::sqrt(0.5f)) < 0.001f);
    CHECK(std::abs(direction.z - std::sqrt(0.5f)) < 0.001f);
    CHECK(std::abs(glm::length(half.target - half.position) - 105.0f) < 0.01f);
}
