#pragma once

// Offline script for a single place. A boundary stays addressable until GPU queries flush.
#include "core/benchmark_scenarios.h"
#include "core/god_menu.h"
#include "game/golf_cart.h"
#include "renderer/camera_transition.h"
#include "renderer/water_batch.h"
#include <ostream>
#include <map>

class benchmark_driver {
public:
    benchmark_driver(std::ostream& frames, const game_state& game, const render_water& water, int ms, bool reverse);
    bool prepare(game_state& game, sky_controls& sky, god_menu_state& god, std::uint64_t frame);
    std::optional<camera_view> camera() const;
    input_state keys() const;
    void populate(render_data& data);
    // The crowd's carts, drawn through the frame's own cart path (make_render_data).
    const std::vector<cart_in_world>& crowd_carts() const { return carts_; }
    void launch_if_due(game_state& game);
    bool entering() const { return frames_in_step_ == 0; }
    void advance(double ms);
    void record(const frame_profile& frame);
    bool complete() const { return current_ == steps_.size(); }
private:
    enum class phase { cold, warmup, steady };
    struct step { benchmark_scenario scene; phase traversal; };
    void enter(game_state& game, sky_controls& sky, god_menu_state& god);
    void move_crowd(const game_state& game);
    struct observation {
        float player_x, player_z, sun_y, rain, ball_y;
        bool moving_ball, zoom, flag_lock;
        std::size_t figures, balls, carts;
    };
    std::map<std::uint64_t, observation> observations_;
    std::uint64_t frame_ = 0;
    std::ostream& frames_;
    int measured_ms_;
    std::vector<step> steps_;
    std::size_t current_ = 0;
    std::map<std::uint64_t, std::size_t> boundaries_;
    int frames_in_step_ = 0;
    double elapsed_ms_ = 0;
    glm::vec3 spawn_;
    float heading_;
    float view_heading_ = 0;
    float pond_yaw_ = 0;
    glm::vec3 pond_{0};
    std::vector<render_remote_avatar> figures_;
    std::vector<cart_in_world> carts_;
    std::vector<glm::vec3> crowd_points_;  // each figure's place when the view was entered
    glm::vec3 crowd_forward_{0.0f, 0.0f, 1.0f};
    std::vector<render_remote_ball> balls_;
};
