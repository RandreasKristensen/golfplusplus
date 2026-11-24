#pragma once

// A knocked-out golfer's body, lying where they fell for an hour of real time
// even after they log in again: a thing in the world, not the player. Online
// it is a `body` row of the room (the server inserts it, and deletes it when
// it expires); offline it is kept in the save (save_data::bodies) with the
// place it lies in. It stops balls, walkers and carts as a low box in the one
// list of dynamic solids, and is never a target of a ball's hit (only people
// standing are, game/ball_hit_rules.h). Pure, shared with the server.

#include "game/game_tuning.h"
#include "game/shot_setup.h"
#include "game/shot_snapshot.h"
#include "physics/solid.h"

#include <cstdint>
#include <string>
#include <vector>

#include <glm/vec3.hpp>

struct fallen_body {
    std::uint64_t account = 0;  // whose it is online; 0 for offline's me
    std::string place_id;       // offline: the place it lies in; online its room's rows hold it
    glm::vec3 position{0.0f};   // the middle of the lying body, on the ground
    // They lie on their back, head at the +yaw end of the box body_solid
    // makes (physics/vector_math.h yaw_direction), feet at the other; drawn
    // so by renderer/lying_body.h.
    float yaw = 0.0f;
    std::int64_t fell_at_ms = 0;     // UTC milliseconds
    std::int64_t expires_at_ms = 0;
    // What drawing them needs: whether their bag still lies on their back,
    // their favourite colour and the hand they play with.
    bool bag_on_back = false;
    std::uint8_t colour = 0;
    handedness hand = handedness::right;
};

// `body` as one that fell at `fell_at_ms`, lying knockout.body_minutes.
fallen_body body_fallen_at(fallen_body body, std::int64_t fell_at_ms, const knockout_tuning& tuning);

// Whether it has lain its time by `now_ms`: from the very millisecond it ends.
bool body_expired(const fallen_body& body, std::int64_t now_ms);

// `bodies` without those expired at `now_ms`.
std::vector<fallen_body> bodies_lying(std::vector<fallen_body> bodies, std::int64_t now_ms);

// The low box it is: its length a standing person's height
// (rangefinder.person_height) along its yaw, centred on its position.
solid body_solid(const fallen_body& body, const game_tuning& tuning);
std::vector<solid> body_solids(const std::vector<fallen_body>& bodies, const game_tuning& tuning);

// The entry a shot's snapshot holds for it (snapshot_kind::body).
snapshot_entry body_snapshot_entry(const fallen_body& body);
