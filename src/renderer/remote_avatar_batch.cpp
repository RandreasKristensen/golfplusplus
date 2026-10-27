#include "renderer/remote_avatar_batch.h"

#include "physics/vector_math.h"
#include "renderer/camera_local.h"
#include "renderer/primitive_mesh.h"
#include "renderer/swing_club.h"


#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/trigonometric.hpp>

namespace {
constexpr float no_glow = 0.0f;
constexpr bool writes_depth = true;
// The figure's height over the eye height, and its parts as fractions of it.
constexpr float figure_height_scale = 1.08f;
constexpr float figure_head_height = 0.90f;
constexpr float figure_head_radius = 0.085f;
// The mouth, from the middle of the head: low on its front.
const glm::vec3 figure_mouth(0.0f, -0.035f, 0.08f);
// What a figure holds: larger than life, so it reads from afar.
constexpr float figure_prop_scale = 0.4f;

constexpr std::size_t quad_vertex_count = 6;
constexpr std::size_t figure_cylinder_count = 7;  // legs, torso, arms, neck, cap
constexpr std::size_t seated_cylinder_count = 9;  // thighs, shins, torso, arms, neck, cap
constexpr std::size_t figure_sphere_count = 3;    // head, shoes
constexpr std::size_t figure_quad_count = 1;      // cap brim

// Where the figure's limbs go, in its frame (x left, y up, z forward) as
// fractions of its height: the left one, the right mirrored.
const glm::vec3 shoulder(0.168f, 0.78f, 0.0f);
const glm::vec3 hanging_hand(0.212f, 0.46f, 0.0f);
const glm::vec3 hip(0.07f, 0.46f, 0.0f);
const glm::vec3 foot(0.07f, 0.0f, 0.0f);
const glm::vec3 shoe(0.07f, 0.05f, 0.03f);
// Sat at a cart's wheel (cart_batch.h): on its seat, feet on its floor.
const glm::vec3 seated_hip(0.07f, 0.45f, 0.0f);
const glm::vec3 seated_knee(0.07f, 0.45f, 0.24f);
const glm::vec3 seated_foot(0.07f, 0.30f, 0.27f);
const glm::vec3 seated_shoe(0.07f, 0.30f, 0.31f);
const glm::vec3 wheel_hand(0.08f, 0.57f, 0.22f);
const glm::vec3 lap_hand(0.12f, 0.48f, 0.18f);
constexpr float leg_radius = 0.06f;
constexpr float arm_radius = 0.045f;
constexpr float shoe_radius = 0.05f;

glm::vec3 mirrored(const glm::vec3& left) {
    return glm::vec3(-left.x, left.y, left.z);
}

std::size_t figure_vertices(const std::size_t cylinders) {
    return cylinders * make_cylinder_positions(primitive_cylinder_segments).size() +
           figure_sphere_count * make_sphere_positions(primitive_sphere_latitude_segments, primitive_sphere_longitude_segments).size() +
           figure_quad_count * quad_vertex_count;
}

// Draws parts in a figure's own frame: feet at the origin, facing along its
// yaw, sizes as fractions of its height.
class figure_painter {
public:
    figure_painter(world_marker_batch& batch, const glm::vec3& feet, const float yaw, const float height)
        : batch_(batch), feet_(feet), forward_(yaw_direction(yaw)), left_(yaw_left(forward_)), h_(height) {}

    void cylinder(const glm::vec3& centre, const glm::vec3& rotation, const glm::vec3& scale, const glm::vec3& color) const {
        batch_.append_cylinder(local_cylinder_model(feet_, feet_ + forward_, centre * h_, rotation, scale * h_), color, no_glow,
                               writes_depth);
    }
    void sphere(const glm::vec3& centre, const float radius, const glm::vec3& color) const {
        batch_.append_sphere(local_sphere_model(feet_, feet_ + forward_, centre * h_, radius * h_), color, no_glow, writes_depth);
    }
    void limb(const glm::vec3& from, const glm::vec3& to, const float radius, const glm::vec3& color) const {
        batch_.append_cylinder(local_segment_model(feet_, feet_ + forward_, from * h_, to * h_, radius * h_), color, no_glow,
                               writes_depth);
    }
    void panel(const glm::vec3& centre, const glm::vec3& rotation, const glm::vec2& half_size, const glm::vec3& color) const {
        batch_.append_quad(local_panel_model(feet_, feet_ + forward_, centre * h_, rotation, half_size * h_), color, no_glow,
                           writes_depth);
    }
    // A world point in the frame, as fractions of the height.
    glm::vec3 local(const glm::vec3& world) const {
        const glm::vec3 offset = world - feet_;
        return glm::vec3(glm::dot(offset, left_), offset.y, glm::dot(offset, forward_)) / h_;
    }

private:
    world_marker_batch& batch_;
    glm::vec3 feet_;
    glm::vec3 forward_;
    glm::vec3 left_;
    float h_;
};

void append_upper_body(const figure_painter& figure, const glm::vec3& shirt, const glm::vec3& cap, const glm::vec3& skin) {
    figure.cylinder(glm::vec3(0.0f, 0.63f, 0.0f), glm::vec3(0.0f), glm::vec3(0.15f, 0.36f, 0.11f), shirt);
    figure.cylinder(glm::vec3(0.0f, 0.83f, 0.0f), glm::vec3(0.0f), glm::vec3(0.04f, 0.06f, 0.04f), skin);
    figure.sphere(glm::vec3(0.0f, figure_head_height, 0.0f), figure_head_radius, skin);
    figure.cylinder(glm::vec3(0.0f, 0.965f, 0.0f), glm::vec3(0.0f), glm::vec3(0.09f, 0.04f, 0.09f), cap);
    figure.panel(glm::vec3(0.0f, 0.95f, 0.11f), glm::vec3(glm::radians(90.0f), 0.0f, 0.0f), glm::vec2(0.07f, 0.05f), cap);
}

void append_arms(const figure_painter& figure, const glm::vec3& left_hand, const glm::vec3& right_hand, const glm::vec3& shirt) {
    figure.limb(shoulder, left_hand, arm_radius, shirt);
    figure.limb(mirrored(shoulder), right_hand, arm_radius, shirt);
}
}

glm::vec3 remote_tint(const figure_colors& colors, const std::uint64_t player_id) {
    return colors.shirts[player_id % colors.shirts.size()];
}

glm::vec3 group_highlight(const figure_colors& colors, const std::uint64_t group_id) {
    return colors.group_caps[group_id % colors.group_caps.size()];
}

std::size_t remote_figure_vertex_count() {
    return figure_vertices(figure_cylinder_count);
}

std::size_t remote_seated_vertex_count() {
    return figure_vertices(seated_cylinder_count);
}

void append_remote_avatar(world_marker_batch& batch, const render_remote_avatar& avatar, const float eye_height,
                          const float ball_radius, const scene_colors& colors) {
    const figure_colors& clothes = colors.figures;
    const figure_painter figure(batch, avatar.position, avatar.yaw, eye_height * figure_height_scale);
    const glm::vec3 shirt = remote_tint(clothes, avatar.player_id);
    const glm::vec3 cap = avatar.group_id != 0 ? group_highlight(clothes, avatar.group_id) : clothes.cap;
    append_upper_body(figure, shirt, cap, clothes.skin);

    if (avatar.seated) {
        for (const bool right : {false, true}) {
            const auto side = [right](const glm::vec3& left) { return right ? mirrored(left) : left; };
            figure.limb(side(seated_hip), side(seated_knee), leg_radius, clothes.trousers);
            figure.limb(side(seated_knee), side(seated_foot), leg_radius, clothes.trousers);
            figure.sphere(side(seated_shoe), shoe_radius, clothes.shoes);
        }
        const glm::vec3 hand = avatar.at_wheel ? wheel_hand : lap_hand;
        append_arms(figure, hand, mirrored(hand), shirt);
        return;
    }

    for (const bool right : {false, true}) {
        const auto side = [right](const glm::vec3& left) { return right ? mirrored(left) : left; };
        figure.limb(side(hip), side(foot), leg_radius, clothes.trousers);
        figure.sphere(side(shoe), shoe_radius, clothes.shoes);
    }
    if (avatar.swing) {
        // Both hands on the club's grip.
        const glm::vec3 grip = figure.local(swing_club_grip(*avatar.swing, ball_radius));
        append_arms(figure, grip, grip, shirt);
    } else {
        append_arms(figure, hanging_hand, mirrored(hanging_hand), shirt);
    }
}

std::optional<prop_holder> remote_prop_holder(const render_remote_avatar& avatar, const float eye_height,
                                              const prop_catalog& props) {
    const auto held = [&props](const std::optional<render_item_use>& use) -> std::optional<held_use> {
        const auto model = use ? props.find(use->model) : props.end();
        return model != props.end() ? std::optional<held_use>(held_use{&model->second, use->elapsed}) : std::nullopt;
    };
    const std::optional<held_use> smokable = held(avatar.smoking);
    const std::optional<held_use> drinkable = held(avatar.drinking);
    if (!smokable && !drinkable) {
        return std::nullopt;
    }
    const float h = eye_height * figure_height_scale;
    const float s = figure_prop_scale;
    const glm::vec3 mouth = figure_mouth * h;
    const held_grip camera = camera_held_grip();
    prop_holder holder;
    const glm::vec3 head = avatar.position + world_up * (h * figure_head_height);
    holder.frame = camera_local_model(head, head + yaw_direction(avatar.yaw), glm::vec3(0.0f), glm::vec3(0.0f), glm::vec3(1.0f));
    holder.scale = s;
    // The cigarette between the lips, as it sits in my view; the can tipped
    // back to the mouth, its label facing out, spilling down the chin.
    holder.grip.smokable_filter = mouth;
    holder.grip.smokable_tip = mouth + (camera.smokable_tip - camera.smokable_filter) * s;
    holder.grip.can_held = can_pose{mouth + glm::vec3(0.0f, -0.12f, 0.26f) * s, glm::vec3(0.0f, 0.5f, -0.866f),
                                    glm::vec3(0.0f, -0.866f, -0.5f)};
    holder.grip.can_pouring = holder.grip.can_held;
    holder.smokable = smokable;
    holder.drinkable = drinkable;
    return holder;
}

void append_remote_ball(world_marker_batch& batch, const render_remote_ball& ball, const float radius,
                        const figure_colors& colors, const float glow) {
    // Tinted towards the player's colour, still mostly white so it reads as a ball.
    const glm::vec3 color = glm::mix(colors.ball, remote_tint(colors, ball.player_id), colors.ball_shirt_share);
    batch.append_sphere(glm::scale(glm::translate(glm::mat4(1.0f), ball.position), glm::vec3(radius)), color, glow,
                        writes_depth);
}
