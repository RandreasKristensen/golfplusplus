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
#include "game/mode_dispatch.h"
#include "game/remote_players.h"
#include "game/motion_sync.h"
#include "physics/vector_math.h"

#include <algorithm>
#include <chrono>
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
    const int before = skill_xp(state.online.progress.skills, earned.skill_id);
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

// My cart, parked on the ground: the server will not have it tip or sink
// there, nor right it while it stands.
void check_carts(checker& run) {
    game_state& state = run.state();
    const std::uint64_t me = state.online.account_id;
    run.check("my cart is parked on its wheels", run.until([me](const game_state& s) {
        const auto cart = s.online.carts.find(me);
        return cart != s.online.carts.end() && cart->second.condition == cart_condition::upright;
    }));
    for (const cart_condition over : {cart_condition::on_left_side, cart_condition::sunk}) {
        net_command overturn;
        overturn.type = net_command_type::overturn_cart;
        overturn.overturned_to = over;
        run.send(overturn);
        run.check(over == cart_condition::sunk ? "it does not sink where it is parked" : "it does not tip where it is parked",
                  run.refusal_of(reducer_overturn_cart) == error_cart_steady);
    }
    net_command right;
    right.type = net_command_type::right_cart;
    right.cart_owner = me;
    run.send(right);
    run.check("a cart on its wheels is not righted", run.refusal_of(reducer_right_cart) == error_cart_upright);
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
    std::optional<net_client> client = net_client::create(with_overrides(*config, argv[1], argv[2], true), "", "");
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
    run.client().join_course(course);
    run.check("joins a room on the course", run.until([](const game_state& s) { return s.online.room.has_value(); }));

    check_items(run);
    check_carts(run);
    const std::vector<course_definition>& courses = content.content->courses;
    const auto played = std::find_if(courses.begin(), courses.end(),
                                     [&course](const course_definition& each) { return each.id == course; });
    const bool started = played != courses.end() && start_course(run.state(), *played);
    run.check("plays the room's course here too", started);
    if (started) {
        check_shot(run);
    }

    run.client().leave_room();
    run.until([](const game_state& s) { return !s.online.room.has_value(); });
    std::printf("%s\n", run.failures() == 0 ? "all checks passed" : "some checks failed");
    return run.failures() == 0 ? 0 : 1;
}
