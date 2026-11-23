#pragma once

// Submitted geometry by view, upload traffic and resident process memory in bytes.
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

enum class profile_pass : std::size_t { main, zoom, reflection, zoom_reflection, sun_shadow, lamp_shadow, count };

struct pass_work {
    std::uint64_t tree_instances = 0;
    std::uint64_t tree_triangles = 0;
    std::uint64_t tree_images = 0;  // trees drawn as their baked images
    std::uint64_t grass_instances = 0;
    std::uint64_t terrain_indices_visible = 0;
    std::uint64_t terrain_indices_submitted = 0;
};

struct frame_accounting {
    std::array<pass_work, static_cast<std::size_t>(profile_pass::count)> passes{};
    profile_pass active_pass = profile_pass::main;
    std::uint64_t water_candidates = 0;
    std::uint64_t reflection_levels = 0;  // candidate levels reaching the mirror stage, captured or not
    std::uint64_t reflection_captures = 0;
    std::uint64_t reflection_pixels = 0;
    std::uint64_t texture_allocations = 0;
    std::uint64_t buffer_growths = 0;
    std::uint64_t static_uploads = 0;
    std::uint64_t static_upload_bytes = 0;
    std::uint64_t dynamic_upload_bytes = 0;
    std::uint64_t state_readbacks = 0;  // glGet* and glIsEnabled calls, each a wait for the driver

    pass_work& current() { return passes[static_cast<std::size_t>(active_pass)]; }
};

struct process_memory {
    std::optional<std::uint64_t> resident_bytes;
    std::optional<std::uint64_t> peak_resident_bytes;
};

process_memory sample_process_memory();

inline std::uint64_t texture_storage_bytes(int width, int height, const unsigned bytes_per_pixel, const bool mipmaps) {
    std::uint64_t bytes = 0;
    while (width > 0 && height > 0) {
        bytes += static_cast<std::uint64_t>(width) * height * bytes_per_pixel;
        if (!mipmaps || (width == 1 && height == 1)) { break; }
        width = width > 1 ? width / 2 : 1;
        height = height > 1 ? height / 2 : 1;
    }
    return bytes;
}
