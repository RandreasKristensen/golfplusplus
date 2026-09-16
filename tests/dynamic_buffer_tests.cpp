#include "doctest.h"

#include "profiling/profiling.h"
#include "renderer/dynamic_buffer.h"

#include <cstddef>

TEST_CASE("dynamic buffer capacity only grows") {
    CHECK(grow_buffer_capacity(0, 0) == 0U);
    CHECK(grow_buffer_capacity(0, 100) == 100U);
    CHECK(grow_buffer_capacity(1000, 999) == 1000U);
    CHECK(grow_buffer_capacity(1000, 1000) == 1000U);
    CHECK(grow_buffer_capacity(1000, 1001) == 2000U);
    CHECK(grow_buffer_capacity(1000, 5000) == 5000U);
    CHECK(grow_buffer_capacity(1000, 10) == 1000U);
}

TEST_CASE("data that fits existing capacity is streamed without reallocating") {
    const buffer_upload_plan plan = plan_buffer_upload(4096, 1024);
    CHECK(plan.action == buffer_upload_action::stream);
    CHECK(plan.capacity_bytes == 4096U);

    const buffer_upload_plan exact = plan_buffer_upload(4096, 4096);
    CHECK(exact.action == buffer_upload_action::stream);
    CHECK(exact.capacity_bytes == 4096U);
}

TEST_CASE("data that overflows reallocates with doubling") {
    const buffer_upload_plan grow = plan_buffer_upload(4096, 4097);
    CHECK(grow.action == buffer_upload_action::reallocate);
    CHECK(grow.capacity_bytes == 8192U);

    // A jump larger than double takes exactly what is required.
    const buffer_upload_plan jump = plan_buffer_upload(4096, 100000);
    CHECK(jump.action == buffer_upload_action::reallocate);
    CHECK(jump.capacity_bytes == 100000U);

    // First upload into an empty buffer.
    const buffer_upload_plan first = plan_buffer_upload(0, 64);
    CHECK(first.action == buffer_upload_action::reallocate);
    CHECK(first.capacity_bytes == 64U);
}

TEST_CASE("zero bytes uploads nothing and keeps capacity") {
    const buffer_upload_plan empty = plan_buffer_upload(4096, 0);
    CHECK(empty.action == buffer_upload_action::none);
    CHECK(empty.capacity_bytes == 4096U);

    const buffer_upload_plan empty_and_unallocated = plan_buffer_upload(0, 0);
    CHECK(empty_and_unallocated.action == buffer_upload_action::none);
    CHECK(empty_and_unallocated.capacity_bytes == 0U);
}

TEST_CASE("a stream of growing then shrinking frames reallocates only while growing") {
    std::size_t capacity = 0;
    int reallocations = 0;
    int writes = 0;

    const std::size_t frames[] = {512, 512, 900, 300, 2048, 2048, 1};
    for (const std::size_t bytes : frames) {
        const buffer_upload_plan plan = plan_buffer_upload(capacity, bytes);
        capacity = plan.capacity_bytes;
        if (plan.action == buffer_upload_action::reallocate) {
            ++reallocations;
        }
        if (plan.action != buffer_upload_action::none) {
            ++writes;
        }
        CHECK(capacity >= bytes);
    }

    CHECK(writes == 7);
    CHECK(reallocations == 3);  // 512, 900, 2048
    CHECK(capacity == 2048U);
}

TEST_CASE("buffer counters separate streaming writes from reallocations") {
    frame_profile profile;

    record_buffer_upload(&profile, 0U);   // orphan: storage only, no data
    record_buffer_write(&profile, 1024U);
    record_buffer_write(&profile, 512U);
    record_buffer_upload(&profile, 256U);  // static re-specification with data

    CHECK(profile.buffer_reallocations == 2U);
    CHECK(profile.buffer_writes == 2U);
    CHECK(profile.buffer_upload_bytes == 1792U);

    record_buffer_write(nullptr, 64U);
    record_buffer_upload(nullptr, 64U);
}
