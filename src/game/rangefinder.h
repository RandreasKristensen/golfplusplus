#pragma once

// The rangefinder, held up on foot while walking: a monocular whose round
// eyepiece magnifies the middle of the view (renderer/eyepiece_pass.h) and
// whose laser reads the distance to whatever is at its reticle (the centre
// of the view): the ground, a tree, a pole, a sign, a net, a cart or a
// person (game/sight_line.h). It locks onto any hole's flag near the
// reticle that it can see. Rain and fog spoil it: its readings wander, or
// it reads nonsense. Presentation and aiming only: it never changes a shot.
// An item (owns_rangefinder in game/progress_rules.h).

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

// What it reads from `eye` along `look` for a player standing at `player`:
// the nearest flag within the lock angle of the look whose flag it can see
// from `eye`, else the first thing the look meets, standing or ground.
rangefinder_reading read_rangefinder(const play_area& area,
                                     const rangefinder_view& view,
                                     const glm::vec3& player,
                                     const glm::vec3& eye,
                                     const glm::vec3& look,
                                     const rangefinder_tuning& tuning,
                                     const world_scale_tuning& scale);

// `clean` as `weather` lets it through on reading `ping` (a new one every
// ping_seconds): true in the dry; off either way by up to light_rain_error
// at light rain's amount (less in less) and light_fog_error at light fog's,
// whichever is more; once the rain is rain (rain_kind) or the fog fog
// (fog_kind), nonsense: blank blank_share of the time, else any distance
// in range, never locked onto a flag.
rangefinder_reading weathered_reading(const rangefinder_reading& clean, const weather_state& weather,
                                      const weather_tuning& weather_rules, const rangefinder_tuning& tuning, std::uint32_t ping);

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

// Whole meters, as the rangefinder and the hole signs show them.
int rounded_rangefinder_meters(float distance_meters);
