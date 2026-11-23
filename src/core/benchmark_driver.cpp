#include "core/benchmark_driver.h"
#include "core/benchmark_csv.h"
#include "game/course_session.h"
#include "game/green_read.h"
#include "game/mode_dispatch.h"
#include "game/sky_clock.h"
#include "physics/vector_math.h"
#include <cmath>

namespace {
constexpr int cold_directions = 32;
// A moving crowd advances a fixed step a frame, so a run draws the same motion at any frame rate.
constexpr float crowd_frames_per_second = 60.0f;
constexpr float walk_speed = 1.4f, walk_span = 12.0f;
constexpr float drive_speed = 4.5f, drive_span = 10.0f;

struct leg {
    float along;  // from the path's start
    bool returning;
};

// Where a body that has gone `distance` along a path of `span` meters, there and back, is.
leg back_and_forth(const float distance, const float span) {
    const float wrapped = std::fmod(distance, 2.0f * span);
    return wrapped < span ? leg{wrapped, false} : leg{2.0f * span - wrapped, true};
}

void face(game_state& game, const glm::vec3& feet, const glm::vec3& target) {
    game.player.position = anchor_on_terrain(game.area, feet);
    const glm::vec3 eye = game.player.position + world_up * my_eye_height(game);
    game.player.yaw = yaw_towards(eye, target);
    game.look.pitch = std::atan2(target.y - eye.y, horizontal_distance(eye, target));
}

void drive_ball(game_state& game) {
    if (!tee_up(game, 0, false)) { return; }
    const auto& club = game.clubs[game.selected_club];
    const auto start = game.ball->position;
    game.address.setup = stock_setup(club, lie_at(game.area, start), start, game.player.yaw,
                                     game.hand, 0, game.tuning.setup);
    auto course = current_shot_course(game);
    course.weather = course_weather_at(game, shot_unix_ms(game));
    play_shot(game, simulate_shot(game.address.setup, course, game.tuning, game.clubs));
}
}

benchmark_driver::benchmark_driver(std::ostream& frames, const game_state& game, const render_water& water,
                                   const int ms, const bool reverse)
    : frames_(frames), measured_ms_(ms), spawn_(game.player.position), heading_(game.player.yaw) {
    const auto scenes = benchmark_scenarios(game.hub.has_value(), !water.triangles.empty(),
                                            game.place.id == "marienlyst_golfklub", reverse);
    for (const auto& scene : scenes) { steps_.push_back({scene, phase::cold}); }
    for (const auto& scene : scenes) {
        steps_.push_back({scene, phase::warmup});
        steps_.push_back({scene, phase::steady});
    }
    // Aim at the interior of a broad water triangle, clear of its shoreline.
    float largest = 0;
    for (std::size_t i = 2; i < water.triangles.size(); i += 3) {
        const auto a = water.triangles[i-2].position, b = water.triangles[i-1].position, c = water.triangles[i].position;
        const float area = std::abs(glm::cross(b-a, c-a).y);
        if (area > largest) { largest = area; pond_ = (a+b+c) / 3.0f; }
    }
}

void benchmark_driver::enter(game_state& game, sky_controls& sky, god_menu_state& god) {
    const auto& scene = steps_[current_].scene;
    const auto view = scene.view;
    game.player = player_state{};
    game.player.position = spawn_;
    game.player.yaw = heading_;
    game.look = {};
    game.mode = game_mode::walking;
    game.shot.reset();
    game.ball.reset();
    game.flight_path_points.clear();
    god.weather = view == benchmark_view::shot ? weather_kind::rain : weather_kind::clear;
    god.wind_speed = view == benchmark_view::walk ? 10.0f : 2.0f;
    god.wind_heading = 0.0f;
    const bool night = view == benchmark_view::lamps || view == benchmark_view::zoom;
    sky.pinned_hour = night ? 0.0f : view == benchmark_view::walk ? 15.0f : 12.0f;
    sky.pinned_date = days_from_civil(1989, (night || view == benchmark_view::walk) ? 12 : 6, 21);

    if (view == benchmark_view::zoom) {
        const auto pin = pin_anchor_position(game, 0);
        face(game, pin - yaw_direction(heading_) * 18.0f, pin + world_up * pin_height_meters(game, 0) * 0.7f);
    } else if (view == benchmark_view::pond) {
        game.player.position = anchor_on_terrain(game.area, pond_);
    } else if (view == benchmark_view::range || view == benchmark_view::lamps) {
        const auto& mat = game.hub->tee_mats[game.hub->tee_mats.size()/2];
        face(game, mat.position, view == benchmark_view::range ? mat.aim : game.hub->floodlights.front().foot);
    } else if (view == benchmark_view::bus) {
        const auto bus = game.world.bus_stop->position;
        face(game, bus + glm::vec3(7,0,6), bus + world_up);
    } else if (view == benchmark_view::plaza) {
        const auto& pads = game.hub->anchors.pads;
        const auto plaza = std::find_if(pads.begin(), pads.end(), [](const hub_pad& pad) { return pad.name == "plaza"; });
        face(game, plaza->position + glm::vec3(0,0,12), anchor_on_terrain(game.area, plaza->position) + world_up);
    }
    figures_.clear();
    carts_.clear();
    balls_.clear();
    crowd_points_.clear();
    const auto forward = yaw_direction(game.player.yaw);
    crowd_forward_ = forward;
    for (int i = 0; i < scene.nearby; ++i) {
        const auto id = static_cast<std::uint64_t>(i + 1);
        const float distance = 10.0f + 4.0f * (i/5);
        const auto point = game.player.position + forward * distance + yaw_left(forward) * (i%5 - 2.0f) * 3.0f;
        crowd_points_.push_back(point);
        render_remote_avatar figure;
        figure.player_id = id;
        figure.position = anchor_on_terrain(game.area, point);
        figures_.push_back(figure);
        cart_in_world cart;
        cart.owner = id;
        cart.pose.position = anchor_on_terrain(game.area, point + forward * 1.6f);
        cart.item = game.default_loadout.cart;
        carts_.push_back(cart);
        balls_.push_back({anchor_on_terrain(game.area, point - forward) + world_up * game.tuning.scale.ball_visual_radius_meters, id});
    }
    view_heading_ = game.player.yaw;
}

bool benchmark_driver::prepare(game_state& game, sky_controls& sky, god_menu_state& god,
                               const std::uint64_t frame) {
    if (complete()) { return false; }
    frame_ = frame;
    const step& current = steps_[current_];
    if (frames_in_step_ == 0) {
        boundaries_.emplace(frame, current_);
        enter(game, sky, god);
    }
    const double duration = current.traversal == phase::steady ? measured_ms_ : std::min(measured_ms_, 1000);
    const float turn = current.traversal == phase::cold ? static_cast<float>(frames_in_step_) / cold_directions
                                                      : static_cast<float>(elapsed_ms_ / duration);
    if (current.traversal == phase::cold || current.scene.view == benchmark_view::sweep) {
        game.player.yaw = view_heading_ + glm::two_pi<float>() * turn;
    }
    pond_yaw_ = game.player.yaw;
    // Outside the frame's timed stages, as a server row's interpolation is.
    if (current.scene.moving) { move_crowd(game); }
    return true;
}

std::optional<camera_view> benchmark_driver::camera() const {
    if (steps_[current_].scene.view != benchmark_view::pond) { return std::nullopt; }
    const auto eye = pond_ + world_up * 2.2f;
    return camera_view{eye, eye + yaw_direction(pond_yaw_) - world_up * 0.28f, 0};
}

input_state benchmark_driver::keys() const {
    input_state input;
    const auto view = steps_[current_].scene.view;
    input.key_w.is_down = view == benchmark_view::walk;
    input.left_shift.is_down = input.key_w.is_down;
    input.ctrl.is_down = view == benchmark_view::zoom;
    return input;
}

void benchmark_driver::launch_if_due(game_state& game) {
    if (steps_[current_].scene.view == benchmark_view::shot && !game.shot) {
        game.ball.reset();
        drive_ball(game);
    }
}

void benchmark_driver::move_crowd(const game_state& game) {
    const float seconds = static_cast<float>(frames_in_step_) / crowd_frames_per_second;
    const glm::vec3 left = yaw_left(crowd_forward_);
    const float forward_yaw = yaw_towards(glm::vec3(0.0f), crowd_forward_);
    const float left_yaw = yaw_towards(glm::vec3(0.0f), left);
    for (std::size_t i = 0; i < figures_.size(); ++i) {
        const float stagger = static_cast<float>(i) * 3.1f;
        // Golfers walk up and down the fairway; carts drive across it.
        const leg walk = back_and_forth(walk_speed * seconds + stagger, walk_span);
        figures_[i].position = anchor_on_terrain(game.area, crowd_points_[i] + crowd_forward_ * (walk.along - walk_span / 2.0f));
        figures_[i].yaw = walk.returning ? forward_yaw + glm::pi<float>() : forward_yaw;
        const leg drive = back_and_forth(drive_speed * seconds + stagger, drive_span);
        carts_[i].pose.position = anchor_on_terrain(
            game.area, crowd_points_[i] + crowd_forward_ * 1.6f + left * (drive.along - drive_span / 2.0f));
        carts_[i].pose.yaw = drive.returning ? left_yaw + glm::pi<float>() : left_yaw;
        carts_[i].driven = true;
        carts_[i].steer = 0.3f * std::sin(1.5f * seconds + static_cast<float>(i));
        carts_[i].speed = drive_speed;
    }
}

void benchmark_driver::populate(render_data& data) {
    if (steps_[current_].scene.no_water) { data.water = nullptr; }
    data.remote_avatars.insert(data.remote_avatars.end(), figures_.begin(), figures_.end());
    data.remote_balls.insert(data.remote_balls.end(), balls_.begin(), balls_.end());
    observations_[frame_] = {data.player_position.x, data.player_position.z, data.lighting.sun_direction.y,
                             data.weather.surface_rain, data.ball_position.y, data.show_ball && data.show_flight_path,
                             data.show_rangefinder, data.rangefinder_flag.has_value(), data.remote_avatars.size(),
                             data.remote_balls.size(), data.carts.size()};
}

void benchmark_driver::advance(const double ms) {
    ++frames_in_step_;
    elapsed_ms_ += ms;
    const auto traversal = steps_[current_].traversal;
    const bool done = traversal == phase::cold ? frames_in_step_ >= cold_directions
        : elapsed_ms_ >= (traversal == phase::warmup ? std::min(measured_ms_, 1000) : measured_ms_);
    if (done) { ++current_; frames_in_step_ = 0; elapsed_ms_ = 0; }
}

void benchmark_driver::record(const frame_profile& frame) {
    const auto& origin = steps_[std::prev(boundaries_.upper_bound(frame.frame_number))->second];
    const char* label = origin.traversal == phase::cold ? "cold" : origin.traversal == phase::warmup ? "warmup" : "steady";
    benchmark_csv_frame(frames_, origin.scene, label, frame);
    const auto observed = observations_.find(frame.frame_number);
    if (observed == observations_.end()) { frames_ << ",,,,,,,,,,,\n"; return; }
    const auto& o = observed->second;
    frames_ << ',' << o.player_x << ',' << o.player_z << ',' << o.sun_y << ',' << o.rain << ',' << o.ball_y
            << ',' << o.moving_ball << ',' << o.zoom << ',' << o.flag_lock << ',' << o.figures << ',' << o.balls
            << ',' << o.carts << '\n';
    observations_.erase(observed);
}
