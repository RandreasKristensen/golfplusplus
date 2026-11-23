#include "doctest.h"

#include "physics/terrain.h"
#include "profiling/profiling.h"
#include "profiling/timestamp_ring.h"
#include "renderer/gl_storage.h"

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

    auto* frame = profiler_begin_frame(profile);
    CHECK(frame == nullptr);

    record_draw_call(frame, 10);

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
    record_draw_call(&profile.frame, 100);
    record_terrain_sample(&profile.frame, 1000);
    profile.frame.frame_ms = 100;
    publish_profile(profile, profile.frame);
    CHECK(!(profile.has_published));

    profiler_begin_frame(profile);
    CHECK(profile.frame.draw_calls == 0U);
    record_draw_call(&profile.frame, 200);
    record_terrain_sample(&profile.frame, 2000);
    profile.frame.frame_ms = 100;
    publish_profile(profile, profile.frame);
    CHECK(!(profile.has_published));

    // Third frame crosses the interval and publishes the window average.
    profiler_begin_frame(profile);
    record_draw_call(&profile.frame, 300);
    record_terrain_sample(&profile.frame, 3000);
    profile.frame.frame_ms = 100;
    publish_profile(profile, profile.frame);

    CHECK(profile.has_published);
    CHECK(profile.published.draw_calls == 200U);
    CHECK(profile.published.terrain_sample_calls == 1U);
    CHECK(profile.published.terrain_triangles_tested == 2000U);
    CHECK(profile.accumulated_frames == 0);

    // The accumulator resets, so the next window starts clean.
    profiler_begin_frame(profile);
    record_draw_call(&profile.frame, 10);
    profile.frame.frame_ms = 300;
    publish_profile(profile, profile.frame);
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

TEST_CASE("chunk culling counters are averaged and shown on the overlay") {
    profiler profile;
    profile.enabled = true;

    // Two frames: the first sees the trees, the second has them culled away.
    profiler_begin_frame(profile);
    profile.frame.visible_chunks = 80U;
    profile.frame.culled_chunks = 280U;
    profile.frame.chunk_draw_ranges = 4U;
    profile.frame.chunk_indices_drawn = 11000U;
    profile.frame.chunk_indices_total = 51876U;
    profile.frame.trees_visible = true;
    profile.frame.frame_ms = 200;
    publish_profile(profile, profile.frame);

    profiler_begin_frame(profile);
    profile.frame.visible_chunks = 90U;
    profile.frame.culled_chunks = 270U;
    profile.frame.chunk_draw_ranges = 4U;
    profile.frame.chunk_indices_drawn = 12000U;
    profile.frame.chunk_indices_total = 51876U;
    profile.frame.trees_visible = false;
    profile.frame.frame_ms = 200;
    publish_profile(profile, profile.frame);

    CHECK(profile.has_published);
    CHECK(profile.published.visible_chunks == 85U);
    CHECK(profile.published.culled_chunks == 275U);
    CHECK(profile.published.chunk_draw_ranges == 4U);
    CHECK(profile.published.chunk_indices_drawn == 11500U);
    CHECK(profile.published.chunk_indices_total == 51876U);
    // Visible in any frame of the window counts as visible, like gpu availability.
    CHECK(profile.published.trees_visible);

    const std::vector<std::string> lines = format_profile_overlay_lines(profile.published);
    CHECK(any_line_contains(lines, "CHUNK 85/275"));
    CHECK(any_line_contains(lines, "R 4"));
    CHECK(any_line_contains(lines, "IDX 11500"));
    CHECK(any_line_contains(lines, "TREE ON"));

    frame_profile culled;
    culled.trees_visible = false;
    CHECK(any_line_contains(format_profile_overlay_lines(culled), "TREE OFF"));
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

    profile.gpu_timers_available = true;
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

TEST_CASE("timestamp ring defers reads and preserves unfinished frames on wrap") {
    timestamp_ring ring;
    CHECK(!ring.oldest(0));
    CHECK(ring.reserve(0) == 0);
    CHECK(!ring.oldest(1));
    CHECK(ring.reserve(1) == 1);
    CHECK(ring.oldest(2) == 0);
    CHECK(ring.reserve(2) == 2);
    CHECK(ring.reserve(3) == 3);
    CHECK(!ring.reserve(4));
    CHECK(ring.oldest(5) == 0);
    ring.release();
    CHECK(ring.reserve(5) == 0);
    for (unsigned slot = 1; slot <= 3; ++slot) {
        CHECK(ring.oldest(5) == slot);
        ring.release();
    }
    CHECK(!ring.oldest(5));
    CHECK(ring.oldest(5, true) == 0);
}

TEST_CASE("repeated timestamp segments accumulate under their parent independently of frame time") {
    frame_profile frame;
    frame.gpu_frame_ms = 10;
    const auto props = gpu_profile_stage::props;
    record_timestamp_segment(frame, props, gpu_stage_group::scene, 0, 1000000);
    record_timestamp_segment(frame, props, gpu_stage_group::scene, 1500000, 3500000);
    record_timestamp_segment(frame, gpu_profile_stage::reflection, gpu_stage_group::scene, 4000000, 9000000);
    record_timestamp_segment(frame, props, gpu_stage_group::reflection, 5000000, 5500000);
    record_timestamp_segment(frame, props, gpu_stage_group::reflection, 6000000, 7500000);
    CHECK(frame.gpu_ms[0][static_cast<std::size_t>(props)] == 3);
    CHECK(frame.gpu_ms[static_cast<std::size_t>(gpu_stage_group::reflection)][static_cast<std::size_t>(props)] == 2);
    CHECK(frame.gpu_ms[0][static_cast<std::size_t>(gpu_profile_stage::reflection)] == 5);
    const auto zoom = child_stage_group(gpu_profile_stage::zoom, gpu_stage_group::scene);
    const auto mirror = child_stage_group(gpu_profile_stage::reflection, zoom);
    CHECK(mirror == gpu_stage_group::zoom_reflection);
    record_timestamp_segment(frame, props, mirror, 7000000, 7500000);
    CHECK(frame.gpu_ms[static_cast<std::size_t>(mirror)][static_cast<std::size_t>(props)] == 0.5);
    CHECK(frame.gpu_frame_ms == 10);
}

TEST_CASE("per-pass submissions and frame counters reset between raw records") {
    profiler recorder;
    recorder.enabled = true;
    profiler_begin_frame(recorder);
    auto& frame = recorder.frame;
    frame.work.current().tree_instances = 4;
    {
        const profile_pass_scope scope(&frame, profile_pass::reflection);
        frame.work.current().tree_instances = 8;
        frame.work.reflection_captures = 2;
    }
    CHECK(frame.work.current().tree_instances == 4);
    frame.work.static_upload_bytes = 512;
    frame.gpu_texture_bytes = 1024;
    profiler_begin_frame(recorder);
    CHECK(frame.frame_number == 1);
    CHECK(frame.work.current().tree_instances == 0);
    CHECK(frame.work.passes[static_cast<std::size_t>(profile_pass::reflection)].tree_instances == 0);
    CHECK(frame.work.reflection_captures == 0);
    CHECK(frame.work.static_upload_bytes == 0);
    CHECK(frame.gpu_texture_bytes == 0);
}

TEST_CASE("logical texture storage includes odd non-square mip tails") {
    CHECK(texture_storage_bytes(5, 3, 4, false) == 60);
    CHECK(texture_storage_bytes(5, 3, 4, true) == 72);
    CHECK(texture_storage_bytes(1, 8, 12, true) == 180);
    CHECK(texture_storage_bytes(0, 8, 12, true) == 0);
}

TEST_CASE("logical allocations track live sizes across replacement, deletion and name reuse") {
    allocation_table storage;
    CHECK(storage.replace(1, 100));
    CHECK(storage.replace(2, 30));
    CHECK(!storage.replace(1, 100));
    CHECK(!storage.replace(1, 80));
    CHECK(storage.bytes() == 110);
    CHECK(storage.bytes(1) == 80);
    CHECK(storage.bytes(2) == 30);
    CHECK(storage.bytes(3) == 0);
    storage.erase(2);
    storage.erase(2);
    CHECK(storage.bytes() == 80);
    CHECK(storage.replace(2, 20));
    CHECK(storage.bytes() == 100);
}

TEST_CASE("overlay averages GPU intervals over frames with available timestamps") {
    profiler recorder;
    frame_profile available;
    available.frame_ms = 100;
    available.gpu_frame_ms = 4;
    available.gpu_timers_available = true;
    record_timestamp_segment(available, gpu_profile_stage::terrain, gpu_stage_group::scene, 0, 1500000);
    publish_profile(recorder, available);
    frame_profile busy;
    busy.frame_ms = 200;
    publish_profile(recorder, busy);
    CHECK(recorder.published.frame_ms == 150);
    CHECK(recorder.published.gpu_frame_ms == 4);
    CHECK(recorder.published.gpu_ms[0][static_cast<std::size_t>(gpu_profile_stage::terrain)] == 1.5);
}
