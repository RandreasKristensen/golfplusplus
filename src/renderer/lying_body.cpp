#include "renderer/lying_body.h"

#include "physics/vector_math.h"
#include "renderer/remote_avatar_batch.h"

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace {
constexpr float no_glow = 0.0f;

// Where the parts lie, as fractions of the figure's length from the feet
// (the standing figure's heights, remote_avatar_batch.cpp), of the box's
// half-width (across) and of its height (up).
constexpr float hip_at = 0.46f;
constexpr float shoulder_at = 0.78f;
constexpr float neck_from = 0.81f;
constexpr float torso_from = 0.45f;
constexpr float head_at = 0.90f;
constexpr float foot_at = 0.06f;
constexpr float hand_at = 0.50f;
constexpr float torso_across = 0.60f;
constexpr float torso_up = 0.44f;
constexpr float hip_across = 0.50f;
constexpr float ankle_across = 0.56f;
constexpr float shoulder_across = 0.76f;
constexpr float hand_across = 0.80f;
constexpr float leg_up = 0.42f;
constexpr float arm_up = 0.18f;
constexpr float neck_up = 0.20f;
constexpr float head_up = 0.48f;
constexpr float shoe_up = 0.36f;
// The cap's disc on the head's crown, in metres from the head's middle.
constexpr float cap_from = 0.07f;
constexpr float cap_to = 0.115f;
constexpr float cap_radius_share = 0.7f;  // of the head's

class body_painter {
public:
    body_painter(world_marker_batch& batch, const render_lying_body& body)
        : batch_(batch), body_(body), along_(yaw_direction(body.yaw)), left_(yaw_left(along_)) {}

    // The point `t` of the figure's length from the feet, `across` of the half-width to the left, `lift` metres over the ground.
    glm::vec3 point(const float t, const float across, const float lift) const {
        const float s = (t - 0.5f) * body_.length;
        const float half = 0.5f * body_.length;
        const float ground = s < 0.0f ? glm::mix(body_.ground[0], body_.ground[1], (s + half) / half)
                                      : glm::mix(body_.ground[1], body_.ground[2], s / half);
        glm::vec3 at = body_.position + along_ * s + left_ * (across * body_.half_width);
        at.y = ground + lift;
        return at;
    }
    float up(const float share) const { return share * body_.height; }

    // A part of the figure from `a` to `b`, `across` wide and `thick` high (radii).
    void limb(const glm::vec3& a, const glm::vec3& b, const float across, const float thick, const glm::vec3& color) const {
        const glm::vec3 axis = b - a;
        const glm::vec3 direction = glm::normalize(axis);
        const glm::vec3 bend = glm::normalize(world_up - direction * glm::dot(world_up, direction));
        const glm::vec3 side = glm::cross(direction, bend);
        glm::mat4 model(1.0f);
        model[0] = glm::vec4(side * across, 0.0f);
        model[1] = glm::vec4(axis, 0.0f);
        model[2] = glm::vec4(bend * thick, 0.0f);
        model[3] = glm::vec4(a, 1.0f);
        batch_.append_instance(prop_mesh::cylinder, model, color, no_glow);
    }
    void sphere(const glm::vec3& centre, const float radius, const glm::vec3& color) const {
        batch_.append_instance(prop_mesh::sphere, glm::scale(glm::translate(glm::mat4(1.0f), centre), glm::vec3(radius)), color,
                               no_glow);
    }
    const glm::vec3& along() const { return along_; }

private:
    world_marker_batch& batch_;
    const render_lying_body& body_;
    glm::vec3 along_;
    glm::vec3 left_;
};
}

void append_lying_body(world_marker_batch& batch, const render_lying_body& body, const scene_colors& colors) {
    const figure_colors& clothes = colors.figures;
    const body_painter figure(batch, body);
    const glm::vec3 shirt = remote_tint(clothes, body.account);
    const float torso_rest = figure.up(torso_up);
    figure.limb(figure.point(torso_from, 0.0f, torso_rest), figure.point(neck_from, 0.0f, torso_rest), torso_across * body.half_width,
                torso_rest, shirt);

    const float head_radius = figure.up(head_up);
    const float neck_rest = figure.up(neck_up);
    figure.limb(figure.point(neck_from, 0.0f, neck_rest), figure.point(head_at, 0.0f, neck_rest), neck_rest, neck_rest, clothes.skin);
    const glm::vec3 head = figure.point(head_at, 0.0f, head_radius);
    figure.sphere(head, head_radius, clothes.skin);
    const float cap_radius = cap_radius_share * head_radius;
    figure.limb(head + figure.along() * cap_from, head + figure.along() * cap_to, cap_radius, cap_radius, clothes.cap);

    const float leg_rest = figure.up(leg_up);
    const float arm_rest = figure.up(arm_up);
    const float shoe_rest = figure.up(shoe_up);
    for (const float side : {1.0f, -1.0f}) {
        figure.limb(figure.point(hip_at, side * hip_across, leg_rest), figure.point(foot_at, side * ankle_across, leg_rest), leg_rest,
                    leg_rest, clothes.trousers);
        figure.sphere(figure.point(foot_at, side * ankle_across, shoe_rest), shoe_rest, clothes.shoes);
        figure.limb(figure.point(shoulder_at, side * shoulder_across, arm_rest), figure.point(hand_at, side * hand_across, arm_rest),
                    arm_rest, arm_rest, shirt);
    }
}
