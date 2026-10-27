#include "doctest.h"

#include "core/render_frame.h"
#include "core/tape_tracking.h"
#include "game/cart_seats.h"
#include "game/course_session.h"
#include "game/golf_bag.h"
#include "game/golf_cart.h"
#include "game/mode_dispatch.h"
#include "game/text_assets.h"
#include "physics/vector_math.h"
#include "renderer/bag_batch.h"
#include "renderer/cart_batch.h"
#include "renderer/held_props.h"
#include "renderer/prop_batch.h"
#include "renderer/remote_avatar_batch.h"
#include "renderer/shadow_casters.h"

#include "test_support.h"

#include <glm/gtc/matrix_transform.hpp>

namespace {
constexpr float test_fov_degrees = 60.0f;

render_data frame_for(const game_state& state, const float fov_degrees = test_fov_degrees) {
    static const text_assets text = *load_text_assets(asset_root());
    return make_render_data(state, text, shipped_content().skills, render_meshes{}, test_props(), fov_degrees, true);
}
}

TEST_CASE("a hole frame shows the ball, every tee box and the pin") {
    game_state state = started_hole();
    refresh_static_anchor_cache(state);
    const render_data data = frame_for(state);

    CHECK(data.show_ball);
    CHECK(data.show_hole);
    CHECK(data.pin_position == state.static_anchors.pins[0]);
    CHECK(data.tee_position == state.static_anchors.tees[0]);
    REQUIRE(data.tee_boxes != nullptr);
    CHECK(data.tee_boxes->size() == state.area.tee_boxes.size());
    CHECK(data.camera_fov_degrees == test_fov_degrees);
}

TEST_CASE("the interact prompt shows beside the ball on foot, not away from it, over it or with the setting off") {
    game_state state = started_hole();
    REQUIRE(interact_available(state));
    CHECK(frame_for(state).show_interact_prompt);
    CHECK(!make_render_data(state, *load_text_assets(asset_root()), shipped_content().skills, render_meshes{}, test_props(), test_fov_degrees, false)
               .show_interact_prompt);

    state.mode = game_mode::addressing;
    CHECK(!frame_for(state).show_interact_prompt);

    state.mode = game_mode::walking;
    state.player.position = state.ball->position + glm::vec3(60.0f, 0.0f, 0.0f);
    CHECK(!interact_available(state));
    CHECK(!frame_for(state).show_interact_prompt);
}

TEST_CASE("the drawn ball sits on the ground, not sunk into it") {
    game_state state = started_hole();
    const render_data data = frame_for(state);
    const float bottom = data.ball_position.y - data.ball_visual_radius_meters;
    CHECK(near(bottom, terrain_height(state.area, state.ball->position), 0.001f));
    CHECK(near(horizontal(data.ball_position), horizontal(state.ball->position)));
}

TEST_CASE("with my ball in my pocket a frame hides it and shows every tee box, pin and claimable collectible") {
    game_state state = started_game(fixture_hub_course());
    refresh_static_anchor_cache(state);
    const render_data data = frame_for(state);
    CHECK(!data.show_ball);
    CHECK(!data.show_hole);
    REQUIRE(data.tee_boxes != nullptr);
    CHECK(data.tee_boxes->size() == 3U);
    CHECK(data.pin_markers.size() == 3U);
    // The fitness cache needs fitness level 2, so two of three are claimable.
    CHECK(data.collectible_markers.size() == 2U);
}

TEST_CASE("with my ball in play every other hole's pin stays up, and the collectibles too") {
    game_state state = started_game(fixture_hub_course());
    REQUIRE(tee_up(state, 1, false));
    refresh_static_anchor_cache(state);
    const render_data data = frame_for(state);
    CHECK(data.show_hole);
    CHECK(data.collectible_markers.size() == 2U);
    CHECK(data.pin_position == state.static_anchors.pins[1]);
    const std::vector<glm::vec3>& all = state.static_anchors.pins;
    REQUIRE(all.size() == 3U);
    REQUIRE(data.pin_markers.size() == 2U);
    CHECK(data.pin_markers[0] == all[0]);
    CHECK(data.pin_markers[1] == all[2]);
    CHECK(near(all[1], data.pin_position));
}

TEST_CASE("a ball left in its cup is drawn there until it is picked up") {
    game_state state = started_game(fixture_hub_course());
    REQUIRE(tee_up(state, 0, false));
    state.ball->strokes = 3;
    complete_current_hole(state);
    REQUIRE(state.ball.has_value());
    REQUIRE(state.ball->place == ball_place::in_cup);
    state.player.position = state.ball->position;
    refresh_static_anchor_cache(state);
    render_data data = frame_for(state);
    CHECK(data.show_ball);
    CHECK(!data.show_hole);
    CHECK(data.pin_markers.size() == 3U);  // its own pin among them
    CHECK(near(data.ball_position, state.ball->position));

    pick_up_cup_ball(state);
    data = frame_for(state);
    CHECK(!data.show_ball);
}

TEST_CASE("the give-up question names the hole my ball is in play on, only while it is asked") {
    game_state state = started_game(fixture_hub_course());
    REQUIRE(tee_up(state, 0, false));
    CHECK(frame_for(state).give_up_label.empty());
    state.player.position = state.world.hole_starts[2].position;
    update_game(state, interact_input(), 0.016f);
    REQUIRE(state.give_up_prompt.has_value());
    const render_data data = frame_for(state);
    CHECK(data.give_up_label.find('1') != std::string::npos);
    CHECK(!data.show_interact_prompt);
}

TEST_CASE("every camera rig uses the field of view setting") {
    game_state state = started_hole();
    CHECK(frame_for(state, 75.0f).camera_fov_degrees == 75.0f);
    enter_addressing(state);
    REQUIRE(active_camera_rig(state) == camera_rig::addressing);
    CHECK(live_camera_view(state, 50.0f).fov_degrees == 50.0f);
    update_game(state, swing_input(), 0.016f);
    swing_through(state);
    REQUIRE(active_camera_rig(state) == camera_rig::following_shot);
    CHECK(live_camera_view(state, 90.0f).fov_degrees == 90.0f);
}

TEST_CASE("each game mode has its camera rig") {
    game_state state = started_hole();
    CHECK(active_camera_rig(state) == camera_rig::walking);
    enter_addressing(state);
    CHECK(active_camera_rig(state) == camera_rig::addressing);
    // At my eyes over the stance, beside the ball.
    const camera_view eyes = live_camera_view(state, test_fov_degrees);
    CHECK(eyes.position.y > state.ball->position.y + 1.0f);
    CHECK(horizontal_distance(eyes.position, state.ball->position) < 1.0f);

    update_game(state, swing_input(), 0.016f);
    swing_through(state);
    CHECK(active_camera_rig(state) == camera_rig::following_shot);

    // The follow camera stays at the address view of the shot's start.
    const camera_view follow = live_camera_view(state, test_fov_degrees);
    update_game(state, game_input{}, 0.05f);
    CHECK(near(live_camera_view(state, test_fov_degrees).position, follow.position));
}

TEST_CASE("the preview is the simulated shot in still air: it starts at the ball, lands where the shot first lands and rests where it stops") {
    game_state state = started_hole();
    enter_addressing(state);
    REQUIRE(state.mode == game_mode::addressing);
    const render_data data = frame_for(state);
    REQUIRE(data.osd_line.size() > 2U);
    REQUIRE(data.osd_landing.has_value());
    REQUIRE(data.osd_rest.has_value());
    CHECK(near(data.osd_line.front(), state.address.setup.ball_start, 0.01f));
    CHECK(near(data.osd_line.back(), *data.osd_landing));

    shot_course course = current_shot_course(state);
    course.weather = course_weather_at(state, shot_unix_ms(state));
    course.weather.wind = glm::vec3(0.0f);
    const shot_result shot = simulate_shot(state.address.setup, course, state.tuning, state.clubs);
    CHECK(near(*data.osd_rest, shot.rest_position, 0.001f));
    CHECK(horizontal_distance(*data.osd_landing, state.address.setup.ball_start) > 50.0f);

    // Only a changed setup is simulated again.
    const std::vector<glm::vec3> before = state.address.preview->line;
    update_game(state, game_input{}, 0.016f);
    CHECK(state.address.preview->line == before);
    game_input longer;
    longer.backswing_steps = -10;
    update_game(state, longer, 0.016f);
    CHECK(horizontal_distance(*state.address.preview->landing, state.address.setup.ball_start) <
          horizontal_distance(*data.osd_landing, state.address.setup.ball_start));
}

TEST_CASE("the cart I drive is drawn whole where it stands, the camera at my seat's eyes whichever way I look") {
    game_state state = started_game(fixture_hub_course());
    const cart_tuning& tuning = state.tuning.cart;
    state.ride = cart_ride{};
    state.player.position = anchor_on_terrain(state.area, seat_position(state.cart.pose, cart_seat::driver, tuning));
    state.player.yaw = state.cart.pose.yaw;
    for (const float head : {0.0f, 1.6f, 3.1f}) {
        state.look.head_yaw = head;
        const render_data data = frame_for(state);
        REQUIRE(data.carts.size() == 1U);
        // Set on its wheels, a little over the ground under its seat.
        CHECK(near(horizontal(data.carts[0].position), horizontal(state.cart.pose.position)));
        CHECK(near(data.carts[0].position.y, state.cart.pose.position.y, 0.1f));
        CHECK(near(data.carts[0].yaw, state.cart.pose.yaw, 0.0001f));
        CHECK(data.carts[0].driven);
        CHECK(near(data.camera_position, state.player.position + world_up * state.tuning.camera.walking_eye_height));
        CHECK(near(horizontal(data.camera_position),
                   horizontal(seat_position(cart_pose{data.carts[0].position, data.carts[0].yaw}, cart_seat::driver, tuning)),
                   0.001f));
        const glm::vec3 look = horizontal(data.camera_target - data.camera_position);
        CHECK(glm::dot(glm::normalize(look), yaw_direction(state.cart.pose.yaw + head)) > 0.999f);
    }
}

TEST_CASE("my figure casts the sun's shadow where I stand, at my ball while I address it, and none from a cart's seat") {
    game_state state = started_hole();
    render_data data = frame_for(state);
    REQUIRE(data.my_figure.has_value());
    CHECK(near(data.my_figure->position, state.player.position));
    CHECK(data.my_figure->yaw == state.player.yaw);
    CHECK(!data.my_figure->swing.has_value());
    CHECK(data.avatar_eye_height == state.tuning.camera.walking_eye_height);
    world_marker_batch casters;
    prop_batch models;
    build_shadow_casters(casters, models, data, test_scene_colors(), test_props());
    CHECK(casters.vertices().size() == remote_figure_vertex_count());
    REQUIRE(!data.carts.empty());
    CHECK(data.carts[0].model == "golf_cart");
    // My cart with my bag strapped on it, and the club in my hand.
    REQUIRE(data.carts[0].bags.size() == 1U);
    CHECK(data.carts[0].bags.begin()->second == "golf_bag");
    CHECK(models.opaque().size() == data.carts.size() * cart_vertex_count(test_props().at("golf_cart")) +
                                        prop_model_vertex_count(test_props().at("golf_bag")) +
                                        prop_model_vertex_count(test_props().at(data.my_figure->carried_club_model)));

    step_up_to_ball(state);
    data = frame_for(state);
    REQUIRE(data.my_figure.has_value());
    REQUIRE(data.my_figure->swing.has_value());
    CHECK(near(data.my_figure->position, address_stance_position(state.area, state.address.setup, state.tuning)));
    CHECK(near(data.my_figure->yaw, address_facing_yaw(state.address.setup), 0.0001f));

    state.mode = game_mode::walking;
    state.ride = cart_ride{};
    CHECK(!frame_for(state).my_figure.has_value());
}

TEST_CASE("online frames show other players, their tags, balls and trails; offline ones none") {
    game_state state = started_game(fixture_hub_course());
    refresh_static_anchor_cache(state);
    state.online.account_id = 1;
    state.online.status = net_status::connected;
    room_player other;
    other.name = "OTHER";
    other.group_id = 4;
    state.online.players[2] = other;
    remote_avatar avatar;
    avatar.position = state.player.position + glm::vec3(3.0f, 0.0f, 0.0f);
    avatar.mode = motion_mode::cart;
    state.remote_avatars[2] = avatar;
    state.online.carts[2] = room_cart{glm::vec3(50.0f, 0.0f, 50.0f), 0.0f, 0};
    state.online.balls[2] = room_ball{0, glm::vec3(1.0f), 1};
    remote_shot shot;
    shot.result.duration = 1.0f;
    shot.result.trajectory = {glm::vec3(0.0f), glm::vec3(1.0f)};
    shot.elapsed = 2.0f;
    shot.trail = {glm::vec3(0.0f), glm::vec3(1.0f)};
    state.remote_shots[2] = shot;

    render_data data = frame_for(state);
    CHECK(data.remote_avatars.empty());
    CHECK(data.carts.size() == 1U);  // offline, only mine
    CHECK(data.name_tags.empty());

    state.play = play_mode::online;
    data = frame_for(state);
    REQUIRE(data.remote_avatars.size() == 1U);
    CHECK(data.remote_avatars[0].seated);
    CHECK(data.remote_avatars[0].at_wheel);
    // Their cart round them, its headlights on, and mine parked.
    REQUIRE(data.carts.size() == 2U);
    CHECK(!data.carts[0].driven);
    CHECK(data.carts[1].driven);
    CHECK(near(horizontal(seat_position(cart_pose{data.carts[1].position, data.carts[1].yaw}, cart_seat::driver,
                                        state.tuning.cart)),
               horizontal(avatar.position), 0.001f));
    CHECK(data.remote_avatars[0].group_id == 4U);
    REQUIRE(data.name_tags.size() == 1U);
    CHECK(data.name_tags[0].name == "OTHER");
    CHECK(data.name_tags[0].position.y > avatar.position.y);
    CHECK(data.name_tags[0].relationship == player_relationship::unknown);
    CHECK(data.remote_balls.size() == 1U);
    REQUIRE(data.remote_trails.size() == 1U);
    CHECK(data.remote_trails[0].alpha < state.tuning.flight_path.alpha);
    CHECK(data.avatar_eye_height == state.tuning.camera.walking_eye_height);

    // In my group, their tag says so; another group's doesn't.
    room_player me;
    me.group_id = 4;
    state.online.players[1] = me;
    data = frame_for(state);
    REQUIRE(data.name_tags.size() == 1U);
    CHECK(data.name_tags[0].relationship == player_relationship::grouped);
    state.online.players[1].group_id = 5;
    CHECK(frame_for(state).name_tags[0].relationship == player_relationship::unknown);
    state.online.players[1].group_id = 0;
    state.online.players[2].group_id = 0;
    CHECK(frame_for(state).name_tags[0].relationship == player_relationship::unknown);
}

TEST_CASE("another player is drawn smoking, and at their ball addressing it") {
    game_state state = started_game(fixture_hub_course());
    refresh_static_anchor_cache(state);
    state.play = play_mode::online;
    state.online.account_id = 1;
    state.online.status = net_status::connected;
    state.online.players[2] = room_player{};
    remote_avatar avatar;
    avatar.position = state.player.position + glm::vec3(6.0f, 0.0f, 0.0f);
    avatar.smoking = item_use_shown{state.default_loadout.smokable, 0.5f};
    state.remote_avatars[2] = avatar;

    render_data data = frame_for(state);
    REQUIRE(data.remote_avatars.size() == 1U);
    REQUIRE(data.remote_avatars[0].smoking.has_value());
    CHECK(data.remote_avatars[0].smoking->elapsed == 0.5f);
    CHECK(data.remote_avatars[0].smoking->model == state.items.find(state.default_loadout.smokable)->model);
    CHECK(!data.remote_avatars[0].drinking.has_value());
    CHECK(!data.remote_avatars[0].swing.has_value());

    // Standing by it: turned the way they face, where they stand.
    const glm::vec3 ball = state.player.position + glm::vec3(0.0f, 0.0f, 4.0f);
    const float aim = 0.5f;
    net_motion aiming;
    aiming.mode = motion_mode::idle;
    aiming.position = avatar.position;
    aiming.yaw = aim;
    state.online.players[2].motion = aiming;
    state.online.players[2].motion_at = 4;
    state.online.balls[2] = room_ball{0, ball, 0};
    state.remote_avatars[2].yaw = aim;
    data = frame_for(state);
    CHECK(!data.remote_avatars[0].swing.has_value());
    CHECK(near(data.remote_avatars[0].position, avatar.position));
    CHECK(data.remote_avatars[0].yaw == aim);

    // Addressing it: stood at it holding the club, the club down.
    net_motion motion;
    motion.mode = motion_mode::address;
    motion.position = avatar.position;
    motion.yaw = aim;
    state.online.players[2].motion = motion;
    state.online.players[2].motion_at = 5;
    data = frame_for(state);
    REQUIRE(data.remote_avatars[0].swing.has_value());
    CHECK(data.remote_avatars[0].swing->raise == 0.0f);
    const glm::vec3 drawn_ball = drawn_ball_center(ball, state.tuning.scale);
    CHECK(near(data.remote_avatars[0].swing->ball_position, drawn_ball));
    CHECK(data.remote_avatars[0].swing->aim_angle == aim);
    // Where I would stand to their stock setup, side on to it.
    shot_setup theirs;
    theirs.ball_start = ball;
    theirs.aim_angle = aim;
    const glm::vec3 stance = address_stance_position(state.area, theirs, state.tuning);
    CHECK(near(data.remote_avatars[0].position, stance));
    CHECK(data.remote_avatars[0].yaw == address_facing_yaw(theirs));
    // An arm's length from the ball, facing it.
    CHECK(horizontal_distance(stance, ball) < 1.0f);
    CHECK(near(data.remote_avatars[0].yaw, yaw_towards(stance, ball), 0.3f));

    // Once the shot of that swing is hit, they look where they hit it.
    state.remote_avatars[2].hit_motion_at = 5;
    data = frame_for(state);
    CHECK(!data.remote_avatars[0].swing.has_value());
    CHECK(data.remote_avatars[0].yaw == aim);
}

TEST_CASE("a refusal from the server shows as a notice") {
    game_state state = started_hole();
    state.notice = game_notice{"online.error.too_far", 0.0f};
    CHECK(frame_for(state).notice_label == "TOO FAR AWAY");
}

TEST_CASE("the course map numbers every hole of a hub course along its line of play and fits to the holes") {
    game_state state = started_game(fixture_hub_course());
    refresh_static_anchor_cache(state);
    CHECK(frame_for(state).map_holes.empty());

    state.course_map_active = true;
    const render_data data = frame_for(state);
    REQUIRE(data.map_holes.size() == state.course_holes.size());
    for (std::size_t i = 0; i < data.map_holes.size(); ++i) {
        CHECK(data.map_holes[i].number == std::to_string(i + 1));
        REQUIRE(data.map_holes[i].line_of_play.size() == state.area.signs[i].line_of_play.size());
        for (std::size_t p = 0; p < data.map_holes[i].line_of_play.size(); ++p) {
            const glm::vec3 point = data.map_holes[i].line_of_play[p];
            CHECK(near(point, state.area.signs[i].line_of_play[p]));
            CHECK((point.x >= data.course_map_low.x && point.x <= data.course_map_high.x));
            CHECK((point.z >= data.course_map_low.z && point.z <= data.course_map_high.z));
        }
    }
    // The holes, not the land around them.
    CHECK(data.course_map_high.x - data.course_map_low.x <= state.area.extent * 2.0f);
    CHECK(data.course_map_high.z - data.course_map_low.z <= state.area.extent * 2.0f);
}

TEST_CASE("what is pinned to the camera keeps its tuned field of view whatever the setting") {
    game_state state = started_hole();
    const float tuned = state.tuning.camera.viewmodel_fov_degrees;
    REQUIRE(tuned > 0.0f);
    for (const float setting : {50.0f, 60.0f, 90.0f}) {
        const render_data data = frame_for(state, setting);
        CHECK(data.camera_fov_degrees == setting);
        CHECK(data.viewmodel_fov_degrees == tuned);
    }
}

namespace {
// The centre of each unit sphere among `vertices` (prop_batch's, in order).
std::vector<glm::vec3> sphere_centres(const std::vector<prop_vertex>& vertices) {
    const std::size_t per_sphere = prop_mesh_vertex_count(prop_mesh::sphere);
    std::vector<glm::vec3> centres;
    for (std::size_t first = 0; first + per_sphere <= vertices.size(); first += per_sphere) {
        glm::vec3 sum(0.0f);
        for (std::size_t i = first; i < first + per_sphere; ++i) {
            sum += vertices[i].position;
        }
        centres.push_back(sum / static_cast<float>(per_sphere));
    }
    return centres;
}

held_use held(const char* model, const float elapsed) {
    return held_use{&test_props().at(model), elapsed};
}
}

TEST_CASE("what I hold follows the camera: the same on screen wherever I look, the smoke rising up the world and the beer falling down it") {
    game_state state = started_game(fixture_hub_course());
    const float max_pitch = state.tuning.player.max_look_pitch;
    REQUIRE(max_pitch > 0.5f);
    struct seen {
        prop_batch batch;
        glm::mat4 view{1.0f};
        glm::vec3 tip{0.0f};
    };
    const auto props_seen = [&state] {
        const render_data data = frame_for(state);
        const prop_holder mine = camera_prop_holder(data.camera_position, data.camera_target, held("cigarette", 0.7f),
                                                    held("beer_can", 1.0f));
        seen view;
        append_held_props(view.batch, mine, test_scene_colors().glow);
        view.view = glm::lookAt(data.camera_position, data.camera_target, world_up);
        view.tip = glm::vec3(mine.frame * glm::vec4(mine.grip.smokable_tip, 1.0f));
        return view;
    };

    const seen level = props_seen();
    REQUIRE(!level.batch.opaque().empty());
    // Five puffs of smoke, then the beer's drops.
    const std::vector<glm::vec3> level_blobs = sphere_centres(level.batch.blended());
    REQUIRE(level_blobs.size() > 5U);
    float lowest_held = 1e9f;
    for (const prop_vertex& vertex : level.batch.opaque()) {
        lowest_held = std::min(lowest_held, vertex.position.y);
    }
    // Poured: the stream runs below the props held in view.
    CHECK(level_blobs.back().y < lowest_held);

    const auto check_same_on_screen = [&](const seen& now) {
        REQUIRE(now.batch.opaque().size() == level.batch.opaque().size());
        for (std::size_t i = 0; i < now.batch.opaque().size(); ++i) {
            const glm::vec3 seen_now(now.view * glm::vec4(now.batch.opaque()[i].position, 1.0f));
            const glm::vec3 seen_level(level.view * glm::vec4(level.batch.opaque()[i].position, 1.0f));
            CHECK(near(seen_now, seen_level, 0.0001f));
        }
        const std::vector<glm::vec3> blobs = sphere_centres(now.batch.blended());
        REQUIRE(blobs.size() == level_blobs.size());
        for (std::size_t i = 0; i < blobs.size(); ++i) {
            if (i < 5) {
                // A puff of smoke: over the tip in the world, as high as when looking level.
                CHECK(near(blobs[i].y - now.tip.y, level_blobs[i].y - level.tip.y, 0.0001f));
                CHECK(blobs[i].y > now.tip.y);
            } else {
                // A drop of beer: as far down the stream in the world as when looking level.
                CHECK(near(blobs[i].y - blobs[5].y, level_blobs[i].y - level_blobs[5].y, 0.0001f));
            }
        }
    };

    for (const float pitch : {-0.9f * max_pitch, 0.9f * max_pitch}) {
        state.look.pitch = pitch;
        check_same_on_screen(props_seen());
    }
    // Turned, hopping.
    state.look.pitch = -0.3f;
    state.player.yaw += 2.0f;
    state.player.position.y += 0.4f;
    check_same_on_screen(props_seen());

    // Sat in a cart with my head turned from its heading.
    state.ride = cart_ride{};
    state.player.position = anchor_on_terrain(state.area, seat_position(state.cart.pose, cart_seat::driver, state.tuning.cart));
    state.player.yaw = state.cart.pose.yaw;
    state.look.head_yaw = 1.6f;
    state.look.pitch = 0.4f;
    check_same_on_screen(props_seen());
}

TEST_CASE("my beer can tips towards me to pour, its opening facing me and the beer in view") {
    game_state state = started_game(fixture_hub_course());
    const render_data data = frame_for(state);
    const glm::mat4 view = glm::lookAt(data.camera_position, data.camera_target, world_up);
    // The narrowest picture (wider windows only add width).
    const glm::mat4 projection = glm::perspective(glm::radians(data.viewmodel_fov_degrees), 4.0f / 3.0f, 0.05f, 100.0f);
    const auto in_view = [&](const glm::vec3& world) {
        const glm::vec4 clip = projection * view * glm::vec4(world, 1.0f);
        return clip.w > 0.0f && std::abs(clip.x / clip.w) < 0.95f && std::abs(clip.y / clip.w) < 0.95f;
    };
    const glm::vec3 ahead = glm::normalize(data.camera_target - data.camera_position);

    const prop_holder pouring = camera_prop_holder(data.camera_position, data.camera_target, std::nullopt, held("beer_can", 1.0f));
    const glm::mat4 can = held_can_frame(pouring, 1.0f);
    const glm::vec3 opening(can * glm::vec4(test_props().at("beer_can").point("pour"), 1.0f));
    const glm::vec3 top = glm::normalize(glm::vec3(can[1]));
    CHECK(in_view(opening));
    CHECK(glm::dot(top, data.camera_position - opening) > 0.0f);
    // Tipped from how it is first held: its top leans towards me.
    const glm::vec3 held_top = glm::normalize(glm::vec3(held_can_frame(pouring, 0.0f)[1]));
    CHECK(glm::dot(top, ahead) < glm::dot(held_top, ahead) - 0.3f);

    prop_batch batch;
    append_held_props(batch, pouring, test_scene_colors().glow);
    const glm::vec3 beer = test_props().at("beer_can").color("beer");
    CHECK(std::any_of(batch.blended().begin(), batch.blended().end(),
                      [&](const prop_vertex& vertex) { return vertex.color == beer && in_view(vertex.position); }));
}

TEST_CASE("tracking noise eases in while paused and after a hole changes, never in play") {
    tape_style tape;
    tape.tracking_burst_seconds = 1.0f;
    tape.tracking_fade_seconds = 0.5f;
    const auto run = [&tape](tape_tracking_state state, const tape_tracking_signals& signals, const float seconds) {
        for (float t = 0.0f; t < seconds; t += 0.05f) {
            state = update_tape_tracking(state, signals, 0.05f, tape);
        }
        return state;
    };

    const tape_tracking_state paused = run({}, {true, false, false}, 0.25f);
    CHECK(paused.level > 0.3f);
    CHECK(paused.level < 0.7f);
    CHECK(run(paused, {true, false, false}, 1.0f).level == 1.0f);
    CHECK(run(paused, {false, false, false}, 1.0f).level == 0.0f);
    CHECK(update_tape_tracking(paused, {true, false, true}, 0.05f, tape).level == 0.0f);

    // A hole starting (or ending) runs a burst, then it fades out.
    const tape_tracking_state started = update_tape_tracking({}, {false, true, false}, 0.05f, tape);
    CHECK(started.burst_left == tape.tracking_burst_seconds);
    CHECK(run(started, {false, true, false}, 0.6f).level == 1.0f);
    CHECK(run(started, {false, true, false}, 2.0f).level == 0.0f);
    CHECK(run({}, {false, false, false}, 1.0f).level == 0.0f);
}

TEST_CASE("tracking signals: a hole is being played, and play means anything but walking") {
    game_state state = started_hole();
    state.mode = game_mode::walking;
    tape_tracking_signals signals = tape_tracking_signals_for(state, true);
    CHECK(signals.paused);
    CHECK(signals.on_hole);
    CHECK(!signals.in_play);
    state.mode = game_mode::following_shot;
    CHECK(tape_tracking_signals_for(state, false).in_play);
}

TEST_CASE("a tipped cart is seen lying on the side it rolled onto, a sunk one resting on its pond's bed") {
    game_state state = started_game(fixture_hub_course());
    // Standing behind my cart, looking along it.
    const glm::vec3 ahead = yaw_direction(state.cart.pose.yaw);
    state.player.position = anchor_on_terrain(state.area, state.cart.pose.position - ahead * 8.0f);
    state.player.yaw = state.cart.pose.yaw;
    state.look.pitch = 0.0f;
    const prop_model& model = test_props().at("golf_cart");
    const glm::vec3 roof_local(0.0f, 2.0f, 0.0f);

    for (const cart_condition side : {cart_condition::on_left_side, cart_condition::on_right_side}) {
        state.cart.motion.condition = side;
        const render_data data = frame_for(state);
        REQUIRE(data.carts.size() == 1U);
        const render_cart& drawn = data.carts[0];
        CHECK(!drawn.driven);
        const glm::mat4 view = glm::lookAt(data.camera_position, data.camera_target, world_up);
        const glm::mat4 projection = glm::perspective(glm::radians(test_fov_degrees), 4.0f / 3.0f, 0.05f, 500.0f);
        const auto screen_x = [&](const glm::vec3& world) {
            const glm::vec4 clip = projection * view * glm::vec4(world, 1.0f);
            return clip.x / clip.w;
        };
        // Its roof lies over to that side of it on screen (+x is the screen's right).
        const glm::vec3 roof(cart_frame(drawn) * glm::vec4(roof_local, 1.0f));
        const float roof_x = screen_x(roof) - screen_x(state.cart.pose.position);
        CHECK((side == cart_condition::on_left_side ? roof_x < -0.05f : roof_x > 0.05f));
        // Lying on the ground, not in it nor floating: its lowest point just over it.
        prop_batch batch;
        append_cart_model(batch, drawn, model, test_scene_colors().glow);
        float lowest = 1000.0f;
        for (const prop_vertex& vertex : batch.opaque()) {
            lowest = std::min(lowest, vertex.position.y - terrain_height(state.area, vertex.position));
        }
        CHECK(lowest > -0.15f);
        CHECK(lowest < 0.15f);
    }

    // Sunk in a pond shallower than it is tall: on its wheels on the bed, its
    // lower half under the water and its top above it, never through the ground.
    state.cart.motion.condition = cart_condition::sunk;
    state.area.zones.push_back(circle_zone(material_zone_type::water, state.cart.pose.position, 8.0f));
    const float surface = state.cart.pose.position.y + 0.6f;
    state.area.water_levels.push_back(surface);
    const render_data data = frame_for(state);
    REQUIRE(data.carts.size() == 1U);
    CHECK(near(data.carts[0].up, world_up, 0.05f));
    prop_batch batch;
    append_cart_model(batch, data.carts[0], model, test_scene_colors().glow);
    float lowest = 1000.0f;
    float highest = -1000.0f;
    for (const prop_vertex& vertex : batch.opaque()) {
        lowest = std::min(lowest, vertex.position.y - terrain_height(state.area, vertex.position));
        highest = std::max(highest, vertex.position.y);
    }
    CHECK(lowest > -0.05f);
    CHECK(lowest < 0.15f);
    CHECK(highest > surface);
}

namespace {
// The picture's lines (assets/picture/picture.json target_lines).
constexpr float scene_lines = 360.0f;

// Where `world` lands in the scene picture at `aspect`, in scene pixels from
// its middle (+x right, +y up); nothing when it is behind the eye.
std::optional<glm::vec2> scene_pixel(const render_data& data, const float aspect, const glm::vec3& world) {
    const glm::mat4 view = glm::lookAt(data.camera_position, data.camera_target, world_up);
    const glm::mat4 projection = glm::perspective(glm::radians(data.camera_fov_degrees), aspect, 0.05f, 2000.0f);
    const glm::vec4 clip = projection * view * glm::vec4(world, 1.0f);
    if (clip.w <= 0.0f) {
        return std::nullopt;
    }
    return glm::vec2(clip.x / clip.w * scene_lines * aspect, clip.y / clip.w * scene_lines) * 0.5f;
}

bool in_picture(const glm::vec2& pixel, const float aspect) {
    return std::abs(pixel.x) < 0.5f * scene_lines * aspect && std::abs(pixel.y) < 0.5f * scene_lines;
}

constexpr float aspects[] = {4.0f / 3.0f, 16.0f / 9.0f, 21.0f / 9.0f};
}

TEST_CASE("in the target view the line I aim along runs out ahead into the picture, for either hand") {
    for (const handedness hand : {handedness::right, handedness::left}) {
        game_state state = started_hole();
        state.hand = hand;
        enter_addressing(state);
        const render_data data = frame_for(state);
        const glm::vec3 far = state.address.setup.ball_start + yaw_direction(state.address.setup.aim_angle) * 150.0f;
        for (const float aspect : aspects) {
            const std::optional<glm::vec2> seen = scene_pixel(data, aspect, far);
            REQUIRE(seen.has_value());
            CHECK(in_picture(*seen, aspect));
            // Near the middle across: the eyes are just to one side of the line.
            CHECK(std::abs(seen->x) < 0.1f * scene_lines);
        }
    }
}

TEST_CASE("the down view shows the ball, the face's line and the stick, and a few degrees of face reads in scene pixels") {
    for (const handedness hand : {handedness::right, handedness::left}) {
        game_state state = started_hole();
        state.hand = hand;
        enter_addressing(state);
        state.address.look = down_look(state.tuning.camera);
        const render_data data = frame_for(state);
        REQUIRE(data.address_marks.has_value());
        const address_marks marks = my_address_marks(state);

        // Opened 5 degrees: the toe of the face's line moves back from the target.
        game_state opened = state;
        opened.address.setup.face_open += glm::radians(5.0f);
        const address_marks open_marks = my_address_marks(opened);
        // Played one step forward: the stance, and my eyes with it, move back, so the ball moves in the picture.
        game_state forward = state;
        forward.address.setup.ball_position += state.tuning.setup.ball_position_step;
        const render_data forward_data = frame_for(forward);

        for (const float aspect : aspects) {
            for (const glm::vec3& point : {marks.ball, marks.face_heel, marks.face_toe, marks.stick_back, marks.stick_front,
                                           marks.lead_foot, marks.trail_foot}) {
                const std::optional<glm::vec2> seen = scene_pixel(data, aspect, point);
                REQUIRE(seen.has_value());
                CHECK(in_picture(*seen, aspect));
            }
            const glm::vec2 toe = *scene_pixel(data, aspect, marks.face_toe);
            const glm::vec2 open_toe = *scene_pixel(data, aspect, open_marks.face_toe);
            CHECK(glm::length(open_toe - toe) >= 2.0f);
            const glm::vec2 ball = *scene_pixel(data, aspect, marks.ball);
            const glm::vec2 target_side =
                *scene_pixel(data, aspect, marks.ball + yaw_direction(state.address.setup.aim_angle) * 0.3f) - ball;
            CHECK(glm::dot(open_toe - toe, target_side) < 0.0f);
            CHECK(glm::length(*scene_pixel(forward_data, aspect, marks.ball) - ball) >= 2.0f);
        }
    }
}

TEST_CASE("a left-hander stands, looks and opens the face mirrored, and the physics sees world angles") {
    game_state right = started_hole();
    game_state left = started_hole();
    left.hand = handedness::left;
    enter_addressing(right);
    enter_addressing(left);
    const glm::vec3 ball = right.address.setup.ball_start;
    const glm::vec3 forward = yaw_direction(right.address.setup.aim_angle);
    const glm::vec3 side = yaw_left(forward);
    // Stances and eyes either side of the line, the same distance out.
    const float right_out = glm::dot(right.player.position - ball, side);
    const float left_out = glm::dot(left.player.position - ball, side);
    CHECK(right_out > 0.0f);
    CHECK(near(left_out, -right_out, 0.001f));
    CHECK(glm::dot(my_address_eye(right).position - ball, side) > 0.0f);
    CHECK(glm::dot(my_address_eye(left).position - ball, side) < 0.0f);
    // Both target views look the same way down the line.
    CHECK(near(horizontal(my_address_eye(right).direction), horizontal(my_address_eye(left).direction), 0.0001f));
    // Q opens either face: a right-hander's to the right of the path, a left-hander's to the left.
    game_input q;
    q.face_open_held = true;
    update_game(right, q, 0.016f);
    update_game(left, q, 0.016f);
    CHECK(wrap_angle(world_face_yaw(right.address.setup) - right.address.setup.aim_angle) < 0.0f);
    CHECK(wrap_angle(world_face_yaw(left.address.setup) - left.address.setup.aim_angle) > 0.0f);
    // The swing's club and figure are drawn mirrored too.
    const render_data right_frame = frame_for(right);
    const render_data left_frame = frame_for(left);
    REQUIRE(right_frame.my_club.has_value());
    REQUIRE(left_frame.my_club.has_value());
    CHECK(!right_frame.my_club->left_handed);
    CHECK(left_frame.my_club->left_handed);
    CHECK(right_frame.my_club->face_turn < 0.0f);
    CHECK(left_frame.my_club->face_turn > 0.0f);
}

TEST_CASE("over the ball the frame outlines what the wheel would change, and shows the key hints until the swing") {
    game_state state = started_hole();
    enter_addressing(state);
    render_data data = frame_for(state);
    CHECK(data.show_address_hints);
    CHECK(data.show_swing_clock);
    CHECK(data.swing_backswing == state.address.setup.backswing);
    CHECK(data.setup_outline.empty());  // the target view: the wheel sets the backswing

    state.address.look = down_look(state.tuning.camera);
    const eye_view eye = my_address_eye(state);
    const glm::vec3 to_ball = state.address.setup.ball_start - eye.position;
    state.address.look.yaw = wrap_angle(yaw_towards(eye.position, state.address.setup.ball_start) - address_facing_yaw(state.address.setup));
    state.address.look.pitch = std::atan2(to_ball.y, glm::length(horizontal(to_ball)));
    data = frame_for(state);
    REQUIRE(my_pointed_part(state) == setup_part::ball);
    REQUIRE(data.setup_outline.size() == 1U);
    CHECK(near(data.setup_outline[0], data.ball_position));

    update_game(state, swing_input(), 0.016f);
    data = frame_for(state);
    CHECK(!data.show_address_hints);
    CHECK(data.setup_outline.empty());
    // The club goes back with the swing.
    update_game(state, game_input{}, 0.3f);
    REQUIRE(frame_for(state).my_club.has_value());
    CHECK(frame_for(state).my_club->raise > 0.0f);
}

TEST_CASE("the ball is drawn right over where it is, near a cup too, and the cup as big as it plays") {
    game_state state = started_hole();
    const cup& hole = state.area.cups[state.ball->hole];
    const world_scale_tuning& scale = state.tuning.scale;
    for (const float out : {0.0f, 0.5f * hole.radius, hole.radius, 0.3f, 2.0f}) {
        const glm::vec3 at = hole.centre + glm::vec3(out, 0.0f, 0.0f);
        const glm::vec3 drawn = drawn_ball_center(at, scale);
        CHECK(near(horizontal(drawn), horizontal(at)));
        // Its bottom where the real ball's is.
        CHECK(near(drawn.y - scale.ball_visual_radius_meters, at.y - scale.ball_physics_radius_meters, 0.0001f));
    }
    const render_data data = frame_for(state);
    CHECK(near(data.cup_radius_meters, hole.radius * scale.meters_per_world_unit, 0.0001f));
    // The drawn ball fits the drawn cup with room to drop, as a real one does.
    CHECK(scale.ball_visual_radius_meters < 0.6f * data.cup_radius_meters);
}

TEST_CASE("with the putter the preview is a start line on the green along where the ball starts, not an arc") {
    for (const handedness hand : {handedness::right, handedness::left}) {
        game_state state = started_hole();
        state.hand = hand;
        state.selected_club = state.clubs.size() - 1;
        REQUIRE(state.clubs[state.selected_club].club_class == "putter");
        enter_addressing(state);
        render_data data = frame_for(state);
        CHECK(!data.osd_landing.has_value());
        CHECK(!data.osd_rest.has_value());
        const std::vector<glm::vec3>& putt_line = data.osd_line;
        REQUIRE(putt_line.size() >= 2U);
        const shot_setup& setup = state.address.setup;
        const shot_preview_tuning& tuning = state.tuning.shot_preview;
        const float unit = state.tuning.scale.meters_per_world_unit;
        CHECK(near(horizontal(putt_line.front()), horizontal(setup.ball_start), 0.0001f));
        CHECK(near(horizontal_distance(putt_line.front(), putt_line.back()), tuning.putt_line_meters / unit, 0.001f));
        for (const glm::vec3& point : putt_line) {
            CHECK(near(point.y, terrain_height(state.area, point), 0.0001f));
        }
        // Square, straight along the aim.
        const float square = yaw_towards(putt_line.front(), putt_line.back());
        CHECK(near(wrap_angle(square - setup.aim_angle), 0.0f, 0.001f));

        // Opened, it starts the way the face points: right for a right-hander, left for a left-hander.
        game_input open;
        open.face_open_held = true;
        for (int i = 0; i < 30; ++i) {
            update_game(state, open, 0.016f);
        }
        data = frame_for(state);
        const float turned = wrap_angle(yaw_towards(data.osd_line.front(), data.osd_line.back()) - state.address.setup.aim_angle);
        CHECK((hand == handedness::right ? turned < -0.01f : turned > 0.01f));
        CHECK(std::abs(turned) < std::abs(wrap_angle(world_face_yaw(state.address.setup) - state.address.setup.aim_angle)));
    }
    // Every other club shows its flight to a landing.
    game_state iron = started_hole();
    enter_addressing(iron);
    const render_data data = frame_for(iron);
    CHECK(data.osd_landing.has_value());
}

TEST_CASE("my bag is drawn where it is: behind my cart's driver's seat, stood on the ground leant back, on my back, and on the map") {
    game_state state = started_hole();
    state.course_map_active = true;
    render_data data = frame_for(state);
    REQUIRE(!data.carts.empty());
    const render_cart& cart = data.carts[0];
    REQUIRE(cart.bags.count("driver_bag") == 1U);
    CHECK(data.my_figure->bag_model.empty());
    const prop_model& bag = test_props().at(cart.bags.at("driver_bag"));
    const prop_model& cart_model = test_props().at(cart.model);
    const glm::mat4 strapped = mounted_frame(cart_frame(cart), cart_model.mount("driver_bag"), bag);
    const glm::vec3 bottom(strapped * glm::vec4(bag.point("hang"), 1.0f));
    const glm::vec3 top(strapped * glm::vec4(bag.point("hang") + glm::vec3(0.0f, 0.8f, 0.0f), 1.0f));
    const glm::vec3 forward = yaw_direction(cart.yaw);
    // Behind the seat, on the driver's (left) side, standing up and leaning back.
    CHECK(glm::dot(bottom - cart.position, forward) < -0.4f);
    CHECK(glm::dot(bottom - cart.position, yaw_left(forward)) > 0.1f);
    CHECK(top.y > bottom.y + 0.6f);
    CHECK(glm::dot(top - bottom, forward) < 0.0f);
    REQUIRE(data.map_bag_position.has_value());
    CHECK(horizontal_distance(*data.map_bag_position, bag_position(state)) < 0.001f);

    // On my back: no bag on the cart or the map, and it is slung on my figure.
    state.player.position = bag_position(state);
    game_input b;
    b.move_bag = true;
    update_game(state, b, 0.016f);
    REQUIRE(bag_on_back(state));
    state.course_map_active = true;
    data = frame_for(state);
    CHECK(data.carts[0].bags.empty());
    CHECK(!data.map_bag_position.has_value());
    REQUIRE(data.my_figure.has_value());
    CHECK(data.my_figure->bag_model == "golf_bag");
    const glm::mat4 slung = back_bag_frame(*data.my_figure, data.avatar_eye_height, bag);
    const glm::vec3 slung_bottom(slung * glm::vec4(bag.point("hang"), 1.0f));
    CHECK(glm::dot(slung_bottom - state.player.position, yaw_direction(state.player.yaw)) < 0.0f);

    // Set down away from the cart: standing on the ground beside me, leant back from its pocket.
    state.player.position += glm::vec3(0.0f, 0.0f, 12.0f);
    state.player.position.y = terrain_height(state.area, state.player.position);
    update_game(state, b, 0.016f);
    REQUIRE(state.bag.where == bag_where::on_ground);
    data = frame_for(state);
    REQUIRE(data.ground_bags.size() == 1U);
    const render_ground_bag& down = data.ground_bags[0];
    const glm::mat4 stood = ground_bag_frame(down, bag);
    const glm::vec3 foot(stood * glm::vec4(bag.point("hang"), 1.0f));
    const glm::vec3 head(stood * glm::vec4(bag.point("hang") + glm::vec3(0.0f, 0.8f, 0.0f), 1.0f));
    CHECK(near(foot.y, terrain_height(state.area, foot), 0.05f));
    CHECK(head.y > foot.y + 0.7f);
    CHECK(glm::dot(head - foot, yaw_direction(down.yaw)) < 0.0f);
}
