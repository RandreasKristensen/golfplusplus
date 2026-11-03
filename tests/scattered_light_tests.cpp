#include "doctest.h"

#include "renderer/lighting_uniforms.h"
#include "renderer/scattered_light.h"
#include "renderer/scene_haze.h"
#include "test_support.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec3.hpp>

namespace {
const glm::vec3 eye(0.0f, 1.5f, 0.0f);

// The frame's light seen from `eye`, without haze, in air scattering
// `per_meter` with `forwardness`; the air takes a light as a surface does at
// a fifth of its reach, softened over half a metre.
frame_lighting_block night(const std::vector<render_light>& lights, const float per_meter, const float forwardness) {
    render_lighting lighting;
    lighting.weather.scatter_per_meter = per_meter;
    lighting.weather.scatter_forwardness = forwardness;
    lighting.beam_near_share = 0.2f;
    lighting.beam_core_meters = 0.5f;
    scene_haze haze;
    haze.full_distance = 1000.0f;
    haze.eye = eye;
    return make_frame_lighting_block(lighting, haze, lights);
}

render_light spot(const glm::vec3& position, const glm::vec3& direction, const float radius, const float cone_deg) {
    render_light light;
    light.position = position;
    light.color = glm::vec3(1.25f);
    light.radius = radius;
    light.direction = direction;
    light.cone_edge_cos = std::cos(glm::radians(cone_deg));
    light.cone_full_cos = std::cos(glm::radians(cone_deg * 0.5f));
    return light;
}

// A floodlight 15 m up, 30 m ahead, shining straight down.
render_light overhead_floodlight() {
    return spot(glm::vec3(0.0f, 15.0f, 30.0f), glm::vec3(0.0f, -1.0f, 0.0f), 65.0f, 40.0f);
}

float brightness(const glm::vec3& light) {
    return light.r + light.g + light.b;
}

// A look from the eye past `target`, twice as far again.
glm::vec3 past(const glm::vec3& target) {
    return eye + (target - eye) * 3.0f;
}

// What scattered_light works out, summed in fine steps along the look.
glm::vec3 summed_in_steps(const frame_lighting_block& block, const glm::vec3& world) {
    constexpr int steps = 200000;
    const glm::vec3 to = world - eye;
    const float far = glm::length(to);
    const glm::vec3 way = to / far;
    const float g = block.scatter.y;
    const float step = far / static_cast<float>(steps);
    glm::vec3 sum(0.0f);
    for (int k = 0; k < steps; ++k) {
        const glm::vec3 point = eye + way * (step * (static_cast<float>(k) + 0.5f));
        for (std::size_t i = 0; i < block.light_position.size(); ++i) {
            const reaching_light reaching = air_reaching(block, i, point);
            const float turn = 1.0f + g * g - 2.0f * g * glm::dot(reaching.towards, way);
            const float phase = (1.0f - g * g) / (4.0f * turn * std::sqrt(turn));
            sum += reaching.light * ((block.scatter.x + mist_at(block, point)) * phase * (1.0f - haze_at(block, point)) * step);
        }
    }
    return sum;
}
}

TEST_CASE("a floodlight's beam shows in fog where the eye looks through it, and nowhere else") {
    const frame_lighting_block fog = night({overhead_floodlight()}, 0.02f, 0.85f);
    // The ground beyond the lamp: the look runs under it, through its beam.
    const float through = brightness(scattered_light(fog, glm::vec3(0.0f, 0.0f, 60.0f)));
    CHECK(through > 0.001f);
    // The ground short of the beam, and a look up and away from it.
    CHECK(brightness(scattered_light(fog, glm::vec3(0.0f, 0.0f, 8.0f))) == 0.0f);
    CHECK(brightness(scattered_light(fog, glm::vec3(0.0f, 40.0f, -60.0f))) == 0.0f);
    // Up past the lamp, inside its reach but never in its cone.
    CHECK(brightness(scattered_light(fog, glm::vec3(0.0f, 60.0f, 30.0f))) == 0.0f);

    // Clear air is fifty times thinner than this fog: the beam all but goes.
    const frame_lighting_block clear = night({overhead_floodlight()}, 0.0002f, 0.85f);
    CHECK(brightness(scattered_light(clear, glm::vec3(0.0f, 0.0f, 60.0f))) < through / 50.0f);
}

TEST_CASE("fog glows looking into a headlight and barely along its back") {
    const glm::vec3 lamp(0.0f, 1.5f, 20.0f);
    const frame_lighting_block towards_eye = night({spot(lamp, glm::vec3(0.0f, 0.0f, -1.0f), 40.0f, 30.0f)}, 0.02f, 0.85f);
    const frame_lighting_block away = night({spot(lamp, glm::vec3(0.0f, 0.0f, 1.0f), 40.0f, 30.0f)}, 0.02f, 0.85f);
    // The look offset a little to the side, so it is never along the axis itself.
    const glm::vec3 beyond(1.0f, 1.5f, 60.0f);
    const float into = brightness(scattered_light(towards_eye, beyond));
    const float behind = brightness(scattered_light(away, beyond));
    CHECK(behind > 0.0f);
    CHECK(into > 20.0f * behind);
}

TEST_CASE("the air takes a lamp's light by the inverse square: a hundred times brighter at 3 m than at 30 m") {
    // A lamp reaching so far that its reach barely fades it here.
    render_light lamp;
    lamp.position = glm::vec3(0.0f, 20.0f, 0.0f);
    lamp.color = glm::vec3(1.0f);
    lamp.radius = 1000.0f;
    const frame_lighting_block far_reaching = night({lamp}, 0.02f, 0.85f);
    const auto air_at = [&](const float below) {
        return brightness(air_reaching(far_reaching, 0, lamp.position - glm::vec3(0.0f, below, 0.0f)).light);
    };
    CHECK(near(air_at(3.0f) / air_at(30.0f), 100.0f, 4.0f));
    // A fifth of its reach out, the air takes what a surface does.
    const frame_lighting_block fog = night({overhead_floodlight()}, 0.02f, 0.85f);
    const glm::vec3 there = glm::vec3(fog.light_position[0]) - glm::vec3(0.0f, 13.0f, 0.0f);
    const float surface = brightness(light_reaching(fog, 0, there).light);
    CHECK(near(brightness(air_reaching(fog, 0, there).light), surface, surface * 0.002f));
    // Inside its housing the light levels off: at the lamp itself it is (near / housing)².
    CHECK(near(air_at(0.0f), brightness(lamp.color) * (200.0f * 200.0f / (0.5f * 0.5f)), 1.0f));
}

TEST_CASE("the air's light along a look sums the lamp's light in it, even passing right by the lamp") {
    // Looks passing a floodlight's head and a headlight's at a hand's width, a few
    // metres and far off, and an omni light's reach.
    render_light omni;
    omni.position = eye + glm::vec3(5.0f, 0.0f, 20.0f);
    omni.color = glm::vec3(1.0f);
    omni.radius = 10.0f;
    const frame_lighting_block flood = night({overhead_floodlight()}, 0.02f, 0.85f);
    const frame_lighting_block head = night({spot(glm::vec3(0.0f, 1.5f, 20.0f), glm::vec3(0.0f, 0.0f, -1.0f), 40.0f, 30.0f)}, 0.02f, 0.6f);
    const frame_lighting_block bulb = night({omni}, 1.0f, 0.0f);
    const std::vector<std::pair<const frame_lighting_block*, glm::vec3>> looks{
        {&flood, past(glm::vec3(0.0f, 14.0f, 30.0f))},  {&flood, past(glm::vec3(0.0f, 10.0f, 30.0f))},
        {&flood, glm::vec3(0.0f, 0.0f, 60.0f)},          {&flood, glm::vec3(12.0f, 0.0f, 50.0f)},
        {&head, glm::vec3(0.1f, 1.5f, 60.0f)},           {&head, glm::vec3(3.0f, 1.5f, 60.0f)},
        {&bulb, eye + glm::vec3(0.0f, 0.0f, 100.0f)},
    };
    for (const auto& [block, world] : looks) {
        const float sampled = brightness(scattered_light(*block, world));
        const float summed = brightness(summed_in_steps(*block, world));
        CHECK(summed > 0.0f);
        CHECK(near(sampled, summed, summed * 0.15f));
    }
}

TEST_CASE("a floodlight's beam in fog is brightest at the lamp and faint down at the ground") {
    const frame_lighting_block fog = night({overhead_floodlight()}, 0.02f, 0.85f);
    // Looks across the beam just under the lamp's head and just over the ground.
    const float at_lamp = brightness(scattered_light(fog, past(glm::vec3(0.0f, 14.0f, 30.0f))));
    const float at_ground = brightness(scattered_light(fog, past(glm::vec3(0.0f, 2.0f, 30.0f))));
    CHECK(at_lamp > 10.0f * at_ground);
}

TEST_CASE("the haze glows with the sun looking towards it, and its mist reaches nearer") {
    render_lighting lighting;
    lighting.sun_direction = glm::normalize(glm::vec3(0.0f, 0.1f, 1.0f));
    lighting.sun_color = glm::vec3(1.0f, 0.7f, 0.4f);
    lighting.weather.sun_glow = 0.03f;
    lighting.weather.sun_forwardness = 0.7f;
    scene_haze haze;
    haze.color = glm::vec3(0.5f);
    haze.far_amount = 0.6f;
    haze.full_distance = 400.0f;
    haze.eye = eye;
    const frame_lighting_block block = make_frame_lighting_block(lighting, haze, {});
    // Towards a low sun the haze is brighter, and sun-coloured; away from it, it barely changes.
    const glm::vec3 towards = haze_toward(block, glm::normalize(glm::vec3(0.0f, 0.0f, 1.0f)));
    const glm::vec3 away = haze_toward(block, glm::normalize(glm::vec3(0.0f, 0.0f, -1.0f)));
    CHECK(towards.r > away.r + 0.2f);
    CHECK(towards.r - towards.b > 0.1f);
    CHECK(near(away, haze.color, 0.01f));
    render_lighting dark = lighting;
    dark.sun_color = glm::vec3(0.0f);
    CHECK(near(haze_toward(make_frame_lighting_block(dark, haze, {}), glm::vec3(0.0f, 0.0f, 1.0f)), haze.color));

    // The mist hazes the middle distance more, the far edge just as much.
    render_lighting misty = lighting;
    misty.weather.mist = 0.5f;
    const frame_lighting_block with_mist = make_frame_lighting_block(misty, haze, {});
    const glm::vec3 middle = eye + glm::vec3(0.0f, 0.0f, 100.0f);
    const glm::vec3 edge = eye + glm::vec3(0.0f, 0.0f, 400.0f);
    CHECK(haze_at(with_mist, middle) > 2.0f * haze_at(block, middle));
    CHECK(near(haze_at(with_mist, edge), haze_at(block, edge)));
}

TEST_CASE("a look at the sky gathers a lamp's light only as far as it reaches") {
    const frame_lighting_block fog = night({overhead_floodlight()}, 0.02f, 0.85f);
    const glm::vec3 way = glm::normalize(glm::vec3(0.0f, 0.2f, 1.0f));
    const glm::vec3 sky = scattered_light(fog, eye + way * 10000.0f);
    CHECK(brightness(sky) > 0.0f);
    CHECK(near(sky, scattered_light(fog, eye + way * 200.0f), 1.0e-6f));
}

namespace {
// Dawn mist, the sun low ahead along +z: its block seen from `eye`.
frame_lighting_block misty_dawn() {
    render_lighting lighting;
    lighting.sun_direction = glm::normalize(glm::vec3(0.0f, 0.2f, 1.0f));
    lighting.sun_color = glm::vec3(3.0f, 2.2f, 1.4f);
    lighting.weather.sun_glow = 0.03f;
    lighting.weather.sun_forwardness = 0.7f;
    lighting.weather.mist = 0.5f;
    scene_haze haze;
    haze.color = glm::vec3(0.6f, 0.62f, 0.66f);
    haze.far_amount = 0.9f;
    haze.full_distance = 240.0f;
    haze.eye = eye;
    return make_frame_lighting_block(lighting, haze, {});
}

// A trunk 0.5 m round and 10 m tall 20 m ahead of the eye: the sun it lets
// through to `point`, as the shadow map would say.
float past_trunk(const glm::vec3& point, const glm::vec3& towards_sun) {
    const glm::vec3 trunk(0.0f, 0.0f, 20.0f);
    if (point.z >= trunk.z) {
        return 1.0f;
    }
    const float along = (trunk.z - point.z) / towards_sun.z;
    const glm::vec3 at = point + towards_sun * along;
    return std::abs(at.x - trunk.x) < 0.5f && at.y < 10.0f ? 0.0f : 1.0f;
}
}

TEST_CASE("a trunk against a low sun casts a dark shaft in the misty air in front of it; the gap beside it keeps its glow") {
    const frame_lighting_block dawn = misty_dawn();
    const glm::vec3 towards_sun(dawn.sun_direction);
    const auto lit = [&towards_sun](const glm::vec3& point) { return past_trunk(point, towards_sun); };
    const int steps = 16;
    // Looking at the trunk, the air in front of it is in its shadow.
    const glm::vec3 at_trunk = glm::normalize(glm::vec3(0.0f, 0.5f, 20.0f));
    const glm::vec3 shaded = shaft_shade(dawn, lit, at_trunk, 20.0f, steps, 0.5f);
    CHECK(brightness(shaded) > 0.0f);
    // Sun-coloured: what is taken out is the sun's glow.
    CHECK(shaded.r > shaded.b);
    // Through the gap 3 m beside it the air is in sun but for the few metres
    // nearest the eye, where the shadow reaches back to it.
    const glm::vec3 beside = glm::normalize(glm::vec3(3.0f, 0.5f, 20.0f));
    CHECK(brightness(shaft_shade(dawn, lit, beside, 20.0f, steps, 0.5f)) < 0.2f * brightness(shaded));
    // With the trunk 3 m to the other side, that ray never meets its shadow.
    const auto trunk_aside = [&towards_sun](const glm::vec3& point) {
        return past_trunk(point + glm::vec3(3.0f, 0.0f, 0.0f), towards_sun);
    };
    CHECK(brightness(shaft_shade(dawn, trunk_aside, beside, 20.0f, steps, 0.5f)) == 0.0f);
}

TEST_CASE("air all in shadow takes out exactly the sun's glow the haze was drawn with, most looking at the sun") {
    const frame_lighting_block dawn = misty_dawn();
    const auto dark = [](const glm::vec3&) { return 0.0f; };
    const glm::vec3 towards(0.0f, 0.0f, 1.0f);
    const glm::vec3 away(0.0f, 0.0f, -1.0f);
    for (const float offset : {0.03f, 0.5f, 0.97f}) {
        const glm::vec3 shaded = shaft_shade(dawn, dark, towards, 80.0f, 16, offset);
        CHECK(near(shaded, haze_sun_glow(dawn, towards) * haze_over(dawn, towards, 80.0f), 1.0e-5f));
    }
    // That glow is what haze_toward adds to the haze's colour.
    CHECK(near(haze_toward(dawn, towards) - glm::vec3(dawn.haze_color), haze_sun_glow(dawn, towards), 1.0e-6f));
    // Looking away from the sun the air sends the eye little of it.
    CHECK(brightness(shaft_shade(dawn, dark, away, 80.0f, 16, 0.5f)) <
          0.1f * brightness(shaft_shade(dawn, dark, towards, 80.0f, 16, 0.5f)));
    // Nothing takes out more than the peak the pass is skipped under.
    const glm::vec3 most = shaft_shade(dawn, dark, glm::vec3(dawn.sun_direction), 160.0f, 16, 0.5f);
    CHECK(std::max(most.r, std::max(most.g, most.b)) <= shaft_peak(dawn, 160.0f) * 1.0001f);
    CHECK(std::max(most.r, std::max(most.g, most.b)) > 0.99f * shaft_peak(dawn, 160.0f));
    // With the sun down there is nothing to take out.
    frame_lighting_block night_block = dawn;
    night_block.sun_color = glm::vec4(0.0f);
    CHECK(shaft_peak(night_block, 160.0f) == 0.0f);
}

namespace {
// Dawn mist lying on ground at height 0, thinning by e every 6 m, taking
// 1% of the light per metre down there; the haze full at 400 m.
frame_lighting_block low_mist(const glm::vec3& from) {
    render_lighting lighting;
    lighting.sun_direction = glm::normalize(glm::vec3(0.0f, 0.2f, 1.0f));
    lighting.sun_color = glm::vec3(3.0f, 2.2f, 1.4f);
    lighting.weather.sun_glow = 0.03f;
    lighting.weather.sun_forwardness = 0.7f;
    lighting.weather.mist = 0.5f;
    lighting.mist_height_meters = 6.0f;
    lighting.mist_density = 0.01f;
    scene_haze haze;
    haze.color = glm::vec3(0.6f, 0.62f, 0.66f);
    haze.far_amount = 0.6f;
    haze.full_distance = 400.0f;
    haze.eye = from;
    haze.mist_floor = 0.0f;
    return make_frame_lighting_block(lighting, haze, {});
}

// mist_at summed in fine steps along `way` out to `meters`.
float mist_in_steps(const frame_lighting_block& block, const glm::vec3& way, const float meters) {
    constexpr int steps = 100000;
    const float step = meters / static_cast<float>(steps);
    double sum = 0.0;
    for (int k = 0; k < steps; ++k) {
        sum += static_cast<double>(mist_at(block, glm::vec3(block.haze_eye) + way * (step * (static_cast<float>(k) + 0.5f))));
    }
    return static_cast<float>(sum * static_cast<double>(step));
}
}

TEST_CASE("the mist lies low: a look along the ground is hazier than one as far up a slope, and tree tops stand out of it") {
    const frame_lighting_block dawn = low_mist(eye);
    const float far = 150.0f;
    const glm::vec3 along_ground = eye + glm::vec3(0.0f, -1.0f, far);
    const glm::vec3 up_slope = eye + glm::normalize(glm::vec3(0.0f, 0.3f, 1.0f)) * glm::length(along_ground - eye);
    CHECK(haze_at(dawn, along_ground) > haze_at(dawn, up_slope) + 0.2f);
    // Without the mist, only the distance counts.
    frame_lighting_block clear = dawn;
    clear.mist.z = 0.0f;
    CHECK(near(haze_at(clear, along_ground), haze_at(clear, up_slope), 1.0e-6f));
    CHECK(haze_at(dawn, along_ground) > haze_at(clear, along_ground) + 0.3f);
    // A tree's foot and its top 20 m up, both 100 m off.
    CHECK(haze_at(dawn, glm::vec3(0.0f, 20.0f, 100.0f)) < haze_at(dawn, glm::vec3(0.0f, 0.0f, 100.0f)) - 0.2f);
    // The mist thins upwards by e every 6 m.
    CHECK(near(mist_at(dawn, glm::vec3(0.0f, 6.0f, 0.0f)), 0.01f / std::exp(1.0f), 1.0e-6f));
}

TEST_CASE("the mist's closed form is the mist summed along the look, level or climbing, and never blows up") {
    const frame_lighting_block dawn = low_mist(eye);
    for (const glm::vec3& towards : {glm::vec3(0.0f, -0.0105f, 1.0f), glm::vec3(0.0f, 0.2f, 1.0f), glm::vec3(0.0f, -0.01f, 1.0f),
                                     glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.3f, -0.004f, 1.0f)}) {
        const glm::vec3 way = glm::normalize(towards);
        const float meters = std::min(140.0f, 1.4f / std::max(-way.y, 0.001f));
        const float summed = mist_in_steps(dawn, way, meters);
        CHECK(near(mist_depth(dawn, way, meters), summed, summed * 1.0e-3f));
    }
    // Level, it is the even mist at the eye's height all the way.
    const glm::vec3 level(0.0f, 0.0f, 1.0f);
    CHECK(near(mist_depth(dawn, level, 200.0f), mist_at(dawn, eye) * 200.0f, 1.0e-6f));
    // Either side of where the series takes over, the two agree.
    const float at_edge = 0.01f / (200.0f * dawn.mist.y);
    for (const float lean : {at_edge * 0.999f, at_edge * 1.001f, -at_edge * 0.999f, -at_edge * 1.001f, 1.0e-9f, -1.0e-9f}) {
        const glm::vec3 way = glm::normalize(glm::vec3(0.0f, lean, 1.0f));
        const float depth = mist_depth(dawn, way, 200.0f);
        CHECK(std::isfinite(depth));
        CHECK(near(depth, mist_in_steps(dawn, way, 200.0f), 1.0e-4f));
    }
    // From high above, looking straight down or level, it stays finite.
    // High enough that the mist at the eye is under the smallest float.
    frame_lighting_block from_above = low_mist(glm::vec3(0.0f, 900.0f, 0.0f));
    from_above.haze_eye.w = 1.0f / 2000.0f;
    const float down = mist_depth(from_above, glm::vec3(0.0f, -1.0f, 0.0f), 900.0f);
    CHECK(std::isfinite(down));
    CHECK(near(down, mist_in_steps(from_above, glm::vec3(0.0f, -1.0f, 0.0f), 900.0f), 1.0e-3f));
    CHECK(mist_depth(from_above, level, 400.0f) == 0.0f);
    CHECK(std::isfinite(haze_at(from_above, glm::vec3(0.0f, 0.0f, 300.0f))));
    // The backdrop's look steeply down past the lowest ground, the mist
    // thin and low: finite, and none at all when it has no density.
    frame_lighting_block steep = low_mist(eye);
    steep.mist.y = 1.0f;
    steep.haze_eye.w = 1.0f / 3000.0f;
    const glm::vec3 steeply_down = glm::normalize(glm::vec3(0.0f, -1.0f, 1.0f));
    CHECK(std::isfinite(mist_depth(steep, steeply_down, 10000.0f)));
    CHECK(haze_over(steep, steeply_down, 10000.0f) <= 1.0f);
    steep.mist.z = 0.0f;
    CHECK(mist_depth(steep, steeply_down, 10000.0f) == 0.0f);
}

TEST_CASE("the backdrop's seam holds in the mist: its land at the area's edge is as hazed as the ground there") {
    const frame_lighting_block dawn = low_mist(eye);
    // backdrop.frag: the land's own haze at the far ground (far_amount),
    // then the mist out to sky_distance, which stops at the full distance.
    constexpr float sky_distance = 10000.0f;
    for (const float drop : {-0.004f, -0.02f, 0.0f, 0.05f}) {
        const glm::vec3 way = glm::normalize(glm::vec3(0.3f, drop, 1.0f));
        const float land = 1.0f - (1.0f - dawn.haze_color.a) * std::exp(-mist_depth(dawn, way, sky_distance));
        const glm::vec3 edge = eye + way * (1.0f / dawn.haze_eye.w);
        CHECK(near(haze_at(dawn, edge), land, 1.0e-5f));
        // Past the edge it hazes no more.
        CHECK(near(haze_at(dawn, eye + way * (1.5f / dawn.haze_eye.w)), land, 1.0e-5f));
    }
}

TEST_CASE("in mist the shafts take out exactly the glow drawn, and the lamps' beams glow more low down") {
    const frame_lighting_block dawn = low_mist(eye);
    const auto dark = [](const glm::vec3&) { return 0.0f; };
    const glm::vec3 low_sun = glm::normalize(glm::vec3(0.0f, -0.01f, 1.0f));
    const glm::vec3 shaded = shaft_shade(dawn, dark, low_sun, 120.0f, 16, 0.5f);
    CHECK(near(shaded, haze_sun_glow(dawn, low_sun) * haze_over(dawn, low_sun, 120.0f), 1.0e-5f));
    for (const glm::vec3& way : {low_sun, glm::vec3(dawn.sun_direction), glm::vec3(0.0f, -0.3f, 0.95f)}) {
        const glm::vec3 most = shaft_shade(dawn, dark, glm::normalize(way), 160.0f, 16, 0.5f);
        CHECK(std::max(most.r, std::max(most.g, most.b)) <= shaft_peak(dawn, 160.0f) * 1.0001f);
    }
    // A floodlight's beam in clear air, with and without the mist on the ground.
    const frame_lighting_block clear_night = night({overhead_floodlight()}, 0.0002f, 0.85f);
    frame_lighting_block misty_night = clear_night;
    misty_night.mist = glm::vec4(0.0f, 1.0f / 6.0f, 0.01f, 0.0f);
    const glm::vec3 at_ground = past(glm::vec3(0.0f, 1.0f, 30.0f));
    const glm::vec3 at_lamp = past(glm::vec3(0.0f, 14.0f, 30.0f));
    const float low_gain = brightness(scattered_light(misty_night, at_ground)) / brightness(scattered_light(clear_night, at_ground));
    const float high_gain = brightness(scattered_light(misty_night, at_lamp)) / brightness(scattered_light(clear_night, at_lamp));
    CHECK(low_gain > 10.0f);
    CHECK(low_gain > 3.0f * high_gain);
    // Sampled as finely as the reference, still.
    const float sampled = brightness(scattered_light(misty_night, at_ground));
    const float summed = brightness(summed_in_steps(misty_night, at_ground));
    CHECK(near(sampled, summed, summed * 0.15f));
}
