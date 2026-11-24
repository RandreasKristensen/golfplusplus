#include "doctest.h"

#include "renderer/hud_boxes.h"
#include "renderer/hud_overlay.h"
#include "renderer/xp_drop_layout.h"
#include "test_support.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace {
render_xp_drop drop_at(const float progress, const std::int64_t xp = 12) {
    return render_xp_drop{skill_icon_id::generic, xp, progress};
}
}

TEST_CASE("the newest xp drop starts directly under the club label, right-aligned with it") {
    const ui_rect club = club_label_box();
    const std::vector<xp_drop_slot> slots = layout_xp_drops({drop_at(0.9f), drop_at(0.0f)});
    REQUIRE(slots.size() == 2U);
    CHECK(slots[0].drop == 1U);  // the last given is the newest
    CHECK(std::abs(rect_right(slots[0].box) - rect_right(club)) < 0.0001f);
    CHECK(rect_top(slots[0].box) < rect_bottom(club));
    CHECK(rect_bottom(club) - rect_top(slots[0].box) < 0.03f);
}

TEST_CASE("an xp drop falls and fades over its life") {
    float last_top = 10.0f;
    float last_alpha = 2.0f;
    for (const float progress : {0.0f, 0.3f, 0.6f, 0.9f, 1.0f}) {
        const std::vector<xp_drop_slot> slots = layout_xp_drops({drop_at(progress)});
        REQUIRE(slots.size() == 1U);
        CHECK(rect_top(slots[0].box) < last_top);
        CHECK(slots[0].alpha <= last_alpha);
        last_top = rect_top(slots[0].box);
        last_alpha = slots[0].alpha;
    }
    CHECK(layout_xp_drops({drop_at(0.0f)})[0].alpha == 1.0f);
    CHECK(std::abs(layout_xp_drops({drop_at(1.0f)})[0].alpha) < 0.0001f);
    CHECK(layout_xp_drops({drop_at(0.5f)})[0].alpha < 1.0f);
    CHECK(layout_xp_drops({drop_at(0.5f)})[0].alpha > 0.0f);
}

TEST_CASE("xp drops that appear together never overlap, newest on top") {
    const std::vector<render_xp_drop> drops{drop_at(0.30f), drop_at(0.28f), drop_at(0.05f), drop_at(0.0f), drop_at(0.0f), drop_at(0.0f)};
    const std::vector<xp_drop_slot> slots = layout_xp_drops(drops);
    REQUIRE(slots.size() == xp_drops_shown);
    for (std::size_t i = 1; i < slots.size(); ++i) {
        CHECK(rect_top(slots[i].box) <= rect_bottom(slots[i - 1].box) + 0.0001f);
    }
    // Clear of the club label above.
    CHECK(rect_top(slots[0].box) < rect_bottom(club_label_box()));
}

TEST_CASE("drops without xp are not shown") {
    CHECK(layout_xp_drops({drop_at(0.1f, 0)}).empty());
}

TEST_CASE("xp drops draw as text and icon with no panel, within the picture") {
    const text_assets& text = shipped_text_assets();
    overlay_batch batch;
    batch.grid = overlay_grid{480, 360};
    render_data data;
    data.xp_drops = {drop_at(0.5f, 99999), drop_at(0.1f, 5)};
    draw_hud(batch, text, data, glm::mat4(1.0f));
    CHECK(batch.truncated_text_count == 0U);
    CHECK(overlay_batch_quad_count(batch) > 0U);
    for (const overlay_vertex& vertex : batch.vertices) {
        CHECK(vertex.position.x <= 1.0f);
        CHECK(vertex.position.y >= -1.0f);
    }
}
