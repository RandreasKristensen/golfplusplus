#include "renderer/grass_renderer.h"
#include "renderer/gl_loader.h"
#include "renderer/grass_tiles.h"
#include "renderer/shadow_map_pass.h"
#include <algorithm>
#include <cmath>
#include <vector>
#include <glm/geometric.hpp>

namespace {
constexpr int blades_per_tuft = 5;
constexpr int blocks_per_draw = 128;
constexpr float blade_top_share = 1.5f;

struct ring_blocks {
    int level = 0;
    float reach = 0.0f;
    grass_grid grid;
    grass_block_selection blocks;
};

bool tile_less(const glm::ivec2& a, const glm::ivec2& b) { return a.y != b.y ? a.y < b.y : a.x < b.x; }

// The tiles a block's tufts may stand in that could grow a blade.
template <typename visit>
void for_each_grown_tile(const grass_field& field, const glm::ivec4& range, const float required, visit&& action) {
    for (int z = range.y; z <= range.w; ++z) {
        for (int x = range.x; x <= range.z; ++x) {
            const grass_tile_info* info = grass_tile_at(field, glm::ivec2(x, z));
            if (info != nullptr && info->max_clearance >= required) { action(glm::ivec2(x, z)); }
        }
    }
}
}

bool grass_renderer::init(const std::string& blade_vertex, const std::string& blade_fragment, const std::string& defines) {
    shutdown();
    if (!blade_shader_.load_from_files(blade_vertex, blade_fragment, defines)) { return false; }
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    if (!vao_ || !vbo_ || !tiles_.init(field_unit)) { shutdown(); return false; }
    const auto tuft = grass_tuft_vertices(blades_per_tuft);
    vertex_count_ = static_cast<int>(tuft.size());
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    storage_.buffer(vbo_, GL_ARRAY_BUFFER, tuft.size() * sizeof(glm::vec3), tuft.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), nullptr);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    return true;
}

void grass_renderer::shutdown() {
    tiles_.shutdown();
    storage_.delete_buffer(vbo_);
    if (vao_) { glDeleteVertexArrays(1, &vao_); vao_ = 0; }
    blade_shader_.shutdown();
    vertex_count_ = 0;
}

void grass_renderer::fetch_ahead(const grass_field* field, const glm::vec3& eye, const grass_style& style) {
    if (field == nullptr || field->tiles.empty() || vao_ == 0 || !tiles_.use(*field)) { return; }
    const bool new_field = tiles_.fields_used() != fields_seen_;
    const bool jumped = !have_eye_ || glm::length(glm::vec2(eye.x - last_eye_.x, eye.z - last_eye_.z)) > grass_tile_meters;
    fields_seen_ = tiles_.fields_used();
    last_eye_ = eye;
    have_eye_ = true;
    // Half the cache at most: what a view needs must always find room besides.
    const std::vector<glm::ivec2> ahead = grass_tiles_ahead(*field, eye, style, static_cast<std::size_t>(tiles_.capacity() / 2));
    const bool warm = new_field || jumped;
    warm_frames_ += warm ? 1 : 0;
    tiles_.fill_ahead(*field, ahead, warm ? static_cast<int>(ahead.size()) : grass_prefetch_tiles);
}

void grass_renderer::draw(const grass_frame& frame, const grass_style& style,
                          const shadow_map_pass& shadows, const int shadow_unit, gl_timer_pool& timers,
                          frame_profile* profile) {
    last_batches_ = 0;
    last_wanted_ = 0;
    if (frame.field == nullptr || frame.field->tiles.empty() || vao_ == 0 || style.blade_height_meters <= 0.0f) {
        return;
    }
    const grass_field& field = *frame.field;
    if (!tiles_.use(field)) { return; }
    timers.begin(gpu_profile_stage::grass);
    const int ring_count = std::clamp(static_cast<int>(std::lround(style.rings)), 1, grass_max_rings);
    view_frustum frustum = make_view_frustum(frame.view_proj);
    frustum.hidden = frame.hidden;
    const float top = blade_top_share * style.blade_height_meters * std::max(1.0f, style.heavy_blade_share);
    // A tile whose clearance is under what the widest blade needs grows nothing.
    const float required = grass_blade_reach(style, std::ldexp(1.0f, ring_count - 1)) + grass_field_error;
    std::vector<ring_blocks> rings;
    std::vector<glm::ivec2> wanted;
    for (int k = 0; k < ring_count; ++k) {
        const float scale = std::ldexp(1.0f, k);
        ring_blocks ring;
        ring.level = k;
        ring.reach = style.reach_meters * scale;
        ring.grid = grass_grid_around(frame.eye, ring.reach, style.tuft_spacing_meters * scale);
        ring.blocks = grass_visible_blocks(ring.grid, frustum, field, top, required, profile ? &scratch_bound_ : nullptr);
        for (const glm::ivec4& range : ring.blocks.tiles) {
            for_each_grown_tile(field, range, required, [&](const glm::ivec2& tile) { wanted.push_back(tile); });
        }
        rings.push_back(std::move(ring));
    }
    std::sort(wanted.begin(), wanted.end(), tile_less);
    wanted.erase(std::unique(wanted.begin(), wanted.end()), wanted.end());
    if (profile) {
        scratch_bound_ = std::max(scratch_bound_, 2 * wanted.capacity() * sizeof(glm::ivec2));
    }
    last_wanted_ = wanted.size();
    if (wanted.empty()) { timers.end(); return; }

    const float pixel_size = 2.0f / (glm::length(glm::vec3(frame.view_proj[0][1], frame.view_proj[1][1], frame.view_proj[2][1])) *
                                     static_cast<float>(std::max(1, frame.target_height)));
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    blade_shader_.set_profile(profile);
    blade_shader_.use();
    blade_shader_.set_mat4("u_view_proj", frame.view_proj);
    blade_shader_.set_vec2("u_field_grid", glm::vec2(grass_field_spacing, grass_field_error));
    blade_shader_.set_int("u_last_ring", ring_count - 1);
    blade_shader_.set_vec4("u_blade", glm::vec4(style.blade_height_meters, style.blade_width_meters, 0.0f, style.blade_sway));
    blade_shader_.set_float("u_heavy_share", style.heavy_blade_share);
    blade_shader_.set_float("u_pixel_size", pixel_size);
    blade_shader_.set_float("u_min_pixels", style.blade_min_pixels);
    blade_shader_.set_vec3("u_rough_color", frame.rough_color);
    blade_shader_.set_vec2("u_shades", glm::vec2(style.base_shade, style.tip_shade));
    blade_shader_.set_vec2("u_wind", frame.wind);
    blade_shader_.set_float("u_time", frame.clock_seconds);
    blade_shader_.set_int("u_height", field_unit);
    blade_shader_.set_int("u_clearance", field_unit + 1);
    blade_shader_.set_int("u_kind", field_unit + 2);
    blade_shader_.set_int("u_tiles", field_unit + 3);
    shadows.bind(blade_shader_, shadow_unit);
    glBindVertexArray(vao_);

    // One batch unless the tuning asks for more tiles than the cache holds;
    // then each tuft is drawn in the batch holding its tile, and only there.
    const std::size_t capacity = static_cast<std::size_t>(tiles_.capacity());
    const bool batched = wanted.size() > capacity;
    std::vector<glm::vec2> corners;
    for (std::size_t first_tile = 0; first_tile < wanted.size(); first_tile += capacity) {
        const std::vector<glm::ivec2> batch(wanted.begin() + static_cast<std::ptrdiff_t>(first_tile),
                                            wanted.begin() + static_cast<std::ptrdiff_t>(std::min(wanted.size(), first_tile + capacity)));
        tiles_.make_resident(field, batch, batched);
        blade_shader_.set_vec4("u_tile_window", glm::vec4(tiles_.table_window()));
        ++last_batches_;
        for (const ring_blocks& ring : rings) {
            corners.clear();
            for (std::size_t i = 0; i < ring.blocks.corners.size(); ++i) {
                bool inside = !batched;
                if (batched) {
                    for_each_grown_tile(field, ring.blocks.tiles[i], required, [&](const glm::ivec2& tile) {
                        inside = inside || std::binary_search(batch.begin(), batch.end(), tile, tile_less);
                    });
                }
                if (inside) { corners.push_back(ring.blocks.corners[i]); }
            }
            if (corners.empty()) { continue; }
            blade_shader_.set_vec2("u_grid", glm::vec2(ring.grid.spacing, static_cast<float>(ring.level)));
            blade_shader_.set_vec2("u_density", glm::vec2(style.reach_meters, ring.reach));
            for (std::size_t first = 0; first < corners.size(); first += blocks_per_draw) {
                const int count = static_cast<int>(std::min<std::size_t>(blocks_per_draw, corners.size() - first));
                std::vector<glm::vec4> pairs;
                for (int i = 0; i < count; i += 2) {
                    pairs.emplace_back(corners[first + i], i + 1 < count ? corners[first + i + 1] : glm::vec2(0.0f));
                }
                blade_shader_.set_vec4_array("u_blocks", pairs.data(), static_cast<int>(pairs.size()));
                const int instances = count * grass_block_tufts * grass_block_tufts;
                glDrawArraysInstanced(GL_TRIANGLES, 0, vertex_count_, instances);
                if (profile) { profile->work.current().grass_instances += instances; }
                record_draw_call(profile);
            }
        }
    }
    glBindVertexArray(0);
    glDisable(GL_BLEND);
    timers.end();
}
