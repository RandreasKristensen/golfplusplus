#include "flight_codec.h"

#include <cstring>
#include <string>
#include <type_traits>

namespace {
class writer {
public:
    template <typename T>
    void number(const T value) {
        static_assert(std::is_arithmetic<T>::value);
        std::uint8_t raw[sizeof(T)];
        std::memcpy(raw, &value, sizeof(T));
        bytes_.insert(bytes_.end(), raw, raw + sizeof(T));
    }
    void flag(const bool value) { number<std::uint8_t>(value ? 1 : 0); }
    void count(const std::size_t value) { number<std::uint32_t>(static_cast<std::uint32_t>(value)); }
    void index(const std::size_t value) { number<std::uint64_t>(static_cast<std::uint64_t>(value)); }
    void vec(const glm::vec3& value) {
        number(value.x);
        number(value.y);
        number(value.z);
    }
    void text(const std::string& value) {
        count(value.size());
        bytes_.insert(bytes_.end(), value.begin(), value.end());
    }
    template <typename T>
    void maybe_index(const std::optional<T>& value) {
        flag(value.has_value());
        index(value ? static_cast<std::size_t>(*value) : 0);
    }
    std::vector<std::uint8_t> take() { return std::move(bytes_); }

private:
    std::vector<std::uint8_t> bytes_;
};

class reader {
public:
    explicit reader(const std::vector<std::uint8_t>& bytes) : bytes_(bytes) {}

    template <typename T>
    T number() {
        T value{};
        if (!take(sizeof(T))) {
            return value;
        }
        std::memcpy(&value, bytes_.data() + at_ - sizeof(T), sizeof(T));
        return value;
    }
    bool flag() { return number<std::uint8_t>() != 0; }
    // A count of things at least `each` bytes long, so a corrupt count cannot
    // ask for more than the bytes left.
    std::size_t count(const std::size_t each) {
        const std::size_t value = number<std::uint32_t>();
        if (ok_ && value * each > bytes_.size() - at_) {
            ok_ = false;
        }
        return ok_ ? value : 0;
    }
    std::size_t index() { return static_cast<std::size_t>(number<std::uint64_t>()); }
    glm::vec3 vec() {
        const float x = number<float>();
        const float y = number<float>();
        const float z = number<float>();
        return glm::vec3(x, y, z);
    }
    std::string text() {
        const std::size_t size = count(1);
        std::string value;
        if (ok_) {
            value.assign(reinterpret_cast<const char*>(bytes_.data()) + at_, size);
            at_ += size;
        }
        return value;
    }
    template <typename T>
    std::optional<T> maybe_index() {
        const bool present = flag();
        const std::size_t value = index();
        return present ? std::optional<T>(static_cast<T>(value)) : std::nullopt;
    }
    bool done() const { return ok_ && at_ == bytes_.size(); }

private:
    bool take(const std::size_t size) {
        if (!ok_ || size > bytes_.size() - at_) {
            ok_ = false;
            return false;
        }
        at_ += size;
        return true;
    }
    const std::vector<std::uint8_t>& bytes_;
    std::size_t at_ = 0;
    bool ok_ = true;
};

terrain_material material_of(const std::uint8_t value) {
    return value < terrain_material_count ? static_cast<terrain_material>(value) : terrain_material::rough;
}

shot_event_kind event_kind(const std::uint8_t value) {
    return value <= static_cast<std::uint8_t>(shot_event_kind::ball_hit) ? static_cast<shot_event_kind>(value) : shot_event_kind::land;
}

handedness hand_of(const std::uint8_t value) {
    return value == static_cast<std::uint8_t>(handedness::left) ? handedness::left : handedness::right;
}

void put_ball(writer& out, const ball_state& ball) {
    out.vec(ball.position);
    out.number(ball.radius);
    out.vec(ball.velocity);
    out.vec(ball.spin);
}

ball_state get_ball(reader& in) {
    ball_state ball;
    ball.position = in.vec();
    ball.radius = in.number<float>();
    ball.velocity = in.vec();
    ball.spin = in.vec();
    return ball;
}

void put_ground(writer& out, const terrain_sample& ground) {
    out.vec(ground.point);
    out.vec(ground.normal);
    out.number(ground.distance_from_center);
    out.number<std::int32_t>(ground.triangle_index);
    out.number<std::int32_t>(ground.triangles_tested);
    out.number(static_cast<std::uint8_t>(ground.material));
    out.flag(ground.inside_surface);
    out.number(ground.water_level);
    out.number<std::int32_t>(ground.top_solid);
    out.number(ground.green_stimp_feet);
}

terrain_sample get_ground(reader& in) {
    terrain_sample ground;
    ground.point = in.vec();
    ground.normal = in.vec();
    ground.distance_from_center = in.number<float>();
    ground.triangle_index = in.number<std::int32_t>();
    ground.triangles_tested = in.number<std::int32_t>();
    ground.material = material_of(in.number<std::uint8_t>());
    ground.inside_surface = in.flag();
    ground.water_level = in.number<float>();
    ground.top_solid = in.number<std::int32_t>();
    ground.green_stimp_feet = in.number<float>();
    return ground;
}

void put_cursor(writer& out, const flight_cursor& cursor) {
    put_ball(out, cursor.ball);
    put_ground(out, cursor.ground);
    out.number<std::int32_t>(cursor.steps);
    out.flag(cursor.from_green);
    out.flag(cursor.hit_ball_on_green);
    out.flag(cursor.lost);
    out.flag(cursor.in_leaves);
    out.flag(cursor.finished);
    out.number<std::int32_t>(cursor.on_pond_floor);
    out.flag(cursor.touching.has_value());
    out.number<std::uint8_t>(cursor.touching ? static_cast<std::uint8_t>(*cursor.touching) : 0);
    out.maybe_index(cursor.touching_person);
    out.maybe_index(cursor.touching_cup);
    out.count(cursor.came_in.size());
    for (const cup_entry& entry : cursor.came_in) {
        out.index(entry.cup);
        out.vec(entry.position);
    }
    out.maybe_index(cursor.resting_cup);
    out.maybe_index(cursor.in_contact);
}

flight_cursor get_cursor(reader& in) {
    flight_cursor cursor;
    cursor.ball = get_ball(in);
    cursor.ground = get_ground(in);
    cursor.steps = in.number<std::int32_t>();
    cursor.from_green = in.flag();
    cursor.hit_ball_on_green = in.flag();
    cursor.lost = in.flag();
    cursor.in_leaves = in.flag();
    cursor.finished = in.flag();
    cursor.on_pond_floor = in.number<std::int32_t>();
    const bool touching = in.flag();
    const shot_event_kind kind = event_kind(in.number<std::uint8_t>());
    if (touching) {
        cursor.touching = kind;
    }
    cursor.touching_person = in.maybe_index<std::size_t>();
    cursor.touching_cup = in.maybe_index<std::size_t>();
    const std::size_t entries = in.count(8 + 12);
    for (std::size_t i = 0; i < entries; ++i) {
        const std::size_t cup = in.index();
        cursor.came_in.push_back(cup_entry{cup, in.vec()});
    }
    cursor.resting_cup = in.maybe_index<std::size_t>();
    cursor.in_contact = in.maybe_index<std::size_t>();
    return cursor;
}

// What a cursor takes at the least: its ball, ground, flags and counts.
constexpr std::size_t cursor_bytes = 40 + 49 + 4 + 5 + 4 + 2 + 9 * 3 + 4;

void put_entry(writer& out, const snapshot_entry& entry) {
    out.number(static_cast<std::uint8_t>(entry.kind));
    out.vec(entry.position);
    out.number(entry.yaw);
    out.flag(entry.shooter);
    out.number(entry.account);
}

snapshot_entry get_entry(reader& in) {
    snapshot_entry entry;
    const std::uint8_t kind = in.number<std::uint8_t>();
    entry.kind = kind <= static_cast<std::uint8_t>(snapshot_kind::body) ? static_cast<snapshot_kind>(kind) : snapshot_kind::person;
    entry.position = in.vec();
    entry.yaw = in.number<float>();
    entry.shooter = in.flag();
    entry.account = in.number<std::uint64_t>();
    return entry;
}

constexpr std::size_t entry_bytes = 1 + 12 + 4 + 1 + 8;

void put_contact(writer& out, const flight_contact& contact) {
    out.number<std::int32_t>(contact.step);
    put_entry(out, contact.mover);
    out.number(contact.time);
    out.number(contact.speed_mps);
    out.number(contact.height_m);
}

flight_contact get_contact(reader& in) {
    flight_contact contact;
    contact.step = in.number<std::int32_t>();
    contact.mover = get_entry(in);
    contact.time = in.number<float>();
    contact.speed_mps = in.number<float>();
    contact.height_m = in.number<float>();
    return contact;
}

constexpr std::size_t contact_bytes = 4 + entry_bytes + 12;

void put_result(writer& out, const shot_result& result) {
    out.vec(result.rest_position);
    out.flag(result.holed);
    out.number(result.duration);
    out.count(result.trajectory.size());
    for (const glm::vec3& point : result.trajectory) {
        out.vec(point);
    }
    out.count(result.events.size());
    for (const shot_event& event : result.events) {
        out.number(event.time);
        out.number(static_cast<std::uint8_t>(event.kind));
        out.number(static_cast<std::uint8_t>(event.material));
    }
    out.count(result.person_hits.size());
    for (const person_hit& hit : result.person_hits) {
        out.index(hit.entry);
        out.number(hit.time);
        out.number(hit.speed_mps);
        out.number(hit.contact_height_m);
    }
    out.number(result.strike_centredness);
    out.flag(result.lost);
    out.number<std::int32_t>(result.penalty_strokes);
}

shot_result get_result(reader& in) {
    shot_result result;
    result.rest_position = in.vec();
    result.holed = in.flag();
    result.duration = in.number<float>();
    const std::size_t points = in.count(12);
    result.trajectory.reserve(points);
    for (std::size_t i = 0; i < points; ++i) {
        result.trajectory.push_back(in.vec());
    }
    const std::size_t events = in.count(6);
    for (std::size_t i = 0; i < events; ++i) {
        shot_event event;
        event.time = in.number<float>();
        event.kind = event_kind(in.number<std::uint8_t>());
        event.material = material_of(in.number<std::uint8_t>());
        result.events.push_back(event);
    }
    const std::size_t hits = in.count(8 + 12);
    for (std::size_t i = 0; i < hits; ++i) {
        person_hit hit;
        hit.entry = in.index();
        hit.time = in.number<float>();
        hit.speed_mps = in.number<float>();
        hit.contact_height_m = in.number<float>();
        result.person_hits.push_back(hit);
    }
    result.strike_centredness = in.number<float>();
    result.lost = in.flag();
    result.penalty_strokes = in.number<std::int32_t>();
    return result;
}

void put_flight(writer& out, const ball_flight& flight) {
    out.number(flight.launch_ms);
    out.count(flight.checkpoints.size());
    for (const flight_checkpoint& checkpoint : flight.checkpoints) {
        put_cursor(out, checkpoint.cursor);
        out.number(checkpoint.event_count);
        out.number(checkpoint.hit_count);
    }
    out.count(flight.boxes.size());
    for (const flight_box& box : flight.boxes) {
        out.vec(box.low);
        out.vec(box.high);
    }
    out.count(flight.contacts.size());
    for (const flight_contact& contact : flight.contacts) {
        put_contact(out, contact);
    }
    out.number<std::int32_t>(flight.steps);
    out.number<std::int32_t>(flight.open_from_step);
    put_result(out, flight.result);
}

ball_flight get_flight(reader& in) {
    ball_flight flight;
    flight.launch_ms = in.number<std::int64_t>();
    const std::size_t checkpoints = in.count(cursor_bytes + 8);
    flight.checkpoints.reserve(checkpoints);
    for (std::size_t i = 0; i < checkpoints; ++i) {
        flight_checkpoint checkpoint;
        checkpoint.cursor = get_cursor(in);
        checkpoint.event_count = in.number<std::uint32_t>();
        checkpoint.hit_count = in.number<std::uint32_t>();
        flight.checkpoints.push_back(std::move(checkpoint));
    }
    const std::size_t boxes = in.count(24);
    flight.boxes.reserve(boxes);
    for (std::size_t i = 0; i < boxes; ++i) {
        const glm::vec3 low = in.vec();
        flight.boxes.push_back(flight_box{low, in.vec()});
    }
    const std::size_t contacts = in.count(contact_bytes);
    for (std::size_t i = 0; i < contacts; ++i) {
        flight.contacts.push_back(get_contact(in));
    }
    flight.steps = in.number<std::int32_t>();
    flight.open_from_step = in.number<std::int32_t>();
    flight.result = get_result(in);
    return flight;
}
}

std::vector<std::uint8_t> encode_flight(const ball_flight& flight) {
    writer out;
    put_flight(out, flight);
    return out.take();
}

std::optional<ball_flight> decode_flight(const std::vector<std::uint8_t>& bytes) {
    reader in(bytes);
    ball_flight flight = get_flight(in);
    if (!in.done()) {
        return std::nullopt;
    }
    return flight;
}

std::vector<std::uint8_t> encode_live_flight(const live_flight& live) {
    writer out;
    const shot_setup& setup = live.record.setup;
    out.vec(setup.ball_start);
    out.text(setup.club_id);
    out.number(setup.aim_angle);
    out.number(setup.face_open);
    out.number(setup.ball_position);
    out.number(setup.backswing);
    out.number(setup.strike_timing);
    out.number(static_cast<std::uint8_t>(setup.hand));
    out.number(setup.shot_at);
    out.number(live.record.seed);
    out.count(live.record.snapshot.size());
    for (const snapshot_entry& entry : live.record.snapshot) {
        put_entry(out, entry);
    }
    out.count(live.golfer.levels.size());
    for (const auto& [skill, level] : live.golfer.levels) {
        out.text(skill);
        out.number<std::int32_t>(level);
    }
    out.number(static_cast<std::uint8_t>(live.golfer.natural));
    out.number(static_cast<std::uint8_t>(live.golfer.hand));
    out.number(live.golfer.spread_scale);
    out.number(live.golfer.club_speed_scale);
    out.number<std::int32_t>(live.hole);
    out.number<std::int32_t>(live.stroke);
    out.count(live.flags_out.size());
    for (const int flag : live.flags_out) {
        out.number<std::int32_t>(flag);
    }
    put_flight(out, live.flight);
    out.count(live.pending.size());
    for (const flight_contact& contact : live.pending) {
        put_contact(out, contact);
    }
    out.count(live.rolled.size());
    for (const std::uint64_t account : live.rolled) {
        out.number(account);
    }
    return out.take();
}

std::optional<live_flight> decode_live_flight(const std::vector<std::uint8_t>& bytes) {
    reader in(bytes);
    live_flight live;
    shot_setup& setup = live.record.setup;
    setup.ball_start = in.vec();
    setup.club_id = in.text();
    setup.aim_angle = in.number<float>();
    setup.face_open = in.number<float>();
    setup.ball_position = in.number<float>();
    setup.backswing = in.number<float>();
    setup.strike_timing = in.number<float>();
    setup.hand = hand_of(in.number<std::uint8_t>());
    setup.shot_at = in.number<std::int64_t>();
    live.record.seed = in.number<std::uint64_t>();
    const std::size_t entries = in.count(entry_bytes);
    for (std::size_t i = 0; i < entries; ++i) {
        live.record.snapshot.push_back(get_entry(in));
    }
    const std::size_t levels = in.count(8);
    for (std::size_t i = 0; i < levels; ++i) {
        std::string skill = in.text();
        live.golfer.levels[std::move(skill)] = in.number<std::int32_t>();
    }
    live.golfer.natural = natural_shape_from_index(in.number<std::uint8_t>()).value_or(natural_shape::neutral);
    live.golfer.hand = hand_of(in.number<std::uint8_t>());
    live.golfer.spread_scale = in.number<float>();
    live.golfer.club_speed_scale = in.number<float>();
    live.hole = in.number<std::int32_t>();
    live.stroke = in.number<std::int32_t>();
    const std::size_t flags = in.count(4);
    for (std::size_t i = 0; i < flags; ++i) {
        live.flags_out.push_back(in.number<std::int32_t>());
    }
    live.flight = get_flight(in);
    const std::size_t pending = in.count(contact_bytes);
    for (std::size_t i = 0; i < pending; ++i) {
        live.pending.push_back(get_contact(in));
    }
    const std::size_t rolled = in.count(8);
    for (std::size_t i = 0; i < rolled; ++i) {
        live.rolled.push_back(in.number<std::uint64_t>());
    }
    if (!in.done()) {
        return std::nullopt;
    }
    return live;
}
