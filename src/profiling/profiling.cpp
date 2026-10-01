#include "profiling/profiling.h"

#include <algorithm>

namespace {
std::uint64_t rounded(const double value) {
    if (value <= 0.0) {
        return 0U;
    }
    return static_cast<std::uint64_t>(value + 0.5);
}

std::string microseconds_text(const double milliseconds) {
    return std::to_string(rounded(milliseconds * 1000.0)) + "US";
}

void add_frame(frame_profile& total, const frame_profile& frame) {
    for (std::size_t i = 0; i < profile_stage_count; ++i) {
        total.stage_ms[i] += frame.stage_ms[i];
    }
    for (std::size_t i = 0; i < gpu_profile_stage_count; ++i) {
        total.gpu_stage_ms[i] += frame.gpu_stage_ms[i];
    }
    total.gpu_timers_available = total.gpu_timers_available || frame.gpu_timers_available;
    total.terrain_sample_calls += frame.terrain_sample_calls;
    total.terrain_triangles_tested += frame.terrain_triangles_tested;
    total.draw_calls += frame.draw_calls;
    total.uniform_sets += frame.uniform_sets;
    total.uniform_location_queries += frame.uniform_location_queries;
    total.buffer_reallocations += frame.buffer_reallocations;
    total.buffer_writes += frame.buffer_writes;
    total.buffer_upload_bytes += frame.buffer_upload_bytes;
    total.debug_overlay_draw_calls += frame.debug_overlay_draw_calls;
    total.debug_overlay_uniform_sets += frame.debug_overlay_uniform_sets;
    total.debug_overlay_buffer_calls += frame.debug_overlay_buffer_calls;
    total.debug_overlay_buffer_bytes += frame.debug_overlay_buffer_bytes;
    total.visible_chunks += frame.visible_chunks;
    total.culled_chunks += frame.culled_chunks;
    total.chunk_draw_ranges += frame.chunk_draw_ranges;
    total.chunk_indices_drawn += frame.chunk_indices_drawn;
    total.chunk_indices_total += frame.chunk_indices_total;
    total.trees_visible = total.trees_visible || frame.trees_visible;
    total.frame_ms += frame.frame_ms;
}

std::uint32_t average_u32(const std::uint32_t total, const int frames) {
    if (frames <= 0) {
        return 0U;
    }
    return static_cast<std::uint32_t>((static_cast<std::uint64_t>(total) + static_cast<std::uint64_t>(frames) / 2U) /
                                      static_cast<std::uint64_t>(frames));
}

std::uint64_t average_u64(const std::uint64_t total, const int frames) {
    if (frames <= 0) {
        return 0U;
    }
    return (total + static_cast<std::uint64_t>(frames) / 2U) / static_cast<std::uint64_t>(frames);
}

frame_profile averaged(const frame_profile& total, const int frames) {
    frame_profile result;
    if (frames <= 0) {
        return result;
    }

    const double divisor = static_cast<double>(frames);
    for (std::size_t i = 0; i < profile_stage_count; ++i) {
        result.stage_ms[i] = total.stage_ms[i] / divisor;
    }
    for (std::size_t i = 0; i < gpu_profile_stage_count; ++i) {
        result.gpu_stage_ms[i] = total.gpu_stage_ms[i] / divisor;
    }
    result.gpu_timers_available = total.gpu_timers_available;
    result.terrain_sample_calls = average_u32(total.terrain_sample_calls, frames);
    result.terrain_triangles_tested = average_u64(total.terrain_triangles_tested, frames);
    result.draw_calls = average_u32(total.draw_calls, frames);
    result.uniform_sets = average_u32(total.uniform_sets, frames);
    result.uniform_location_queries = average_u32(total.uniform_location_queries, frames);
    result.buffer_reallocations = average_u32(total.buffer_reallocations, frames);
    result.buffer_writes = average_u32(total.buffer_writes, frames);
    result.buffer_upload_bytes = average_u64(total.buffer_upload_bytes, frames);
    result.debug_overlay_draw_calls = average_u32(total.debug_overlay_draw_calls, frames);
    result.debug_overlay_uniform_sets = average_u32(total.debug_overlay_uniform_sets, frames);
    result.debug_overlay_buffer_calls = average_u32(total.debug_overlay_buffer_calls, frames);
    result.debug_overlay_buffer_bytes = average_u64(total.debug_overlay_buffer_bytes, frames);
    result.visible_chunks = average_u32(total.visible_chunks, frames);
    result.culled_chunks = average_u32(total.culled_chunks, frames);
    result.chunk_draw_ranges = average_u32(total.chunk_draw_ranges, frames);
    result.chunk_indices_drawn = average_u64(total.chunk_indices_drawn, frames);
    result.chunk_indices_total = average_u64(total.chunk_indices_total, frames);
    result.trees_visible = total.trees_visible;
    result.frame_ms = static_cast<float>(static_cast<double>(total.frame_ms) / divisor);
    return result;
}

double stage_value(const frame_profile& profile, const profile_stage stage) {
    return profile.stage_ms[static_cast<std::size_t>(stage)];
}

double gpu_stage_value(const frame_profile& profile, const gpu_profile_stage stage) {
    return profile.gpu_stage_ms[static_cast<std::size_t>(stage)];
}
}

void profiler_begin_frame(profiler& profile) {
    if (!profile.enabled) {
        profile.frame_open = false;
        return;
    }

    profile.frame = frame_profile{};
    profile.frame_open = true;
}

void profiler_end_frame(profiler& profile, const float frame_seconds) {
    if (!profile.frame_open) {
        return;
    }

    profile.frame_open = false;
    profile.frame.frame_ms = std::max(0.0f, frame_seconds) * 1000.0f;
    add_frame(profile.accumulator, profile.frame);
    ++profile.accumulated_frames;
    profile.accumulated_seconds += std::max(0.0f, frame_seconds);

    if (profile.accumulated_seconds < std::max(0.01f, profile.publish_interval_seconds)) {
        return;
    }

    profile.published = averaged(profile.accumulator, profile.accumulated_frames);
    profile.has_published = true;
    profile.accumulator = frame_profile{};
    profile.accumulated_frames = 0;
    profile.accumulated_seconds = 0.0f;
}

const char* profile_stage_label(const profile_stage stage) {
    switch (stage) {
    case profile_stage::update_game:
        return "UPD";
    case profile_stage::refresh_render_mesh_cache:
        return "MESH";
    case profile_stage::make_render_data:
        return "MRD";
    case profile_stage::render:
        return "REN";
    case profile_stage::render_scene:
        return "SCN";
    case profile_stage::render_overlay:
        return "OVL";
    case profile_stage::render_crt:
        return "CRT";
    case profile_stage::window_swap:
        return "SWP";
    case profile_stage::count:
        break;
    }
    return "";
}

const char* gpu_profile_stage_label(const gpu_profile_stage stage) {
    switch (stage) {
    case gpu_profile_stage::terrain:
        return "TER";
    case gpu_profile_stage::trees:
        return "TRE";
    case gpu_profile_stage::overlay:
        return "OVL";
    case gpu_profile_stage::crt:
        return "CRT";
    case gpu_profile_stage::count:
        break;
    }
    return "";
}

std::string format_byte_count(const std::uint64_t bytes) {
    constexpr std::uint64_t kilobyte = 1024U;
    constexpr std::uint64_t megabyte = kilobyte * kilobyte;
    if (bytes < kilobyte) {
        return std::to_string(bytes) + "B";
    }
    if (bytes < 10U * megabyte) {
        return std::to_string((bytes + kilobyte / 2U) / kilobyte) + "KB";
    }
    return std::to_string((bytes + megabyte / 2U) / megabyte) + "MB";
}

std::vector<std::string> format_profile_overlay_lines(const frame_profile& profile) {
    const auto cpu = [&profile](const profile_stage stage) {
        return std::string(profile_stage_label(stage)) + " " + microseconds_text(stage_value(profile, stage));
    };
    const auto gpu = [&profile](const gpu_profile_stage stage) {
        return std::string(gpu_profile_stage_label(stage)) + " " + microseconds_text(gpu_stage_value(profile, stage));
    };

    std::vector<std::string> lines;
    lines.reserve(10);
    lines.push_back(cpu(profile_stage::update_game) + " " + cpu(profile_stage::refresh_render_mesh_cache));
    lines.push_back(cpu(profile_stage::make_render_data) + " " + cpu(profile_stage::window_swap));
    lines.push_back(cpu(profile_stage::render) + " " + cpu(profile_stage::render_crt));
    lines.push_back(cpu(profile_stage::render_scene) + " " + cpu(profile_stage::render_overlay));
    lines.push_back("DRAW " + std::to_string(profile.draw_calls) + " UNI " + std::to_string(profile.uniform_sets) +
                    " ULOC " + std::to_string(profile.uniform_location_queries));
    // BUF glBufferSubData writes / glBufferData reallocations, then bytes.
    lines.push_back("BUF " + std::to_string(profile.buffer_writes) +
                    "/" + std::to_string(profile.buffer_reallocations) +
                    " " + format_byte_count(profile.buffer_upload_bytes) +
                    " DBGUI " + std::to_string(profile.debug_overlay_draw_calls));
    lines.push_back("TSAMP " + std::to_string(profile.terrain_sample_calls) +
                    " TTRI " + std::to_string(profile.terrain_triangles_tested));
    // Chunks drawn/culled, merged draw ranges, indices drawn, and whether the
    // tree batch survived the frustum test.
    lines.push_back("CHUNK " + std::to_string(profile.visible_chunks) +
                    "/" + std::to_string(profile.culled_chunks) +
                    " R " + std::to_string(profile.chunk_draw_ranges) +
                    " IDX " + std::to_string(profile.chunk_indices_drawn) +
                    " TREE " + std::string(profile.trees_visible ? "ON" : "OFF"));

    if (profile.gpu_timers_available) {
        lines.push_back("GPU " + gpu(gpu_profile_stage::terrain) + " " + gpu(gpu_profile_stage::trees));
        lines.push_back("GPU " + gpu(gpu_profile_stage::overlay) + " " + gpu(gpu_profile_stage::crt));
    }

    return lines;
}
