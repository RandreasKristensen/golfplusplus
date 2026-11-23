#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include <glm/vec3.hpp>

// The ground's vertex as the GPU holds it, 32 bytes, and what a ground
// attribute rounds to in it. Every setter rounds the way the GPU reads the
// value back (`describe_render_terrain_vertex`, `terrain.vert`), so anything
// compared or blended on the CPU (the coarse levels, the grass field) sees the
// vertex as drawn, and the rounding is the same on every machine: fixed steps
// of the value, never of the view.
//   normal   10 bits an axis, 0.002 at most (0.1 degrees); the flat ground's
//            straight-up normal is exact
//   colour   8 bits a channel in linear light, 0.002 at most
//   rough, paved, ao   8 bits, 0.002 at most
//   turf, blades       0..2 in steps of 1/127, so 0, 1 and 2 are exact
struct render_terrain_vertex {
    glm::vec3 position = glm::vec3(0.0f);
    // GL_INT_2_10_10_10_REV: x, y, z as 10-bit signed normalized.
    std::uint32_t normal_bits = 511U << 10U;
    std::array<std::uint8_t, 4> color_rough = {255, 255, 255, 0};  // colour red, green, blue; rough
    // How much the cart path's asphalt shows; how much sky it sees
    // (renderer/sky_visibility.h); the turf (1 on a fairway, 2 on a green, 0
    // elsewhere, between where they meet: the grass style's fibre); the
    // grass's blades (1 rough, 2 heavy rough, as tall as the grass style says,
    // 0 none, between where they meet).
    std::array<std::uint8_t, 4> paved_ao_turf_blades = {0, 255, 0, 0};
    // The sine of the horizon's elevation each of the 8 horizon ways
    // (sky_visibility.h), 0..255 for 0..1: where the sun is hidden behind the land.
    std::array<std::uint8_t, 8> horizon{};

    static constexpr float level_steps = 127.0f;  // turf and blades, per 1.0

    glm::vec3 normal() const {
        const auto axis = [this](const unsigned shift) {
            const auto bits = static_cast<std::int32_t>((normal_bits >> shift) & 1023U);
            return std::max(static_cast<float>(bits >= 512 ? bits - 1024 : bits) / 511.0f, -1.0f);
        };
        return glm::vec3(axis(0), axis(10), axis(20));
    }
    void set_normal(const glm::vec3& value) {
        const auto axis = [](const float component) {
            return static_cast<std::uint32_t>(static_cast<std::int32_t>(std::lround(std::clamp(component, -1.0f, 1.0f) * 511.0f))) & 1023U;
        };
        normal_bits = axis(value.x) | axis(value.y) << 10U | axis(value.z) << 20U;
    }

    glm::vec3 color() const { return glm::vec3(unit(color_rough[0]), unit(color_rough[1]), unit(color_rough[2])); }
    void set_color(const glm::vec3& value) {
        color_rough[0] = byte(value.x);
        color_rough[1] = byte(value.y);
        color_rough[2] = byte(value.z);
    }

    float rough() const { return unit(color_rough[3]); }
    void set_rough(const float value) { color_rough[3] = byte(value); }
    float paved() const { return unit(paved_ao_turf_blades[0]); }
    void set_paved(const float value) { paved_ao_turf_blades[0] = byte(value); }
    float ao() const { return unit(paved_ao_turf_blades[1]); }
    void set_ao(const float value) { paved_ao_turf_blades[1] = byte(value); }
    float turf() const { return level(paved_ao_turf_blades[2]); }
    void set_turf(const float value) { paved_ao_turf_blades[2] = step(value); }
    float blades() const { return level(paved_ao_turf_blades[3]); }
    void set_blades(const float value) { paved_ao_turf_blades[3] = step(value); }

private:
    static float unit(const std::uint8_t value) { return static_cast<float>(value) / 255.0f; }
    static std::uint8_t byte(const float value) {
        return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
    }
    static float level(const std::uint8_t value) { return static_cast<float>(value) / level_steps; }
    static std::uint8_t step(const float value) {
        return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0f, 255.0f / level_steps) * level_steps));
    }
};

static_assert(sizeof(render_terrain_vertex) == 32, "the ground's vertex is 32 bytes");
