// A headless client that plays a little online against a running server and
// says whether the server answered as the game expects: it signs in
// anonymously, claims a name, joins a course and runs each check below. For
// checking a module before release, against a local server
// (tooling/net/online_check/README.md).
//
//   golfpp-online-check <server uri> <database> [course id]

#include "game/game_content.h"
#include "game/game_state.h"
#include "game/net_types.h"
#include "game/progression.h"
#include "game/server_errors.h"
#include "net/net_client.h"
#include "net/online_config.h"
#include "game/content_files.h"
#include "game/course_session.h"
#include "game/golf_bag.h"
#include "game/save_data.h"
#include "game/mode_dispatch.h"
#include "game/remote_players.h"
#include "game/motion_sync.h"
#include "physics/vector_math.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <thread>

namespace {
constexpr auto poll_interval = std::chrono::milliseconds(20);
constexpr auto answer_timeout = std::chrono::seconds(15);

class checker {
public:
    checker(net_client client, game_state state) : client_(std::move(client)), state_(std::move(state)) {}

    // Updates the connection until `done` holds, or the timeout.
    bool until(const std::function<bool(const game_state&)>& done) {
        const auto give_up = std::chrono::steady_clock::now() + answer_timeout;
        while (std::chrono::steady_clock::now() < give_up) {
            client_.update(state_);
            if (done(state_)) {
                return true;
            }
            std::this_thread::sleep_for(poll_interval);
        }
        return false;
    }

    // Real frames at real elapsed time, so server speed budgets see the same clock.
    void play_for(const float seconds, const game_input& input = {}) {
        const auto end = std::chrono::steady_clock::now() + std::chrono::duration<float>(seconds);
        auto previous = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() < end) {
            std::this_thread::sleep_for(poll_interval);
            const auto now = std::chrono::steady_clock::now();
            update_game(state_, input, std::chrono::duration<float>(now - previous).count());
            previous = now;
            client_.update(state_);
        }
    }

    // The next refusal of `reducer`, when one arrives.
    std::optional<std::string> refusal_of(const std::string& reducer) {
        std::optional<std::string> found;
        until([&](const game_state& state) {
            for (const reducer_failure& refusal : state.online.refusals) {
                if (refusal.reducer == reducer) {
                    found = refusal.error;
                }
            }
            return found.has_value();
        });
        state_.online.refusals.clear();
        return found;
    }

    void check(const char* what, const bool passed) {
        std::printf("%s  %s\n", passed ? "PASS" : "FAIL", what);
        failures_ += passed ? 0 : 1;
    }

    void send(net_command command) { state_.net_commands.push_back(std::move(command)); }

    net_client& client() { return client_; }
    game_state& state() { return state_; }
    int failures() const { return failures_; }

private:
    net_client client_;
    game_state state_;
    int failures_ = 0;
};

net_command use(const std::string& item) {
    net_command command;
    command.type = net_command_type::use_item;
    command.item_id = item;
    return command;
}

void check_items(checker& run) {
    game_state& state = run.state();
    const std::uint64_t me = state.online.account_id;
    run.check("my loadout row arrives with the defaults",
              run.until([me](const game_state& s) { return s.online.loadouts.count(me) == 1; }) &&
                  state.online.loadouts.at(me).smokable == state.default_loadout.smokable);

    const item_definition& smokable = *state.items.find(state.default_loadout.smokable);
    const xp_reward earned = *smokable.use->xp_per_use;
    const std::int64_t before = skill_xp(state.online.progress.skills, earned.skill_id);
    run.send(use(smokable.id));
    run.check("using what I smoke earns its XP", run.until([&](const game_state& s) {
        return skill_xp(s.online.progress.skills, earned.skill_id) >= before + earned.xp;
    }));
    run.send(use(smokable.id));
    run.check("using it again while its use plays is refused", run.refusal_of(reducer_use_item) == error_too_soon);
    run.send(use(state.default_loadout.cart));
    run.check("a cart is not used", run.refusal_of(reducer_use_item) == error_unknown_item);
    run.send(use("no_such_item"));
    run.check("an unknown item is refused", run.refusal_of(reducer_use_item) == error_unknown_item);
}

// My cart, parked on the ground: steps at its wheel before the server gives
// it to me are refused, and it will not right a cart on its wheels.
void check_carts(checker& run) {
    game_state& state = run.state();
    const std::uint64_t me = state.online.account_id;
    run.check("my cart is parked on its wheels", run.until([me](const game_state& s) {
        const auto cart = s.online.carts.find(me);
        return cart != s.online.carts.end() && cart->second.condition == cart_condition::upright;
    }));
    net_command drive;
    drive.type = net_command_type::motion;
    drive.motion = current_motion(state);
    drive.motion.mode = motion_mode::cart;
    move_input pedal;
    pedal.kind = move_kind::cart;
    pedal.forward = 1;
    drive.first_step = state.motion_sync.step + 1;
    drive.runs = {move_run{pedal, 30}};
    state.motion_sync.step += 30;
    run.send(drive);
    run.check("steps at a wheel the server has not given me are refused",
              run.refusal_of(reducer_update_motion) == error_not_in_cart);
    net_command right;
    right.type = net_command_type::right_cart;
    right.cart_owner = me;
    run.send(right);
    run.check("a cart on its wheels is not righted", run.refusal_of(reducer_right_cart) == error_cart_upright);
}

// Walking is the server's: it plays my steps to where I am.
void check_walking(checker& run) {
    game_state& state = run.state();
    game_input forward;
    forward.forward_held = true;
    run.play_for(1.0f, forward);
    run.play_for(0.5f);
    const std::uint64_t me = state.online.account_id;
    run.check("the server's steps leave me where mine did", run.until([me](const game_state& s) {
        const auto self = s.online.players.find(me);
        return self != s.online.players.end() && self->second.motion &&
            glm::length(self->second.motion->position - s.player.position) <= s.tuning.net.cart_reconcile_distance;
    }));
    run.check("walking caused no sync breach", state.motion_sync.breaches == 0);
}

// Enter through the door, drive, stop, step out; both clients must use the
// row left behind, even after the final driving motion has been acknowledged.
void check_cart_sync(checker& run) {
    game_state& state = run.state();
    game_input forward;
    forward.forward_held = true;
    for (int i = 0; i < 500 && on_foot(state); ++i) {
        const auto cart = shown_cart(state, 0);
        if (!cart) { break; }
        const glm::vec3 door = seat_position(*cart, cart_seat::driver, state.tuning.cart);
        state.player.yaw = yaw_towards(state.player.position, door);
        run.play_for(0.02f, forward);
    }
    run.play_for(state.tuning.cart.step_seconds + 0.4f);
    run.check("the server confirms my wheel before driving", driving_cart(state) && state.ride->confirmed);
    if (!driving_cart(state)) { return; }
    run.play_for(0.8f, forward);
    run.play_for(0.5f);
    game_input out;
    out.interact = true;
    run.play_for(0.02f, out);
    run.play_for(state.tuning.cart.step_seconds + state.tuning.net.remote_correction_seconds + 0.4f);
    const std::uint64_t me = state.online.account_id;
    run.check("stepping out releases the server's wheel", run.until([me](const game_state& s) {
        const auto cart = s.online.carts.find(me);
        return cart != s.online.carts.end() && cart->second.driver == 0;
    }));
    const auto row = state.online.carts.find(me);
    const auto shown = shown_cart(state, 0);
    run.check("the parked cart equals its server row", row != state.online.carts.end() && shown &&
        glm::length(row->second.position - shown->position) <= state.tuning.net.cart_reconcile_distance &&
        std::abs(wrap_angle(row->second.yaw - shown->yaw)) <= state.tuning.net.cart_reconcile_yaw);
    run.check("driving and stepping out caused no sync breach", state.motion_sync.breaches == 0);
}

// A course without carts: none of mine, my bag on my back.
void check_no_cart(checker& run) {
    const std::uint64_t me = run.state().online.account_id;
    run.check("my bag arrives on my back", run.until([me](const game_state& s) {
        const auto bag = s.online.bags.find(me);
        return bag != s.online.bags.end() && bag->second.place.where == bag_where::on_back;
    }));
    run.check("no cart of mine is parked", run.state().online.carts.count(me) == 0);
}
}

// Tees up on hole 1 and hits a drive: the server's rest is where this
// client's own simulation of the same shot rests.
void check_shot(checker& run) {
    game_state& state = run.state();
    run.check("tees up on hole 1", tee_up(state, 0, false));
    send_motion_now(state);
    const std::uint64_t me = state.online.account_id;
    run.check("the server tees my ball up", run.until([me](const game_state& s) { return s.online.balls.count(me) == 1; }));
    if (state.online.balls.count(me) == 0) {
        return;
    }
    const glm::vec3 ball = state.online.balls.at(me).position;
    // The longest club is in hand when a room is joined, the bag on the cart.
    shot_setup input = stock_setup(state.clubs.front(), lie_at(state.area, ball), ball,
                                   yaw_towards(ball, pin_anchor_position(state, 0)), handedness::right, 0.0f, state.tuning.setup);
    input.backswing = 0.9f;
    input.shot_at = shot_unix_ms(state);
    net_command shot;
    shot.type = net_command_type::take_shot;
    shot.shot = input;
    shot.stroke = 1;
    run.send(std::move(shot));
    const bool answered = run.until([me](const game_state& s) {
        for (const room_shot& played : s.online.shots) {
            if (played.account_id == me) {
                return true;
            }
        }
        return false;
    });
    run.check("the server plays my shot", answered);
    if (!answered) {
        return;
    }
    const room_shot& played = *std::find_if(state.online.shots.begin(), state.online.shots.end(),
                                            [me](const room_shot& each) { return each.account_id == me; });
    // As the server played it: its dice, the room as it stood, the golfer and the flags then.
    const shot_result here = room_shot_result(state, played);
    std::printf("      server rest (%.3f, %.3f, %.3f), here (%.3f, %.3f, %.3f)\n", played.rest.x, played.rest.y, played.rest.z,
                here.rest_position.x, here.rest_position.y, here.rest_position.z);
    run.check("the server's rest is this client's own", glm::length(played.rest - here.rest_position) < 0.001f);
    run.check("the shot carries my level in every listed skill", played.golfer.levels.size() == state.skills.skills.size());
}

// Off a practice ground, the shot pays its XP into my skill (and its award
// into the server's ledger).
void check_shot_xp(checker& run, const std::int64_t xp_before) {
    const xp_reward& earned = run.state().rewards.shot;
    run.check("the shot earns its XP", run.until([&](const game_state& s) {
        return skill_xp(s.online.progress.skills, earned.skill_id) >= xp_before + earned.xp;
    }));
}

// On a practice ground: the shot earned nothing, and the same station drops
// a fresh ball again at once.
void check_practice(checker& run, const std::string& progress_before) {
    game_state& state = run.state();
    const std::uint64_t me = state.online.account_id;
    run.check("practice earns no XP", save_data_to_json(active_progress(state)) == progress_before);
    run.check("tees up the same station again", tee_up(state, 0, false));
    run.check("the server drops a fresh ball there", run.until([me](const game_state& s) {
        const auto ball = s.online.balls.find(me);
        return ball != s.online.balls.end() && ball->second.stroke_count == 0;
    }));
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: golfpp-online-check <server uri> <database> [course id]\n");
        return 2;
    }
    const std::string asset_root = GOLFPP_ASSETS_DIR;
    game_content_load_result content = load_game_content(asset_root);
    const std::optional<std::string> config_text = read_text_file(std::filesystem::path(asset_root) / online_config_path);
    const std::optional<online_config> config = config_text ? parse_online_config_from_text(*config_text) : std::nullopt;
    if (!content.content || !config) {
        std::fprintf(stderr, "cannot load content: %s\n", content.error.c_str());
        return 2;
    }
    std::optional<net_client> client = net_client::create(with_overrides(*config, argv[1], argv[2], true), "", "", content.content->content_hash);
    if (!client) {
        std::fprintf(stderr, "the client bridge does not match src/net/stdb_bridge.h\n");
        return 2;
    }
    game_state state = make_game_state(*content.content, save_data{});
    state.play = play_mode::online;
    checker run(std::move(*client), std::move(state));

    run.client().begin_login();
    const bool connected = run.until([](const game_state& s) {
        return s.online.status == net_status::connected && s.online.account_id != 0;
    });
    run.check("signs in anonymously and reads its account", connected);
    if (!connected) {
        std::fprintf(stderr, "%s\n", run.client().failure().c_str());
        return 1;
    }
    run.client().claim_name("CHECK" + std::to_string(run.state().online.account_id % 10000));
    run.check("claims a name", run.until([](const game_state& s) { return !s.online.display_name.empty(); }));
    const std::string course = argc > 3 ? argv[3] : "marienlyst_golfklub";
    run.client().join_course(course, true);
    run.check("joins a room on the course", run.until([](const game_state& s) { return s.online.room.has_value() && s.online.room_ready; }));

    check_items(run);
    const std::vector<course_definition>& courses = content.content->courses;
    const auto played = std::find_if(courses.begin(), courses.end(),
                                     [&course](const course_definition& each) { return each.id == course; });
    if (played != courses.end() && !played->carts) {
        check_no_cart(run);
    } else {
        check_carts(run);
    }
    const bool started = played != courses.end() && start_course(run.state(), *played);
    run.check("plays the room's course here too", started);
    if (started && !played->carts) {
        // Off my back before I can hit: B at my feet, as the server allows it.
        move_bag(run.state());
        const std::uint64_t me = run.state().online.account_id;
        run.check("sets my bag down", run.until([me](const game_state& s) {
            const auto bag = s.online.bags.find(me);
            return bag != s.online.bags.end() && bag->second.place.where == bag_where::on_ground;
        }));
    }
    if (started) {
        check_walking(run);
    }
    if (started && played->carts) {
        check_cart_sync(run);
        // Driving ends away from the tee. Rejoin for the shot scenario,
        // also proving that the motion views deliver a fresh room snapshot.
        run.client().leave_room();
        run.check("leaves the driving room", run.until([](const game_state& s) { return !s.online.room; }));
        run.client().join_course(course, true);
        run.check("rejoins with a complete motion snapshot", run.until([](const game_state& s) {
            const auto self = s.online.players.find(s.online.account_id);
            return s.online.room && s.online.room_ready && self != s.online.players.end() && self->second.motion &&
                s.online.carts.count(s.online.account_id) == 1;
        }));
        run.check("starts the shot scenario at its tee", start_course(run.state(), *played));
    }
    if (started) {
        const std::string progress_before = save_data_to_json(active_progress(run.state()));
        const std::int64_t xp_before = skill_xp(active_progress(run.state()).skills, run.state().rewards.shot.skill_id);
        check_shot(run);
        if (played->practice) {
            check_practice(run, progress_before);
        } else {
            check_shot_xp(run, xp_before);
        }
    }

    run.client().leave_room();
    run.until([](const game_state& s) { return !s.online.room.has_value(); });
    std::printf("%s\n", run.failures() == 0 ? "all checks passed" : "some checks failed");
    return run.failures() == 0 ? 0 : 1;
}
