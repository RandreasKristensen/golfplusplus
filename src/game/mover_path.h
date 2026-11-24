#pragma once

// Where a mover (a golfer on foot or a cart) was over a span of the shared
// clock, as the server plays it: its place after each fixed movement step,
// held until the next. A mover that stands still is one place held over the
// span. The box a span sweeps lets a flight rule out most movers cheaply
// (ball_contact.h). Pure, shared with the server.

#include "game/game_tuning.h"
#include "game/shot_snapshot.h"
#include "physics/solid.h"

#include <cstdint>
#include <optional>
#include <vector>

#include <glm/vec3.hpp>

// A mover's place from `time_ms` on: a golfer's feet, or a cart's pose
// (position and yaw as in cart_pose).
struct mover_sample {
    std::int64_t time_ms = 0;
    glm::vec3 position{0.0f};
    float yaw = 0.0f;
};

// `mover` says what and who it is (its kind: person or cart, its account, the
// shooter flag); its place is the samples'. They run in ascending time from
// the first, and the last is held until `until_ms` (exclusive).
struct mover_path {
    snapshot_entry mover;
    std::vector<mover_sample> samples;
    std::int64_t until_ms = 0;
};

// `mover` standing where its entry says from `from_ms` to `until_ms`.
mover_path standing_path(const snapshot_entry& mover, std::int64_t from_ms, std::int64_t until_ms);

// The mover as it stands at `time_us` microseconds of the shared clock (the
// latest sample at or before it), or none outside its span.
std::optional<snapshot_entry> mover_at(const mover_path& path, std::int64_t time_us);

// An axis-aligned box.
struct world_box {
    glm::vec3 low{0.0f};
    glm::vec3 high{0.0f};
};

bool boxes_overlap(const world_box& a, const world_box& b);

// The box of `thing`: for a cylinder its radius round its base and up its height,
// for a box solid its rotated rectangle's bounds.
world_box solid_bounds(const solid& thing);

// The box of everywhere the mover is in `[from_us, to_us]`, as its solid
// (a golfer's cylinder, a cart's box), or none when the path does not
// cover any of that time.
std::optional<world_box> swept_box(const mover_path& path, std::int64_t from_us, std::int64_t to_us, const game_tuning& tuning);
