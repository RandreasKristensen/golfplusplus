// Plays the golden shots (tests/fixtures/golden_shots/shots.json) the way the
// server does (server_content.h), drives the golden cart through fixed
// manoeuvres, walks and drives from every tee of the fixture course in the
// shared fixed steps (game/movement_step.h) and drops a cart into each
// hole's first deep water, and prints every field of
// each result, floats as their exact bits (0x then 8 hex digits), one row per
// line: a shot's summary, then each of its trajectory points and events. Each
// putt is also flown as a stored flight (game/ball_flight.h) met by a golfer
// and a driving cart (game/ball_contact.h), its contacts, hits, checkpoints
// and boxes printed, and rebuilt from the shot's record and contacts.
// check_determinism.ps1 builds this natively and to WASM and compares the
// two row by row. Exits 1 when a shot's holed, lost or penalty strokes are
// not what shots.json expects.
//
// Usage: golden_shots <repo root>

#include "game/content_files.h"
#include "game/ball_contact.h"
#include "game/course_loader.h"
#include "game/json_util.h"
#include "game/movement_step.h"
#include "game/play_area.h"
#include "game/shot_simulation.h"
#include "game/step_timeline.h"
#include "physics/cart_dynamics.h"
#include "physics/cart_rollover.h"
#include "physics/vector_math.h"
#include "server_content.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <optional>
#include <string>

#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

namespace {
// Every JSON file under `root`/`folder`, keyed by its path relative to `root`.
void add_folder(std::map<std::string, std::string>& texts, const std::string& root, const std::string& folder) {
    for (const std::filesystem::path& path : json_files_under(std::filesystem::path(root) / folder)) {
        if (const std::optional<std::string> text = read_text_file(path)) {
            texts[path.lexically_relative(root).generic_string()] = *text;
        }
    }
}
}

namespace {
// `value`'s exact bits, printed the same whatever the locale.
void print_bits(const float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof bits);
    std::printf(" 0x%08lx", static_cast<unsigned long>(bits));
}

void print_bits(const glm::vec3& value) {
    print_bits(value.x);
    print_bits(value.y);
    print_bits(value.z);
}

// Every field of `result`, as rows "<what> <i> ...".
void print_shot(const char* what, const std::size_t i, const shot_result& result) {
    std::printf("%s %zu summary rest", what, i);
    print_bits(result.rest_position);
    std::printf(" holed %d duration", result.holed ? 1 : 0);
    print_bits(result.duration);
    std::printf(" strike");
    print_bits(result.strike_centredness);
    std::printf(" lost %d penalty %d points %zu events %zu\n", result.lost ? 1 : 0, result.penalty_strokes,
                result.trajectory.size(), result.events.size());
    for (std::size_t j = 0; j < result.trajectory.size(); ++j) {
        std::printf("%s %zu point %zu", what, i, j);
        print_bits(result.trajectory[j]);
        std::printf("\n");
    }
    for (std::size_t j = 0; j < result.events.size(); ++j) {
        const shot_event& event = result.events[j];
        std::printf("%s %zu event %zu", what, i, j);
        print_bits(event.time);
        std::printf(" kind %d material %d\n", static_cast<int>(event.kind), static_cast<int>(event.material));
    }
}

// A golden shot's snapshot: each entry `ahead_meters` along the aim from
// the ball, on the ground (a ball resting there).
std::vector<snapshot_entry> golden_snapshot(const json& shot, const play_area& area, const glm::vec3& ball, const float aim,
                                            const float ball_radius) {
    std::vector<snapshot_entry> entries;
    if (const json* listed = json_array(shot, "snapshot")) {
        for (const json& entry : *listed) {
            const std::string kind = json_string(entry, "kind").value_or("person");
            const glm::vec3 at = ball + yaw_direction(aim) * json_float(entry, "ahead_meters").value_or(0.0f);
            const snapshot_kind of = kind == "cart" ? snapshot_kind::cart : kind == "ball" ? snapshot_kind::ball : snapshot_kind::person;
            const glm::vec3 placed = of == snapshot_kind::ball ? resting_ball_position(area, at, ball_radius)
                                                               : anchor_on_terrain(area, at);
            entries.push_back(snapshot_entry{of, placed, aim, json_bool(entry, "shooter").value_or(false)});
        }
    }
    return entries;
}

// A cart manoeuvre: one surface, sloped `grade` ahead and `side_rise` to the
// left (rise over run), the pedal held and the steering swung from `steer`
// to its opposite every `swing_seconds`.
struct golden_drive {
    const cart_surface_tuning* surface;
    float grade;
    float side_rise;
    float immersion;
    cart_controls controls;
    float swing_seconds;
    float seconds;
};

std::vector<golden_drive> golden_drives(const cart_surfaces_tuning& surfaces) {
    return {
        {&surfaces.path, 0.0f, 0.0f, 0.0f, {1.0f, 0.0f}, 0.0f, 8.0f},
        {&surfaces.fairway, 0.12f, 0.0f, 0.0f, {1.0f, 0.35f}, 1.5f, 10.0f},
        {&surfaces.rough, -0.18f, 0.05f, 0.0f, {0.6f, -0.6f}, 0.0f, 6.0f},
        {&surfaces.path, 0.0f, 0.0f, 0.0f, {-1.0f, 1.0f}, 2.0f, 6.0f},
        {&surfaces.fairway, 0.0f, 0.3f, 0.0f, {1.0f, 1.0f}, 0.0f, 8.0f},
        {&surfaces.green, 0.0f, 0.0f, 0.5f, {1.0f, 0.0f}, 0.0f, 1.0f},
    };
}

// The putts' flights with movers (see print_contact_flight).
struct contact_counts {
    int people = 0;
    int carts = 0;
};

bool same_shot(const shot_result& a, const shot_result& b) {
    if (a.trajectory.size() != b.trajectory.size() || a.events.size() != b.events.size() ||
        a.person_hits.size() != b.person_hits.size() || a.duration != b.duration || a.holed != b.holed) {
        return false;
    }
    for (std::size_t i = 0; i < a.trajectory.size(); ++i) {
        if (std::memcmp(&a.trajectory[i], &b.trajectory[i], sizeof a.trajectory[i]) != 0) {
            return false;
        }
    }
    for (std::size_t i = 0; i < a.events.size(); ++i) {
        if (a.events[i].time != b.events[i].time || a.events[i].kind != b.events[i].kind) {
            return false;
        }
    }
    return std::memcmp(&a.rest_position, &b.rest_position, sizeof a.rest_position) == 0;
}

// Plays every contact `path` makes with `flight`, one after another, as the
// server does where movement is played.
ball_flight play_path(ball_flight flight, const mover_path& path, const shot_course& flown, const game_tuning& tuning,
                      std::vector<flight_contact>& contacts) {
    for (int i = 0; i < 8; ++i) {
        const std::optional<flight_contact> contact = first_contact(flight, path, flown, tuning);
        if (!contact) {
            break;
        }
        contacts.push_back(*contact);
        flight = replay_after_contact(flight, *contact, flown, tuning);
    }
    return flight;
}

// A putt's flight met by a golfer standing 1.2 m ahead, then by a cart driving
// slowly across the line where the ball is a little later: played in one
// contact at a time, then rebuilt from the shot's record and its contacts, as
// a client does. Rows for the flight (its contacts, hits, checkpoints and
// boxes) and the rebuilt shot. False when the rebuilt shot differs.
bool print_contact_flight(const std::size_t i, const shot_record& record, const shot_course& course,
                          const server_content& content, const play_area& area, contact_counts& counts) {
    const game_tuning& tuning = content.tuning;
    const float unit = tuning.scale.meters_per_world_unit;
    shot_record bare = record;
    bare.snapshot = split_snapshot(record.snapshot).still;
    const shot_course flown = struck_course(bare, course, tuning);
    ball_flight flight = fly_struck_shot(bare, golfer_profile{}, course, tuning, content.clubs);
    std::vector<flight_contact> contacts;
    const std::int64_t from = record.setup.shot_at - 100;
    const glm::vec3 heading = yaw_direction(record.setup.aim_angle);

    const snapshot_entry golfer{snapshot_kind::person, anchor_on_terrain(area, record.setup.ball_start + heading * (1.2f / unit)), 0.0f,
                                false, 1};
    flight = play_path(flight, standing_path(golfer, from, from + 30'000), flown, tuning, contacts);
    if (flight.steps > 1) {
        const int later = std::min(contacts.empty() ? 0 : contacts.back().step + 20, flight.steps - 2);
        const flight_cursor there = cursor_at(flight, later, flown, tuning);
        const glm::vec3 going = safe_normalize(horizontal(there.ball.velocity), heading);
        const glm::vec3 left = yaw_left(going);
        mover_path driving;
        driving.mover = snapshot_entry{snapshot_kind::cart, glm::vec3(0.0f), yaw_towards(glm::vec3(0.0f), going), false, 2};
        const glm::vec3 base = there.ball.position - glm::vec3(0.0f, there.ball.radius, 0.0f) + going * (0.4f / unit) + left * (0.5f / unit);
        for (int step = 0; step <= 600; ++step) {
            const float seconds = static_cast<float>(step) * move_step_seconds;
            driving.samples.push_back(mover_sample{step_time_ms(from, static_cast<std::uint64_t>(step)),
                                                   base - left * (0.5f / unit * seconds), driving.mover.yaw});
        }
        driving.until_ms = driving.samples.back().time_ms + 9;
        flight = play_path(flight, driving, flown, tuning, contacts);
    }

    print_shot("flight", i, flight.result);
    std::printf("flight %zu steps %d open %d contacts %zu checkpoints %zu boxes %zu\n", i, flight.steps, flight.open_from_step,
                flight.contacts.size(), flight.checkpoints.size(), flight.boxes.size());
    for (std::size_t j = 0; j < contacts.size(); ++j) {
        const flight_contact& contact = contacts[j];
        counts.people += contact.mover.kind == snapshot_kind::person ? 1 : 0;
        counts.carts += contact.mover.kind == snapshot_kind::cart ? 1 : 0;
        std::printf("flight %zu contact %zu step %d kind %d account %llu", i, j, contact.step, static_cast<int>(contact.mover.kind),
                    static_cast<unsigned long long>(contact.mover.account));
        print_bits(contact.time);
        print_bits(contact.speed_mps);
        print_bits(contact.height_m);
        print_bits(contact.mover.position);
        print_bits(contact.mover.yaw);
        std::printf("\n");
    }
    for (std::size_t j = 0; j < flight.result.person_hits.size(); ++j) {
        const person_hit& hit = flight.result.person_hits[j];
        std::printf("flight %zu hit %zu entry %zu", i, j, hit.entry);
        print_bits(hit.time);
        print_bits(hit.speed_mps);
        print_bits(hit.contact_height_m);
        std::printf("\n");
    }
    for (std::size_t j = 0; j < flight.checkpoints.size(); ++j) {
        const flight_cursor& cursor = flight.checkpoints[j].cursor;
        std::printf("flight %zu checkpoint %zu steps %d inside %d", i, j, cursor.steps, cursor.in_contact ? 1 : 0);
        print_bits(cursor.ball.position);
        print_bits(cursor.ball.velocity);
        print_bits(cursor.ball.spin);
        std::printf("\n");
    }
    for (std::size_t j = 0; j < flight.boxes.size(); ++j) {
        std::printf("flight %zu box %zu", i, j);
        print_bits(flight.boxes[j].low);
        print_bits(flight.boxes[j].high);
        std::printf("\n");
    }
    const ball_flight rebuilt = replay_contacts(bare, golfer_profile{}, course, tuning, content.clubs, contacts);
    print_shot("rebuilt", i, rebuilt.result);
    return same_shot(rebuilt.result, flight.result);
}

std::vector<int> golden_flags_out(const json& shot) {
    std::vector<int> out;
    if (const json* listed = json_array(shot, "flags_out")) {
        for (const json& hole : *listed) {
            out.push_back(hole.get<int>());
        }
    }
    return out;
}
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: golden_shots <repo root>\n");
        return 2;
    }
    const std::string repo = argv[1];
    const std::string golden = repo + "/tests/fixtures/golden_shots";
    const std::string fixtures = repo + "/tests/fixtures";

    // The golden shots' frozen tuning, items (the clubs among them) and
    // rewards, the shipped font, and the fixture courses and hub, as the golden test
    // uses them.
    std::map<std::string, std::string> texts;
    add_folder(texts, golden, "play");
    add_folder(texts, golden, "world/props");
    add_folder(texts, repo + "/assets", "interface/fonts");
    add_folder(texts, fixtures, courses_path);
    add_folder(texts, fixtures, "world/hub");
    content_files files;
    for (const auto& [path, text] : texts) {
        files.emplace(path, text);
    }

    const server_content_load_result loaded = parse_server_content(files);
    if (!loaded.content) {
        std::fprintf(stderr, "content: %s\n", loaded.error.c_str());
        return 1;
    }
    const server_content& content = *loaded.content;
    const std::optional<server_course> course = build_server_course(files, content, "fixture_hub");
    const std::optional<std::string> shots_text = read_text_file(golden + "/shots.json");
    const std::optional<json> shots_root = shots_text ? parse_json(*shots_text) : std::nullopt;
    const json* shots = shots_root ? json_array(*shots_root, "shots") : nullptr;
    if (!course || shots == nullptr) {
        std::fprintf(stderr, "cannot load the fixture hub course or shots.json\n");
        return 1;
    }

    const float radius = content.tuning.scale.ball_physics_radius_meters;
    int unexpected = 0;
    contact_counts counts;
    for (std::size_t i = 0; i < shots->size(); ++i) {
        const json& shot = (*shots)[i];
        const std::size_t hole = static_cast<std::size_t>(json_int(shot, "hole").value_or(0));
        const shot_hole target{course->area.cups[hole].centre, hole};
        shot_setup input;
        input.ball_start = resting_ball_position(course->area, course->holes[hole].tee_position, radius);
        input.aim_angle = yaw_towards(input.ball_start, target.pin);
        if (const std::optional<float> putt = json_float(shot, "putt_from_pin_meters")) {
            input.ball_start = resting_ball_position(course->area, target.pin - yaw_direction(input.aim_angle) * *putt, radius);
            input.aim_angle = yaw_towards(input.ball_start, target.pin);
        }
        input.aim_angle += glm::radians(json_float(shot, "aim_offset_degrees").value_or(0.0f));
        // Online the aim arrives as the client's float; here it comes from
        // atan2, which may round differently in the WASM, so both builds
        // take it on the same coarse step.
        constexpr float aim_steps_per_radian = 4096.0f;
        input.aim_angle = std::round(input.aim_angle * aim_steps_per_radian) / aim_steps_per_radian;
        const club_definition* club = find_club(content.clubs, json_string(shot, "club").value_or(""));
        if (club == nullptr) {
            std::fprintf(stderr, "golden shot %zu: unknown club\n", i);
            return 1;
        }
        const float aim = input.aim_angle;
        input = stock_setup(*club, lie_at(course->area, input.ball_start), input.ball_start, aim, handedness::right, 0.0f,
                            content.tuning.setup);
        input.aim_angle = aim;
        input.backswing = json_float(shot, "backswing").value_or(0.0f);
        input.shot_at = shot.value("shot_at", std::int64_t{0});

        const shot_record record{input, shot.value("seed", std::uint64_t{0}),
                                 golden_snapshot(shot, course->area, input.ball_start, aim, radius)};
        const shot_result result =
            simulate_struck_shot(record, golfer_profile{}, server_shot_course(*course, hole, input.shot_at, golden_flags_out(shot), content),
                                 content.tuning, content.clubs);
        print_shot("shot", i, result);
        if (json_string(shot, "club").value_or("") == "putter") {
            const shot_course flown_on = server_shot_course(*course, hole, input.shot_at, golden_flags_out(shot), content);
            if (!print_contact_flight(i, record, flown_on, content, course->area, counts)) {
                std::fprintf(stderr, "golden shot %zu: the flight rebuilt from its contacts is not the one played\n", i);
                ++unexpected;
            }
        }
        const std::optional<bool> holed = json_bool(shot, "holed");
        const std::optional<bool> lost = json_bool(shot, "lost");
        const std::optional<int> penalty = json_int(shot, "penalty_strokes");
        if (!holed || !lost || !penalty || *holed != result.holed || *lost != result.lost ||
            *penalty != result.penalty_strokes) {
            std::fprintf(stderr, "golden shot %zu: holed %d, lost %d, penalty %d, not as shots.json expects\n", i,
                         result.holed ? 1 : 0, result.lost ? 1 : 0, result.penalty_strokes);
            ++unexpected;
        }
    }

    if (counts.people == 0 || counts.carts == 0) {
        std::fprintf(stderr, "the putts' flights met %d golfers and %d carts: both kinds are needed\n", counts.people, counts.carts);
        ++unexpected;
    }

    const cart_spec& spec = *content.items.find(content.default_loadout.cart)->cart;
    const std::vector<golden_drive> drives = golden_drives(content.tuning.surfaces.cart);
    for (std::size_t i = 0; i < drives.size(); ++i) {
        const golden_drive& drive = drives[i];
        cart_ground ground;
        ground.normal = glm::normalize(glm::vec3(-drive.side_rise, 1.0f, -drive.grade));
        ground.rolling_resistance = drive.surface->rolling_resistance;
        ground.grip = drive.surface->grip;
        ground.immersion = drive.immersion;
        const int steps = static_cast<int>(std::lround(drive.seconds / cart_step_seconds));
        const int swing = static_cast<int>(std::lround(drive.swing_seconds / cart_step_seconds));
        cart_motion motion;
        motion.yaw = 0.3f;
        glm::vec2 position(0.0f);
        cart_controls controls = drive.controls;
        for (int step = 0; step < steps; ++step) {
            if (swing > 0 && step > 0 && step % swing == 0) {
                controls.steer = -controls.steer;
            }
            motion = step_cart(motion, controls, ground, spec, cart_step_seconds);
            position += motion.velocity * cart_step_seconds;
        }
        cart_fall fall;
        if (cart_tipped(motion.condition)) {
            fall = start_cart_fall(spec);
            while (!fall.resting) {
                fall = step_cart_fall(fall, spec, content.tuning.cart.fall_restitution, cart_fall_step_seconds);
            }
        }
        std::printf("cart %zu at", i);
        print_bits(position.x);
        print_bits(position.y);
        std::printf(" yaw");
        print_bits(motion.yaw);
        std::printf(" condition %d speed", static_cast<int>(motion.condition));
        print_bits(cart_forward_speed(motion));
        std::printf(" fall");
        print_bits(fall.angle);
        std::printf("\n");
    }

    // On every hole of the fixture course, from its tee: a walk (turning,
    // stepping sideways, running and hopping) and a drive (weaving), each
    // through the shared fixed steps over the course's ground and solids.
    const std::vector<bool> flags_in(course->area.cups.size(), true);
    const std::vector<solid> walker_solids = walker_obstacles({}, course->area.cups, flags_in, content.tuning);
    const std::vector<solid> cart_solids = cart_obstacles_of({}, {}, course->area.cups, flags_in, content.tuning);
    const walk_world on_foot{course->area, walker_solids, content.tuning, content.tuning.player.walk_speed, nullptr};
    const cart_world at_wheel{course->area, content.tuning, spec, 0.4f, 1.0f, cart_solids, nullptr};
    constexpr int steps = 720;
    for (std::size_t hole = 0; hole < course->holes.size(); ++hole) {
        const glm::vec3 tee = anchor_on_terrain(course->area, course->holes[hole].tee_position);
        walker me{tee, vertical_body{tee.y, 0.0f, true}};
        float yaw = yaw_towards(tee, course->area.cups[hole].centre);
        for (int step = 0; step < steps; ++step) {
            move_input input;
            input.forward = static_cast<std::int8_t>(step % 240 < 200 ? 1 : -1);
            input.side = static_cast<std::int8_t>((step / 90) % 3 - 1);
            input.run = (step / 150) % 2 == 1;
            input.jump = step % 180 == 60;
            input.paced = step >= 240 && step < 480;
            input.crouched = step >= 360 && step < 600;
            yaw += 0.4f * move_step_seconds;
            input.yaw = quantise_yaw(yaw);
            me = step_walker(me, input, on_foot).after;
        }
        std::printf("walk %zu at", hole);
        print_bits(me.position);
        std::printf(" grounded %d speed", me.vertical.grounded ? 1 : 0);
        print_bits(me.speed);
        std::printf(" vertical");
        print_bits(me.vertical.speed);
        std::printf("\n");

        cart_mover cart;
        cart.pose = cart_pose{tee, yaw_towards(tee, course->area.cups[hole].centre)};
        cart.motion.yaw = cart.pose.yaw;
        cart.vertical = vertical_body{tee.y, 0.0f, true};
        for (int step = 0; step < steps; ++step) {
            const float steer = step % 300 < 150 ? 0.6f : -0.6f;
            cart = step_cart_mover(cart, cart_controls{1.0f, steer_of(quantise_steer(steer))}, at_wheel).after;
        }
        std::printf("drive %zu at", hole);
        print_bits(cart.pose.position);
        std::printf(" condition %d yaw", static_cast<int>(cart.motion.condition));
        print_bits(cart.pose.yaw);
        std::printf(" speed");
        print_bits(cart_forward_speed(cart.motion));
        std::printf("\n");

        // A cart a metre over the first water deeper than it wades on the
        // way from the tee to the pin, falling in until it sinks.
        const glm::vec3 to_pin = course->area.cups[hole].centre - tee;
        const float unit = content.tuning.scale.meters_per_world_unit;
        for (int metre = 0; metre < static_cast<int>(glm::length(horizontal(to_pin)) * unit); ++metre) {
            const glm::vec3 at = tee + safe_normalize(horizontal(to_pin), glm::vec3(0.0f, 0.0f, 1.0f)) * (static_cast<float>(metre) / unit);
            const terrain_sample pond = sample_area(course->area, at);
            if (pond.material != terrain_material::water || (pond.water_level - pond.point.y) * unit <= spec.wade_depth_m + 0.05f) {
                continue;
            }
            cart_mover sinking;
            sinking.pose = cart_pose{glm::vec3(at.x, pond.water_level + 1.0f / unit, at.z), cart.pose.yaw};
            sinking.motion.yaw = sinking.pose.yaw;
            sinking.vertical = vertical_body{sinking.pose.position.y, 0.0f, false};
            int fell = 0;
            for (; fell < steps && sinking.motion.condition == cart_condition::upright; ++fell) {
                sinking = step_cart_mover(sinking, cart_controls{}, at_wheel).after;
            }
            std::printf("sink %zu at", hole);
            print_bits(sinking.pose.position);
            std::printf(" condition %d steps %d\n", static_cast<int>(sinking.motion.condition), fell);
            break;
        }
    }
    return unexpected == 0 ? 0 : 1;
}
