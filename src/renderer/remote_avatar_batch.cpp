#include "renderer/remote_avatar_batch.h"

#include "physics/vector_math.h"
#include "renderer/camera_local.h"
#include "renderer/cart_batch.h"
#include "renderer/primitive_mesh.h"

#include <array>

#include <glm/geometric.hpp>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/trigonometric.hpp>

namespace {
constexpr float opaque = 1.0f;
constexpr bool writes_depth = true;
// The figure's height over the eye height, and its parts as fractions of it.
constexpr float figure_height_scale = 1.08f;
constexpr float figure_head_height = 0.90f;
constexpr float figure_head_radius = 0.085f;
// The mouth, from the middle of the head: low on its front.
const glm::vec3 figure_mouth(0.0f, -0.035f, 0.08f);
// Emote props on a figure: larger than life, so they read from afar.
constexpr float figure_prop_scale = 0.4f;

const glm::vec3 trousers_color(0.16f, 0.15f, 0.13f);
const glm::vec3 skin_color(0.80f, 0.62f, 0.46f);
const glm::vec3 cap_color(0.86f, 0.84f, 0.76f);
const glm::vec3 shoe_color(0.06f, 0.06f, 0.05f);

// Washed-out camcorder colours: shirts, then group highlights.
const std::array<glm::vec3, 8> shirt_palette{{
    {0.72f, 0.24f, 0.20f}, {0.22f, 0.40f, 0.66f}, {0.84f, 0.70f, 0.26f}, {0.30f, 0.56f, 0.32f},
    {0.62f, 0.36f, 0.62f}, {0.86f, 0.52f, 0.24f}, {0.26f, 0.62f, 0.64f}, {0.82f, 0.80f, 0.74f},
}};
const std::array<glm::vec3, 5> highlight_palette{{
    {0.98f, 0.86f, 0.18f}, {0.20f, 0.92f, 0.96f}, {0.98f, 0.38f, 0.74f}, {0.52f, 0.98f, 0.30f}, {0.98f, 0.56f, 0.16f},
}};

constexpr std::size_t quad_vertex_count = 6;
constexpr std::size_t figure_cylinder_count = 7;  // legs, torso, arms, neck, cap
constexpr std::size_t driver_cylinder_count = 9;  // thighs, shins, torso, arms, neck, cap
constexpr std::size_t figure_sphere_count = 3;    // head, shoes
constexpr std::size_t figure_quad_count = 1;      // cap brim

// Where the figure's limbs go, in its frame (x right, y up, z forward) as
// fractions of its height: the left one, the right mirrored.
const glm::vec3 shoulder(-0.168f, 0.78f, 0.0f);
const glm::vec3 hanging_hand(-0.212f, 0.46f, 0.0f);
const glm::vec3 hip(-0.07f, 0.46f, 0.0f);
const glm::vec3 foot(-0.07f, 0.0f, 0.0f);
const glm::vec3 shoe(-0.07f, 0.05f, 0.03f);
// Sat at a cart's wheel (cart_batch.h): on its seat, feet on its floor.
const glm::vec3 seated_hip(-0.07f, 0.45f, 0.0f);
const glm::vec3 seated_knee(-0.07f, 0.45f, 0.24f);
const glm::vec3 seated_foot(-0.07f, 0.30f, 0.27f);
const glm::vec3 seated_shoe(-0.07f, 0.30f, 0.31f);
const glm::vec3 wheel_hand(-0.08f, 0.57f, 0.22f);
constexpr float leg_radius = 0.06f;
constexpr float arm_radius = 0.045f;
constexpr float shoe_radius = 0.05f;
// How far in front of the figure it holds a club's grip (world units).
constexpr float grip_reach = 0.36f;

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
        : batch_(batch), feet_(feet), forward_(yaw_direction(yaw)), right_(glm::cross(world_up, forward_)), h_(height) {}

    void cylinder(const glm::vec3& centre, const glm::vec3& rotation, const glm::vec3& scale, const glm::vec3& color) const {
        batch_.append_cylinder(local_cylinder_model(feet_, feet_ + forward_, centre * h_, rotation, scale * h_), color, opaque,
                               writes_depth);
    }
    void sphere(const glm::vec3& centre, const float radius, const glm::vec3& color) const {
        batch_.append_sphere(local_sphere_model(feet_, feet_ + forward_, centre * h_, radius * h_), color, opaque, writes_depth);
    }
    void limb(const glm::vec3& from, const glm::vec3& to, const float radius, const glm::vec3& color) const {
        batch_.append_cylinder(local_segment_model(feet_, feet_ + forward_, from * h_, to * h_, radius * h_), color, opaque,
                               writes_depth);
    }
    void panel(const glm::vec3& centre, const glm::vec3& rotation, const glm::vec2& half_size, const glm::vec3& color) const {
        batch_.append_quad(local_panel_model(feet_, feet_ + forward_, centre * h_, rotation, half_size * h_), color, opaque,
                           writes_depth);
    }
    // A world point in the frame, as fractions of the height.
    glm::vec3 local(const glm::vec3& world) const {
        const glm::vec3 offset = world - feet_;
        return glm::vec3(glm::dot(offset, right_), offset.y, glm::dot(offset, forward_)) / h_;
    }

private:
    world_marker_batch& batch_;
    glm::vec3 feet_;
    glm::vec3 forward_;
    glm::vec3 right_;
    float h_;
};

void append_upper_body(const figure_painter& figure, const glm::vec3& shirt, const glm::vec3& cap) {
    figure.cylinder(glm::vec3(0.0f, 0.63f, 0.0f), glm::vec3(0.0f), glm::vec3(0.15f, 0.36f, 0.11f), shirt);
    figure.cylinder(glm::vec3(0.0f, 0.83f, 0.0f), glm::vec3(0.0f), glm::vec3(0.04f, 0.06f, 0.04f), skin_color);
    figure.sphere(glm::vec3(0.0f, figure_head_height, 0.0f), figure_head_radius, skin_color);
    figure.cylinder(glm::vec3(0.0f, 0.965f, 0.0f), glm::vec3(0.0f), glm::vec3(0.09f, 0.04f, 0.09f), cap);
    figure.panel(glm::vec3(0.0f, 0.95f, 0.11f), glm::vec3(glm::radians(90.0f), 0.0f, 0.0f), glm::vec2(0.07f, 0.05f), cap);
}

void append_arms(const figure_painter& figure, const glm::vec3& left_hand, const glm::vec3& right_hand, const glm::vec3& shirt) {
    figure.limb(shoulder, left_hand, arm_radius, shirt);
    figure.limb(mirrored(shoulder), right_hand, arm_radius, shirt);
}
}

glm::vec3 remote_tint(const std::uint64_t player_id) {
    return shirt_palette[player_id % shirt_palette.size()];
}

glm::vec3 group_highlight(const std::uint64_t group_id) {
    return highlight_palette[group_id % highlight_palette.size()];
}

std::size_t remote_figure_vertex_count() {
    return figure_vertices(figure_cylinder_count);
}

std::size_t remote_driver_vertex_count() {
    return figure_vertices(driver_cylinder_count);
}

figure_stance figure_address_stance(const glm::vec3& ball_position, const float ball_radius, const float aim_angle) {
    // On the side the local player addresses from (address_stance_position),
    // the grip at arm's length in front.
    const glm::vec3 grip = swing_club_grip(ball_position, ball_radius, aim_angle);
    glm::vec3 feet = horizontal(grip) + yaw_left(yaw_direction(aim_angle)) * grip_reach;
    feet.y = ball_position.y - ball_radius;
    return figure_stance{feet, yaw_towards(feet, grip)};
}

void append_remote_avatar(world_marker_batch& batch, const render_remote_avatar& avatar, const float eye_height,
                          const float ball_radius) {
    const figure_painter figure(batch, avatar.position, avatar.yaw, eye_height * figure_height_scale);
    const glm::vec3 shirt = remote_tint(avatar.player_id);
    const glm::vec3 cap = avatar.group_id != 0 ? group_highlight(avatar.group_id) : cap_color;
    append_upper_body(figure, shirt, cap);

    if (avatar.in_cart) {
        // Sat at the wheel, the cart around their eyes.
        for (const bool right : {false, true}) {
            const auto side = [right](const glm::vec3& left) { return right ? mirrored(left) : left; };
            figure.limb(side(seated_hip), side(seated_knee), leg_radius, trousers_color);
            figure.limb(side(seated_knee), side(seated_foot), leg_radius, trousers_color);
            figure.sphere(side(seated_shoe), shoe_radius, shoe_color);
        }
        append_arms(figure, wheel_hand, mirrored(wheel_hand), shirt);
        append_outside_cart_model(batch, avatar.position, avatar.yaw);
        return;
    }

    for (const bool right : {false, true}) {
        const auto side = [right](const glm::vec3& left) { return right ? mirrored(left) : left; };
        figure.limb(side(hip), side(foot), leg_radius, trousers_color);
        figure.sphere(side(shoe), shoe_radius, shoe_color);
    }
    if (avatar.swing) {
        // Both hands on the club's grip.
        const glm::vec3 grip =
            figure.local(swing_club_grip(avatar.swing->ball_position, ball_radius, avatar.swing->aim_angle));
        append_arms(figure, grip, grip, shirt);
        append_swing_club(batch, avatar.swing->ball_position, ball_radius, avatar.swing->aim_angle, avatar.swing->power);
    } else {
        append_arms(figure, hanging_hand, mirrored(hanging_hand), shirt);
    }
}

std::optional<emote_holder> remote_emote_holder(const render_remote_avatar& avatar, const float eye_height) {
    if (!avatar.smoke_elapsed && !avatar.drink_elapsed) {
        return std::nullopt;
    }
    const float h = eye_height * figure_height_scale;
    const float s = figure_prop_scale;
    const glm::vec3 mouth = figure_mouth * h;
    const emote_grip camera = camera_emote_grip();
    emote_holder holder;
    holder.eye = avatar.position + world_up * (h * figure_head_height);
    holder.target = holder.eye + yaw_direction(avatar.yaw);
    holder.scale = s;
    // The cigarette between the lips, as it sits in my view; the can tipped
    // back to the mouth, its label facing out.
    holder.grip.cigarette_filter = mouth;
    holder.grip.cigarette_tip = mouth + (camera.cigarette_tip - camera.cigarette_filter) * s;
    holder.grip.can_centre = mouth + glm::vec3(0.0f, -0.12f, 0.26f) * s;
    holder.grip.can_rotation = glm::vec3(glm::radians(60.0f), glm::radians(180.0f), 0.0f);
    holder.smoke_elapsed = avatar.smoke_elapsed;
    holder.drink_elapsed = avatar.drink_elapsed;
    return holder;
}

void append_remote_ball(world_marker_batch& batch, const render_remote_ball& ball, const float radius) {
    // Tinted towards the player's colour, still mostly white so it reads as a ball.
    const glm::vec3 color = glm::vec3(0.86f) * 0.6f + remote_tint(ball.player_id) * 0.4f;
    batch.append_sphere(glm::scale(glm::translate(glm::mat4(1.0f), ball.position), glm::vec3(radius)), color, opaque,
                        writes_depth);
}
