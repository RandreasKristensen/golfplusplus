#include "knockout_rules.h"

#include <utility>

bool knockout_lands(const std::optional<std::uint64_t> member_room, const std::uint64_t room_id, const bool has_position) {
    return member_room && *member_room == room_id && has_position;
}

fallen_body body_after_knockout(const knocked_golfer& golfer, const std::int64_t at_ms, const knockout_tuning& tuning) {
    fallen_body body;
    body.account = golfer.account;
    body.position = golfer.position;
    body.yaw = golfer.yaw;
    body.bag_on_back = golfer.bag_on_back;
    body.colour = golfer.colour;
    body.hand = golfer.hand;
    return body_fallen_at(std::move(body), at_ms, tuning);
}
