#include "doctest.h"

#include "physics/terrain.h"
#include "profiling/profiling.h"

#include <chrono>
#include <cmath>
#include <string>
#include <vector>

namespace {
double stage(const frame_profile& profile, const profile_stage id) {
    return profile.stage_ms[static_cast<std::size_t>(id)];
}

bool any_line_contains(const std::vector<std::string>& lines, const std::string& needle) {
    for (const std::string& line : lines) {
        if (line.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

terrain_spline make_test_spline() {
    terrain_spline spline;
    spline.control_points = {
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 20.0f),
        glm::vec3(0.0f, 0.0f, 40.0f)
    };
    spline.width = 12.0f;
    spline.fairway_width = 8.0f;
    spline.sample_count = 12;
    return spline;
}
}

TEST_CASE("record helpers are no-ops on a null profile") {
    record_draw_call(nullptr);
    record_uniform_set(nullptr, 5);
    record_buffer_upload(nullptr, 128);
    record_buffer_write(nullptr, 128);
    record_terrain_sample(nullptr, 42);
    record_gpu_stage(nullptr, gpu_profile_stage::terrain, 1.0);

    const profile_scope timer(nullptr, profile_stage::render);
    CHECK(true);
}

TEST_CASE("counters accumulate onto an explicit frame profile") {
    frame_profile profile;

    record_draw_call(&profile);
    record_draw_call(&profile, 4);
    record_uniform_set(&profile);
    record_uniform_set(&profile, 9);
    record_buffer_upload(&profile, 64);
    record_buffer_write(&profile, 32);
    record_terrain_sample(&profile, 120);
    record_terrain_sample(&profile, 80);
    record_terrain_sample(&profile, -3);
    record_uniform_location_query(&profile);
    record_uniform_location_query(&profile);

    CHECK(profile.draw_calls == 5U);
    CHECK(profile.uniform_sets == 10U);
    CHECK(profile.uniform_location_queries == 2U);
    CHECK(profile.buffer_reallocations == 1U);
    CHECK(profile.buffer_writes == 1U);
    CHECK(profile.buffer_upload_bytes == 96U);
    CHECK(profile.terrain_sample_calls == 3U);
    CHECK(profile.terrain_triangles_tested == 200U);
}

TEST_CASE("gpu stage recording marks timers as available") {
    frame_profile profile;
    CHECK(!(profile.gpu_timers_available));

    record_gpu_stage(&profile, gpu_profile_stage::crt, 0.5);
    record_gpu_stage(&profile, gpu_profile_stage::crt, 0.25);

    CHECK(profile.gpu_timers_available);
    CHECK(std::abs(profile.gpu_stage_ms[static_cast<std::size_t>(gpu_profile_stage::crt)] - 0.75) < 0.000001);
}

TEST_CASE("profile_scope records elapsed time into its stage and accumulates") {
    frame_profile profile;
    CHECK(stage(profile, profile_stage::update_game) == 0.0);

    {
        const profile_scope timer(&profile, profile_stage::update_game);
        volatile double sink = 0.0;
        for (int i = 0; i < 200000; ++i) {
            sink = sink + static_cast<double>(i);
        }
        (void)sink;
    }

    const double first = stage(profile, profile_stage::update_game);
    CHECK(first > 0.0);
    CHECK(stage(profile, profile_stage::render) == 0.0);

    {
        const profile_scope timer(&profile, profile_stage::update_game);
    }

    CHECK(stage(profile, profile_stage::update_game) >= first);
}

TEST_CASE("disabled profiler hands out a null frame and records nothing") {
    profiler profile;
    profile.enabled = false;

    profiler_begin_frame(profile);
    CHECK(profiler_frame(profile) == nullptr);

    record_draw_call(profiler_frame(profile), 10);
    profiler_end_frame(profile, 0.016f);

    CHECK(profile.frame.draw_calls == 0U);
    CHECK(!(profile.has_published));
    CHECK(profile.accumulated_frames == 0);
}

TEST_CASE("enabled profiler clears each frame and publishes an average") {
    profiler profile;
    profile.enabled = true;
    profile.publish_interval_seconds = 0.25f;

    // Two frames below the publish interval: nothing published yet.
    profiler_begin_frame(profile);
    record_draw_call(profiler_frame(profile), 100);
    record_terrain_sample(profiler_frame(profile), 1000);
    profiler_end_frame(profile, 0.1f);
    CHECK(!(profile.has_published));

    profiler_begin_frame(profile);
    CHECK(profile.frame.draw_calls == 0U);
    record_draw_call(profiler_frame(profile), 200);
    record_terrain_sample(profiler_frame(profile), 2000);
    profiler_end_frame(profile, 0.1f);
    CHECK(!(profile.has_published));

    // Third frame crosses the interval and publishes the window average.
    profiler_begin_frame(profile);
    record_draw_call(profiler_frame(profile), 300);
    record_terrain_sample(profiler_frame(profile), 3000);
    profiler_end_frame(profile, 0.1f);

    CHECK(profile.has_published);
    CHECK(profile.published.draw_calls == 200U);
    CHECK(profile.published.terrain_sample_calls == 1U);
    CHECK(profile.published.terrain_triangles_tested == 2000U);
    CHECK(profile.accumulated_frames == 0);

    // The accumulator resets, so the next window starts clean.
    profiler_begin_frame(profile);
    record_draw_call(profiler_frame(profile), 10);
    profiler_end_frame(profile, 0.3f);
    CHECK(profile.published.draw_calls == 10U);
}

TEST_CASE("debug overlay cost is moved out of the frame counters") {
    frame_profile profile;
    record_draw_call(&profile, 40);
    record_uniform_set(&profile, 90);

    record_buffer_write(&profile, 2048);

    const debug_overlay_cost_mark mark = mark_debug_overlay_cost(&profile);
    record_draw_call(&profile, 700);
    record_uniform_set(&profile, 3500);
    record_buffer_write(&profile, 512);
    record_buffer_upload(&profile, 0U);
    reclaim_debug_overlay_cost(&profile, mark);

    CHECK(profile.draw_calls == 40U);
    CHECK(profile.uniform_sets == 90U);
    CHECK(profile.debug_overlay_draw_calls == 700U);
    CHECK(profile.debug_overlay_uniform_sets == 3500U);
    // The overlay's own upload leaves the frame buffer counters untouched.
    CHECK(profile.buffer_writes == 1U);
    CHECK(profile.buffer_reallocations == 0U);
    CHECK(profile.buffer_upload_bytes == 2048U);
    CHECK(profile.debug_overlay_buffer_calls == 2U);
    CHECK(profile.debug_overlay_buffer_bytes == 512U);
}

TEST_CASE("overlay lines only use glyphs the bitmap font can render") {
    frame_profile profile;
    profile.stage_ms[static_cast<std::size_t>(profile_stage::update_game)] = 1.5;
    profile.draw_calls = 268U;
    profile.uniform_sets = 1904U;
    profile.uniform_location_queries = 7U;
    profile.terrain_sample_calls = 105U;
    profile.terrain_triangles_tested = 1250400U;
    profile.buffer_writes = 3U;
    profile.buffer_reallocations = 1U;
    profile.buffer_upload_bytes = 12288U;

    const std::vector<std::string> lines = format_profile_overlay_lines(profile);
    CHECK(!(lines.empty()));
    CHECK(any_line_contains(lines, "DRAW 268"));
    CHECK(any_line_contains(lines, "UNI 1904"));
    CHECK(any_line_contains(lines, "ULOC 7"));
    CHECK(any_line_contains(lines, "TSAMP 105"));
    CHECK(any_line_contains(lines, "TTRI 1250400"));
    CHECK(any_line_contains(lines, "1500US"));
    CHECK(any_line_contains(lines, "BUF 3/1 12KB"));

    for (const std::string& line : lines) {
        for (const char c : line) {
            const bool ok = (c >= 'A' && c <= 'Y') ||
                            (c >= '0' && c <= '9') ||
                            c == ' ' || c == '/' || c == '-' || c == '+' || c == ':';
            CHECK(ok);
        }
    }
}

TEST_CASE("byte counts are formatted compactly for the overlay") {
    CHECK(format_byte_count(0U) == "0B");
    CHECK(format_byte_count(512U) == "512B");
    CHECK(format_byte_count(1023U) == "1023B");
    CHECK(format_byte_count(1024U) == "1KB");
    CHECK(format_byte_count(12288U) == "12KB");
    CHECK(format_byte_count(1600000U) == "1563KB");
    CHECK(format_byte_count(64U * 1024U * 1024U) == "64MB");
}

TEST_CASE("gpu lines only appear once timer queries have reported") {
    frame_profile profile;
    CHECK(!(any_line_contains(format_profile_overlay_lines(profile), "GPU")));

    record_gpu_stage(&profile, gpu_profile_stage::terrain, 1.0);
    CHECK(any_line_contains(format_profile_overlay_lines(profile), "GPU"));
}

TEST_CASE("terrain samples report the triangles they tested as a pure output") {
    const terrain_mesh mesh = build_terrain_mesh(make_test_spline());
    CHECK(mesh.indices.size() >= 3U);

    const terrain_sample inside = sample_terrain_mesh(mesh, glm::vec3(0.0f, 0.0f, 20.0f), 0.0f);
    CHECK(inside.triangles_tested > 0);

    // Pure: the same inputs always report the same amount of work.
    const terrain_sample repeat = sample_terrain_mesh(mesh, glm::vec3(0.0f, 0.0f, 20.0f), 0.0f);
    CHECK(repeat.triangles_tested == inside.triangles_tested);

    const terrain_sample outside = sample_terrain_mesh(mesh, glm::vec3(900.0f, 0.0f, 900.0f), 0.0f);
    CHECK(outside.triangles_tested > 0);

    frame_profile profile;
    record_terrain_sample(&profile, inside.triangles_tested);
    record_terrain_sample(&profile, outside.triangles_tested);
    CHECK(profile.terrain_sample_calls == 2U);
    CHECK(profile.terrain_triangles_tested ==
          static_cast<std::uint64_t>(inside.triangles_tested) + static_cast<std::uint64_t>(outside.triangles_tested));
}

TEST_CASE("empty terrain meshes report no triangle work") {
    const terrain_mesh empty;
    const terrain_sample sample = sample_terrain_mesh(empty, glm::vec3(1.0f, 0.0f, 2.0f), 3.0f);
    CHECK(sample.triangles_tested == 0);
}
