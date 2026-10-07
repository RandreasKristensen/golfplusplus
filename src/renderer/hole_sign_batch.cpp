#include "renderer/hole_sign_batch.h"

#include "game/course_session.h"
#include "physics/vector_math.h"
#include "renderer/hole_sign_face.h"
#include "renderer/overlay_raster.h"

#include <algorithm>
#include <array>
#include <utility>

#include <glm/geometric.hpp>

namespace {
const glm::vec3 post_wood(0.24f, 0.15f, 0.08f);

// One rectangle facing `normal`: centre +/- `u` +/- `v`, counter-clockwise
// seen from the front, with uv (0, 0) at centre - u - v.
void append_rect(std::vector<hole_sign_vertex>& vertices,
                 const glm::vec3& center,
                 const glm::vec3& normal,
                 const glm::vec3& u,
                 const glm::vec3& v,
                 const glm::vec3& color,
                 const bool textured) {
    const bool front_ccw = glm::dot(glm::cross(u, v), normal) >= 0.0f;
    const std::array<glm::vec2, 4> corners{glm::vec2(0.0f, 0.0f), glm::vec2(1.0f, 0.0f), glm::vec2(1.0f, 1.0f), glm::vec2(0.0f, 1.0f)};
    const std::array<int, 6> order = front_ccw ? std::array<int, 6>{0, 1, 2, 0, 2, 3} : std::array<int, 6>{0, 2, 1, 0, 3, 2};
    for (const int corner : order) {
        const glm::vec2 uv = corners[static_cast<std::size_t>(corner)];
        hole_sign_vertex vertex;
        vertex.position = center + u * (uv.x * 2.0f - 1.0f) + v * (uv.y * 2.0f - 1.0f);
        vertex.normal = normal;
        vertex.color = color;
        vertex.uv = uv;
        vertex.textured = textured ? 1.0f : 0.0f;
        vertices.push_back(vertex);
    }
}

// A box with half extents along three perpendicular unit axes; `textured_face`
// (the +across side) carries the face texture, read left to right by someone
// looking at it.
void append_box(std::vector<hole_sign_vertex>& vertices,
                const glm::vec3& center,
                const glm::vec3& along,
                const glm::vec3& across,
                const glm::vec3& half,
                const glm::vec3& color,
                const bool textured_face,
                const bool bottom) {
    const glm::vec3 a = along * half.x;
    const glm::vec3 up = world_up * half.y;
    const glm::vec3 c = across * half.z;
    // Looking at the +across side, the viewer's right is across x up, negated.
    const glm::vec3 face_right = safe_normalize(glm::cross(-across, world_up), along) * half.x;
    append_rect(vertices, center + c, across, face_right, up, textured_face ? glm::vec3(1.0f) : color, textured_face);
    append_rect(vertices, center - c, -across, a, up, color, false);
    append_rect(vertices, center + a, along, c, up, color, false);
    append_rect(vertices, center - a, -along, c, up, color, false);
    append_rect(vertices, center + up, world_up, a, c, color, false);
    if (bottom) {
        append_rect(vertices, center - up, -world_up, a, c, color, false);
    }
}


std::vector<hole_sign_source> hole_sign_sources(const game_state& game) {
    std::vector<hole_sign_source> sources;
    for (const hole_sign& sign : game.area.signs) {
        sources.push_back(hole_sign_source{course_hole_of_area_hole(game, sign.area_hole), sign.board_center});
    }
    return sources;
}
}

bool refresh_render_hole_signs(render_hole_signs& signs,
                               const game_state& game,
                               const text_assets& text,
                               const std::uint64_t revision) {
    const std::vector<hole_sign_source> sources = hole_sign_sources(game);
    const bool same = sources.size() == signs.sources.size() &&
        std::equal(sources.begin(), sources.end(), signs.sources.begin(), [](const hole_sign_source& a, const hole_sign_source& b) {
            return a.course_hole == b.course_hole && a.board_center == b.board_center;
        });
    if (same) {
        return false;
    }
    signs = build_render_hole_signs(game, text, revision);
    return true;
}

void append_hole_sign_model(std::vector<hole_sign_vertex>& vertices, const hole_sign& sign) {
    append_box(vertices, sign.board_center, sign.down_hole, sign.face_normal,
               glm::vec3(sign.board_half_width, sign.board_half_height, sign.board_thickness * 0.5f), hole_sign_wood, true, true);
    for (const glm::vec3& foot : sign.post_feet) {
        const float height = hole_sign_top(sign) - foot.y;
        append_box(vertices, foot + world_up * (height * 0.5f), sign.down_hole, sign.face_normal,
                   glm::vec3(sign.post_radius, height * 0.5f, sign.post_radius), post_wood, false, false);
    }
}

render_hole_signs build_render_hole_signs(const game_state& game, const text_assets& text, const std::uint64_t revision) {
    render_hole_signs signs;
    signs.revision = revision;
    signs.sources = hole_sign_sources(game);
    overlay_batch batch;
    for (const hole_sign& sign : game.area.signs) {
        const std::size_t index = course_hole_of_area_hole(game, sign.area_hole);
        if (index >= game.course_holes.size()) {
            continue;
        }
        const int meters = rounded_rangefinder_meters(sign.length * game.tuning.scale.meters_per_world_unit);
        const hole_sign_labels labels =
            make_hole_sign_labels(text, static_cast<int>(index) + 1, game.course_holes[index].par, meters);
        clear_overlay_batch(batch);
        draw_hole_sign_face(batch, text, labels, game.area, sign);
        signs.truncated_text_count += batch.truncated_text_count;

        render_hole_sign drawn;
        drawn.face = rasterize_overlay_batch(batch, hole_sign_wood);
        drawn.first_vertex = signs.vertices.size();
        append_hole_sign_model(signs.vertices, sign);
        drawn.vertex_count = signs.vertices.size() - drawn.first_vertex;
        signs.signs.push_back(std::move(drawn));
    }
    return signs;
}
