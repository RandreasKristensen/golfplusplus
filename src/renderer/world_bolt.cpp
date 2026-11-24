#include "renderer/world_bolt.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>

namespace {
constexpr int channel_segments = 14;
constexpr int branch_count = 3;
constexpr int branch_segments = 5;
// The top may lean this share of the height off the golfer's vertical.
constexpr float top_lean = 0.15f;
// A ribbon is this many times as wide as the bolt's core (which is a third of it).
constexpr float ribbon_over_core = 3.0f;
constexpr float top_strength = 0.4f;
constexpr float branch_start_strength = 0.6f;
constexpr float branch_end_strength = 0.1f;
constexpr float branch_width_share = 0.5f;
// A branch strikes out sideways this much for each unit it falls.
constexpr float branch_spread = 0.7f;
constexpr float branch_length_share = 0.9f;

// A fixed value in [0, 1) for the strike seeded `seed`, number `i` (splitmix64).
float unit(const float seed, const int i) {
    std::uint64_t x = static_cast<std::uint64_t>(static_cast<std::int64_t>(seed * 1000.0f)) * 0x9E3779B97F4A7C15ULL +
        static_cast<std::uint64_t>(i + 1) * 0xD1B54A32D192ED03ULL;
    x = (x ^ (x >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27U)) * 0x94D049BB133111EBULL;
    x ^= x >> 31U;
    return static_cast<float>(x >> 40U) / 16777216.0f;
}

struct path_point {
    glm::vec3 position{0.0f};
    float strength = 1.0f;
};

// `path` as ribbons, `core_meters` wide at least and as wide as `angular`
// (radians, half the core) makes it at the camera's distance.
void add_ribbons(std::vector<world_bolt_vertex>& out, const std::vector<path_point>& path, const glm::vec3& camera,
                 const float core_meters, const float angular) {
    struct edge {
        world_bolt_vertex left;
        world_bolt_vertex right;
    };
    std::vector<edge> edges;
    edges.reserve(path.size());
    for (std::size_t i = 0; i < path.size(); ++i) {
        const glm::vec3 along = path[std::min(i + 1, path.size() - 1)].position - path[i == 0 ? 0 : i - 1].position;
        const glm::vec3 view = path[i].position - camera;
        glm::vec3 side = glm::cross(along, view);
        if (glm::dot(side, side) < 1e-12f) {
            side = glm::cross(along, glm::vec3(1.0f, 0.0f, 0.0f));
        }
        side = glm::normalize(side);
        const float core = std::max(core_meters, 2.0f * glm::length(view) * angular);
        const glm::vec3 reach = side * (0.5f * ribbon_over_core * core);
        edges.push_back(edge{{path[i].position - reach, -1.0f, path[i].strength},
                             {path[i].position + reach, 1.0f, path[i].strength}});
    }
    for (std::size_t i = 0; i + 1 < edges.size(); ++i) {
        const edge& a = edges[i];
        const edge& b = edges[i + 1];
        for (const world_bolt_vertex& vertex : {a.left, a.right, b.right, a.left, b.right, b.left}) {
            out.push_back(vertex);
        }
    }
}
}

std::vector<world_bolt_vertex> make_world_bolt(const render_lightning& lightning, const glm::vec3& camera) {
    std::vector<world_bolt_vertex> out;
    if (!lightning.point || lightning.flash <= 0.0f || lightning.world_height <= 0.0f) {
        return out;
    }
    const glm::vec3 feet = *lightning.point;
    const float seed = lightning.seed;
    const float height = lightning.world_height;
    const glm::vec3 top = feet + glm::vec3((unit(seed, 0) - 0.5f) * 2.0f * top_lean * height, height,
                                           (unit(seed, 1) - 0.5f) * 2.0f * top_lean * height);
    const float segment = height / static_cast<float>(channel_segments);
    const float wander = lightning.world_wander * segment;

    // The channel wanders sideways in the middle and comes straight down at
    // the end, onto the feet.
    std::vector<path_point> channel;
    for (int i = 0; i <= channel_segments; ++i) {
        const float along = static_cast<float>(i) / static_cast<float>(channel_segments);
        path_point at{glm::mix(top, feet, along), i == 0 ? top_strength : 1.0f};
        if (i > 0 && i < channel_segments - 1) {
            at.position.x += (unit(seed, 2 + 2 * i) * 2.0f - 1.0f) * wander;
            at.position.z += (unit(seed, 3 + 2 * i) * 2.0f - 1.0f) * wander;
        }
        channel.push_back(at);
    }
    add_ribbons(out, channel, camera, lightning.world_width, lightning.bolt_width);

    const int draws = 2 + 2 * (channel_segments + 1);
    for (int b = 0; b < branch_count; ++b) {
        const int from = 2 + static_cast<int>(unit(seed, draws + 3 * b) * static_cast<float>(channel_segments - 5));
        const float around = unit(seed, draws + 3 * b + 1) * glm::two_pi<float>();
        const glm::vec3 heading =
            glm::normalize(glm::vec3(std::cos(around) * branch_spread, -1.0f, std::sin(around) * branch_spread));
        std::vector<path_point> branch;
        glm::vec3 at = channel[static_cast<std::size_t>(from)].position;
        for (int j = 0; j <= branch_segments; ++j) {
            const float along = static_cast<float>(j) / static_cast<float>(branch_segments);
            branch.push_back(path_point{at, glm::mix(branch_start_strength, branch_end_strength, along)});
            const int jitter = draws + 3 * branch_count + b * 2 * branch_segments + 2 * j;
            at += heading * (segment * branch_length_share) +
                glm::vec3(unit(seed, jitter) * 2.0f - 1.0f, 0.0f, unit(seed, jitter + 1) * 2.0f - 1.0f) * wander;
            at.y = std::max(at.y, feet.y);
        }
        add_ribbons(out, branch, camera, lightning.world_width * branch_width_share,
                    lightning.bolt_width * branch_width_share);
    }
    return out;
}
