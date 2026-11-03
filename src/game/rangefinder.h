#pragma once

// The rangefinder, held up on foot while walking: a monocular whose round
// eyepiece magnifies the middle of the view (renderer/eyepiece_pass.h) and
// whose laser reads the distance to whatever is at its reticle (the centre
// of the view): the ground, a tree, a pole, a sign, a net, a cart or a
// person (game/sight_line.h). It locks onto any hole's flag near the
// reticle that it can see. Like a real golf laser it shows the level
// distance, the height and the slope's plays-like distance, reads nothing
// nearer than its minimum range, and takes the nearest return in its beam
// (first target priority): fog and rain shorten its reach, and their
// droplets send back false short readings. Presentation and aiming only:
// it never changes a shot. An item (owns_rangefinder in
// game/progress_rules.h).

#include "game/game_input.h"
#include "game/game_tuning.h"
#include "game/play_area.h"
#include "game/sight_line.h"
#include "game/weather.h"

#include <cstdint>
#include <optional>
#include <vector>

#include <glm/vec3.hpp>

struct game_state;

struct rangefinder_reading {
    // Level metres from me to what it points at; none at the sky.
    std::optional<float> distance_meters;
    // Meters the same point stands above the ground under me (below, negative).
    std::optional<float> height_meters;
    // Straight metres from the eye to it, which the laser travels.
    std::optional<float> sight_meters;
    std::optional<glm::vec3> flag;  // the flagstick's foot, while locked onto it
};

// What the rangefinder can see: everything standing, and every flagstick
// (`flag_height` world units tall) by its foot.
struct rangefinder_view {
    sight_scene scene;
    std::vector<glm::vec3> flags;
    float flag_height = 0.0f;
};

// The first-person eye and unit look direction, walking or in a cart: the
// walking camera (core/render_frame.h) looks from one along the other.
glm::vec3 first_person_eye(const game_state& state);
glm::vec3 first_person_look(const game_state& state);

// Where the ray from `eye` along unit `direction` first meets the area's
// ground within the tuning's range: marched in steps, then bisected.
// Nullopt when it reaches the sky.
std::optional<glm::vec3> ground_hit(const play_area& area,
                                    const glm::vec3& eye,
                                    const glm::vec3& direction,
                                    const rangefinder_tuning& tuning,
                                    const world_scale_tuning& scale);

// The course as the rangefinder sees it now: its trees and fixtures, the
// carts and other people (rangefinder.person_height, cart_height tall), and
// every hole's flag (this hole's where its pin is now).
rangefinder_view rangefinder_view_of(const game_state& state);

// What it reads in clear air from `eye` along `look` for a player standing
// at `player`: nothing when the first thing the look meets is nearer than
// min_range_meters; else the nearest flag within the lock angle of the
// look, from min_range_meters to flag_range_meters away, whose cloth it
// can see from `eye`; else that first thing, standing or ground, within
// max_range_meters.
rangefinder_reading read_rangefinder(const play_area& area,
                                     const rangefinder_view& view,
                                     const glm::vec3& player,
                                     const glm::vec3& eye,
                                     const glm::vec3& look,
                                     const rangefinder_tuning& tuning,
                                     const world_scale_tuning& scale);

// How far it reaches through `weather` to a target it reaches `range`
// metres away in clear air: fog and rain dim the beam, adding up, so that
// full fog (rain) alone cuts max_range_meters to fog_reach_meters
// (rain_reach_meters).
float rangefinder_reach(const weather_state& weather, const rangefinder_tuning& tuning, float range);

// `clean`, read along unit `look` from an eye `eye_height_meters` over my
// ground, as `weather` lets it through on reading `ping` (a new one every
// ping_seconds). Droplets return the beam: on rain_false_share of pings at
// full rain plus fog_false_share at full fog, fewer the nearer the target
// is within its reach, it reads them instead, from min_range_meters to
// false_return_meters off and short of the target, never on a flag. A
// target beyond its reach, or the sky, reads droplets on that share of
// pings and nothing on the rest. True readings are as clean.
rangefinder_reading weathered_reading(const rangefinder_reading& clean, const glm::vec3& look, float eye_height_meters,
                                      const weather_state& weather, const rangefinder_tuning& tuning, std::uint32_t ping);

// Held up this frame: owned, held, on foot and walking.
bool rangefinder_raised(const game_state& state, const game_input& input);

// Raises or lowers it, zooming over raise_seconds, and takes its reading.
void update_rangefinder(game_state& state, const game_input& input, float dt);

// Its flag lock's ring shows: locked onto the flag, flashing at first
// (the jolt) and then steady.
bool rangefinder_lock_lit(const game_state& state);

// The eyepiece's zoom now: 1 at the eye, the tuning's magnification held
// up. The look turns this much slower.
float rangefinder_magnification(const game_state& state);

// The distance `reading` plays like on the slope: its level distance plus
// its height, as golf lasers that show both work it out (uphill plays
// longer, downhill shorter); none without both.
std::optional<float> plays_like_meters(const rangefinder_reading& reading);

// Whole meters, as the rangefinder and the hole signs show them.
int rounded_rangefinder_meters(float distance_meters);
