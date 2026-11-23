#include "grass_tile_check.h"

#include "grass_motion_check.h"
#include "picture_rig.h"
#include "renderer/gl_loader.h"
#include "renderer/grass_renderer.h"
#include "renderer/grass_tile_store.h"
#include "renderer/grass_tiles.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

#include <glm/trigonometric.hpp>

namespace {
constexpr int first_unit = 10;
constexpr float eye_height = 1.7f;
constexpr float frame_seconds = 1.0f / 60.0f;
constexpr int shown_lines = 360;
// Batches blend the same blades in another order; nothing else may differ.
constexpr double batch_mean_limit = 0.003;
constexpr double batch_share_limit = 0.01;
constexpr float visible_delta = 0.02f;
// The scene differs from itself by a level or two of dither between frames drawn
// alike (peak 0.0156 measured, the sun shafts' own dither): a tile mapping error shows as far more.
constexpr float noise_floor = 0.02f;

struct tally {
    int failures = 0;
    void expect(const bool ok, const char* what) {
        if (!ok) {
            ++failures;
            std::printf("  grass tiles FAILED: %s\n", what);
        }
    }
};

class unpack_poison {
public:
    unpack_poison() {
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 17);
        glPixelStorei(GL_UNPACK_SKIP_PIXELS, 3);
        glPixelStorei(GL_UNPACK_SKIP_ROWS, 2);
        glPixelStorei(GL_UNPACK_SKIP_IMAGES, 1);
        glPixelStorei(GL_UNPACK_IMAGE_HEIGHT, 70);
    }
    // True when the store left every setting at GL's default.
    bool intact() const {
        const GLenum names[] = {GL_UNPACK_ALIGNMENT, GL_UNPACK_ROW_LENGTH, GL_UNPACK_SKIP_PIXELS,
                                GL_UNPACK_SKIP_ROWS, GL_UNPACK_SKIP_IMAGES, GL_UNPACK_IMAGE_HEIGHT};
        const GLint values[] = {4, 0, 0, 0, 0, 0};
        for (int i = 0; i < 6; ++i) {
            GLint now = -1;
            glGetIntegerv(names[i], &now);
            if (now != values[i]) { return false; }
        }
        return true;
    }
    ~unpack_poison() {
        for (const GLenum name : {GL_UNPACK_ROW_LENGTH, GL_UNPACK_SKIP_PIXELS, GL_UNPACK_SKIP_ROWS, GL_UNPACK_SKIP_IMAGES,
                                  GL_UNPACK_IMAGE_HEIGHT}) {
            glPixelStorei(name, 0);
        }
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    }
};

template <typename texel>
std::vector<texel> read_array(const unsigned int texture, const unsigned format, const unsigned type, const std::size_t count) {
    std::vector<texel> data(count);
    GLint pack = 4;
    glGetIntegerv(GL_PACK_ALIGNMENT, &pack);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, texture);
    glGetTexImage(GL_TEXTURE_2D_ARRAY, 0, format, type, data.data());
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    glPixelStorei(GL_PACK_ALIGNMENT, pack);
    return data;
}

// Every resident tile of `tiles` reads back as fill_grass_tile made it, and the
// table says exactly what the cache holds.
void verify_store(tally& result, const grass_field& field, const grass_tile_store& store, const std::vector<glm::ivec2>& tiles,
                  const char* when) {
    const std::size_t per_layer = static_cast<std::size_t>(grass_tile_nodes) * grass_tile_nodes;
    const std::size_t layers = static_cast<std::size_t>(store.capacity());
    const auto height = read_array<float>(store.height_texture(), GL_RED, GL_FLOAT, per_layer * layers);
    const auto clearance = read_array<std::uint16_t>(store.clearance_texture(), GL_RED, GL_UNSIGNED_SHORT, per_layer * layers);
    const auto kind = read_array<std::int8_t>(store.kind_texture(), GL_RED, GL_BYTE, per_layer * layers);
    grass_tile_texels want;
    bool texels_ok = true;
    bool mapped_ok = true;
    for (const glm::ivec2& tile : tiles) {
        const int layer = store.layer_of(tile);
        if (layer < 0) { mapped_ok = false; continue; }
        fill_grass_tile(field, tile, want);
        const std::size_t base = static_cast<std::size_t>(layer) * per_layer;
        for (std::size_t i = 0; i < per_layer; ++i) {
            texels_ok = texels_ok && height[base + i] == want.height[i] && clearance[base + i] == want.clearance[i] &&
                        kind[base + i] == want.kind[i];
        }
    }
    std::printf("  tile store %s: %zu tiles, %s\n", when, tiles.size(), texels_ok && mapped_ok ? "ok" : "FAILED");
    result.expect(mapped_ok, "a requested tile has no layer");
    result.expect(texels_ok, "a layer's texels differ from the field's tile (odd rows, three formats)");
    const glm::ivec4 window = store.table_window();
    std::vector<std::uint16_t> table(static_cast<std::size_t>(window.z) * window.w);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, store.table_texture());
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RED_INTEGER, GL_UNSIGNED_SHORT, table.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    bool table_ok = true;
    for (int z = 0; z < window.w; ++z) {
        for (int x = 0; x < window.z; ++x) {
            const int layer = store.layer_of(glm::ivec2(window.x + x, window.y + z));
            table_ok = table_ok && table[static_cast<std::size_t>(z) * window.z + x] == static_cast<std::uint16_t>(layer + 1);
        }
    }
    result.expect(table_ok, "the table differs from the cache: a stale or missing mapping");
}

void check_upload(tally& result, const check_course& course) {
    const grass_field& field = course.grass;
    // A node with no ground under it (height unset) must not sit beside one that grows
    // grass: a blade between them would stand at a height interpolated from nothing,
    // and the tile bounds would not hold it.
    long long ghost_cells = 0;
    grass_tile_texels texels;
    for (int tz = 0; tz < field.tile_counts.y; ++tz) {
        for (int tx = 0; tx < field.tile_counts.x; ++tx) {
            const glm::ivec2 tile = field.first_tile + glm::ivec2(tx, tz);
            const grass_tile_info* info = grass_tile_at(field, tile);
            if (info == nullptr || info->max_clearance <= 0.0f || !fill_grass_tile(field, tile, texels)) { continue; }
            for (int z = 0; z < grass_tile_cells; ++z) {
                for (int x = 0; x < grass_tile_cells; ++x) {
                    bool unset = false;
                    bool grows = false;
                    for (const int i : {0, 1, grass_tile_nodes, grass_tile_nodes + 1}) {
                        const std::size_t node = static_cast<std::size_t>(z) * grass_tile_nodes + x + i;
                        unset = unset || decode_grass_kind(texels.kind[node]) == 0.0f;
                        grows = grows || texels.clearance[node] > 0;
                    }
                    ghost_cells += unset && grows ? 1 : 0;
                }
            }
        }
    }
    std::printf("  grass field: %lld cells mix nodes without ground and with grass\n", ghost_cells);
    result.expect(ghost_cells == 0, "a cell mixes nodes without ground and with grass");
    std::vector<glm::ivec2> eligible;
    for (int z = 0; z < field.tile_counts.y; ++z) {
        for (int x = 0; x < field.tile_counts.x; ++x) {
            const glm::ivec2 tile = field.first_tile + glm::ivec2(x, z);
            const grass_tile_info* info = grass_tile_at(field, tile);
            if (info != nullptr && info->max_clearance > 0.0f) { eligible.push_back(tile); }
        }
    }
    if (eligible.size() < 4) { std::printf("  tile store: only %zu tiles with grass, not checked\n", eligible.size()); return; }
    constexpr int layers = 8;
    grass_tile_store store;
    result.expect(store.init(first_unit, layers) && store.capacity() == layers, "store init");
    result.expect(store.use(field), "store use");
    const std::size_t count = std::min<std::size_t>(layers, eligible.size());
    std::vector<glm::ivec2> first, second;
    for (std::size_t i = 0; i < count; ++i) { first.push_back(eligible[i * eligible.size() / (2 * count)]); }
    for (std::size_t i = 0; i < count; ++i) { second.push_back(eligible[(i * 2 + 1) * eligible.size() / (2 * count)]); }
    {
        const unpack_poison poison;
        store.make_resident(field, first, false);
        result.expect(poison.intact(), "the unpack state was not put back to its defaults");
        verify_store(result, field, store, first, "first set");
        store.make_resident(field, second, false);
        verify_store(result, field, store, second, "second set (evicting the first)");
        bool left_behind = false;
        for (const glm::ivec2& tile : first) {
            if (std::find(second.begin(), second.end(), tile) == second.end() && store.layer_of(tile) >= 0 &&
                store.tiles_evicted() == 0) { left_behind = true; }
        }
        result.expect(!left_behind && store.tiles_evicted() > 0, "no tile was evicted from a full cache");
        store.make_resident(field, first, true);
        verify_store(result, field, store, first, "the first again, drawn in a batch");
    }
}

struct difference {
    long long differing = 0;
    long long visible = 0;
    float peak = 0.0f;
    double mean = 0.0;
};

difference compare(const rig_capture& a, const rig_capture& b) {
    difference result;
    if (a.scene.size() != b.scene.size() || a.scene.empty()) { result.differing = 1 << 30; return result; }
    double sum = 0.0;
    for (std::size_t i = 0; i < a.scene.size(); ++i) {
        const glm::vec3 d = glm::abs(glm::vec3(a.scene[i]) - glm::vec3(b.scene[i]));
        const float most = std::max(d.x, std::max(d.y, d.z));
        sum += (d.x + d.y + d.z) / 3.0;
        result.differing += most > noise_floor ? 1 : 0;
        result.peak = std::max(result.peak, most);
        result.visible += most > visible_delta ? 1 : 0;
    }
    result.mean = sum / static_cast<double>(a.scene.size());
    return result;
}

struct sequence {
    const char* name;
    std::vector<rig_camera> cameras;
};

rig_camera pose_at(const check_course& course, const glm::vec2 at, const float yaw_degrees, const float fov, const float step) {
    rig_camera camera;
    camera.eye = anchor_on_terrain(course.game.area, glm::vec3(at.x, 0.0f, at.y)) + glm::vec3(0.0f, eye_height, 0.0f);
    const float yaw = glm::radians(yaw_degrees);
    const float pitch = glm::radians(-3.0f);
    camera.target = camera.eye + glm::vec3(std::cos(yaw) * std::cos(pitch), std::sin(pitch), std::sin(yaw) * std::cos(pitch));
    camera.fov_degrees = fov;
    camera.clock_seconds = 3.0f + step * frame_seconds;
    camera.wind = glm::vec2(0.0f);
    return camera;
}

// The rough node furthest from bare ground, and the heading with the most field to walk.
bool starting_place(const grass_field& field, glm::vec2& at, float& heading_degrees, float& run) {
    float best = 0.0f;
    for_each_grass_node(field, 7, [&](const glm::vec2& place, const glm::vec3& node) {
        if (node.y > best) {
            best = node.y;
            at = place;
        }
        return false;
    });
    if (best < 3.0f) { return false; }
    run = 0.0f;
    for (int heading = 0; heading < 8; ++heading) {
        const float angle = glm::radians(45.0f * static_cast<float>(heading));
        const glm::vec2 way(std::cos(angle), std::sin(angle));
        float length = 0.0f;
        while (length < 150.0f && glm::length(sample_grass_field(field, at + way * (length + 2.0f))) > 0.0f) { length += 2.0f; }
        if (length > run) { run = length; heading_degrees = 45.0f * static_cast<float>(heading); }
    }
    return true;
}

std::vector<sequence> sequences_of(const check_course& course, const glm::vec2 at, const float heading, const float run,
                                   const render_water& water) {
    std::vector<sequence> list;
    sequence walk{"walk", {}};
    const glm::vec2 way(std::cos(glm::radians(heading)), std::sin(glm::radians(heading)));
    const int steps = std::max(8, static_cast<int>(run / 3.0f));
    for (int i = 0; i < steps; ++i) {
        walk.cameras.push_back(pose_at(course, at + way * 3.0f * static_cast<float>(i), heading + 20.0f * std::sin(0.4f * static_cast<float>(i)), 60.0f, static_cast<float>(i)));
    }
    list.push_back(std::move(walk));
    sequence turn{"turn", {}};
    for (int i = 0; i < 36; ++i) { turn.cameras.push_back(pose_at(course, at, heading + 10.0f * static_cast<float>(i), 60.0f, static_cast<float>(i))); }
    list.push_back(std::move(turn));
    // Eyes on the corners of the 32 m tiles, and a hair either side of them.
    sequence corners{"tile corners", {}};
    const glm::vec2 corner = glm::round(at / grass_tile_meters) * grass_tile_meters;
    int index = 0;
    for (const glm::vec2 offset : {glm::vec2(0.0f), glm::vec2(0.003f, 0.0f), glm::vec2(0.0f, -0.003f), glm::vec2(-0.003f, 0.003f),
                                   glm::vec2(grass_tile_meters, 0.0f), glm::vec2(0.0f, grass_tile_meters)}) {
        for (const float yaw : {0.0f, 90.0f, 180.0f, 270.0f}) {
            corners.cameras.push_back(pose_at(course, corner + offset, yaw + 7.0f, 60.0f, static_cast<float>(index++)));
        }
    }
    list.push_back(std::move(corners));
    sequence zoom{"zoom", {}};
    index = 0;
    for (const float fov : {5.0f, 12.0f, 28.0f, 100.0f}) {
        for (const float yaw : {heading, heading + 90.0f, heading + 180.0f}) {
            zoom.cameras.push_back(pose_at(course, at, yaw, fov, static_cast<float>(index++)));
        }
    }
    list.push_back(std::move(zoom));
    if (!water.triangles.empty()) {
        sequence pond{"pond mirror", {}};
        const glm::vec3 point = water.triangles.front().position;
        for (int i = 0; i < 24; ++i) {
            rig_camera camera = pose_at(course, glm::vec2(point.x, point.z - 15.0f), 90.0f + 8.0f * static_cast<float>(i - 12), 60.0f, static_cast<float>(i));
            camera.ponds = true;
            pond.cameras.push_back(camera);
        }
        list.push_back(std::move(pond));
    }
    return list;
}
}

grass_tile_check_result check_grass_tiles(const check_course& course, const game_content& content, const lighting_palette& palette,
                                          const std::string& asset_root, SDL_Window* window, const std::filesystem::path& out,
                                          const bool with_motion) {
    grass_tile_check_result result;
    tally failed;
    check_upload(failed, course);
    glm::vec2 at(0.0f);
    float heading = 0.0f;
    float run = 0.0f;
    if (!starting_place(course.grass, at, heading, run)) {
        std::printf("  grass tiles: no rough 3 m from bare ground, transitions not checked\n");
        result.ran = true;
        result.failures = failed.failures;
        return result;
    }
    const int factor = std::max(1, (1080 + shown_lines / 2) / shown_lines);
    picture_rig whole;
    picture_rig evicting;
    picture_rig batched;
    if (!whole.init(window, course, content, palette, asset_root, shown_lines * factor, shown_lines) ||
        !evicting.init(window, course, content, palette, asset_root, shown_lines * factor, shown_lines) ||
        !batched.init(window, course, content, palette, asset_root, shown_lines * factor, shown_lines)) {
        std::printf("  grass tiles: cannot set up the renderers\n");
        failed.failures++;
        result.failures = failed.failures;
        return result;
    }
    result.ran = true;
    const std::vector<sequence> sequences = sequences_of(course, at, heading, run, whole.water());
    std::printf("  grass tiles: %zu sequences from (%.1f, %.1f), %.0f m of walk, %d GPU layers (%.2f MiB)\n", sequences.size(), at.x, at.y, run,
                whole.drawn_by().grass_tile_capacity(), static_cast<double>(whole.drawn_by().grass_gpu_bytes()) / 1048576.0);
    // One pass with every tile resident: uploads are counted, and the greatest need found.
    // Tiles a draw had to upload itself are counted apart from those fetched ahead of the eye:
    // the first are the views' misses, the others bounded work a frame.
    const auto misses = [&] { return whole.drawn_by().grass().tiles().tiles_uploaded() - whole.drawn_by().grass().tiles().tiles_fetched_ahead(); };
    std::size_t most_wanted = 1;
    std::uint64_t most_uploaded_in_a_frame = 0;
    std::uint64_t most_ahead_in_a_frame = 0;
    std::uint64_t most_ahead_in_a_steady_frame = 0;
    for (const sequence& each : sequences) {
        std::uint64_t before = misses();
        const std::uint64_t start = before;
        for (const rig_camera& camera : each.cameras) {
            const std::uint64_t ahead_before = whole.drawn_by().grass().tiles().tiles_fetched_ahead();
            const std::uint64_t warm_before = whole.drawn_by().grass().warm_frames();
            whole.capture(camera);
            const std::uint64_t now = misses();
            most_uploaded_in_a_frame = std::max(most_uploaded_in_a_frame, now - before);
            before = now;
            const std::uint64_t ahead = whole.drawn_by().grass().tiles().tiles_fetched_ahead() - ahead_before;
            most_ahead_in_a_frame = std::max(most_ahead_in_a_frame, ahead);
            if (whole.drawn_by().grass().warm_frames() == warm_before) { most_ahead_in_a_steady_frame = std::max(most_ahead_in_a_steady_frame, ahead); }
            most_wanted = std::max(most_wanted, whole.drawn_by().grass().last_tiles_wanted());
        }
        std::printf("    %-12s %3zu frames: %llu tiles uploaded\n", each.name, each.cameras.size(),
                    static_cast<unsigned long long>(before - start));
    }
    const std::uint64_t resident = whole.drawn_by().grass().tiles().tiles_uploaded();
    std::uint64_t holding_grass = 0;
    for (const grass_tile_info& info : course.grass.tiles) { holding_grass += info.max_clearance > 0.0f ? 1 : 0; }
    failed.expect(resident <= holding_grass, "a tile was uploaded twice, or one that cannot grow grass");
    std::printf("  grass tiles: %llu tiles sent in all, at most %llu missed by a draw in one frame, at most %zu wanted by one view\n",
                static_cast<unsigned long long>(resident), static_cast<unsigned long long>(most_uploaded_in_a_frame), most_wanted);
    std::printf("  grass tiles: ahead of the eye at most %llu a frame (%llu when nothing jumped)\n",
                static_cast<unsigned long long>(most_ahead_in_a_frame), static_cast<unsigned long long>(most_ahead_in_a_steady_frame));
    failed.expect(most_ahead_in_a_steady_frame <= static_cast<std::uint64_t>(grass_prefetch_tiles), "fetching ahead exceeded its per-frame budget");
    failed.expect(most_ahead_in_a_frame <= static_cast<std::uint64_t>(whole.drawn_by().grass_tile_capacity() / 2), "fetching ahead exceeded half the cache");
    // A frame draws the main view and at most a mirror: never the whole course.
    failed.expect(most_uploaded_in_a_frame <= 2 * most_wanted, "a frame uploaded more tiles than its views need");
    // The renderer lags a frame behind a moved eye in places (the sun's shadow focus),
    // so each pose is drawn twice and the second compared.
    // A cache that holds one view and no more, and one a third as big.
    failed.expect(evicting.limit_grass_tiles(static_cast<int>(most_wanted)), "limit the evicting cache");
    const int small = std::max(1, static_cast<int>(most_wanted / 3));
    failed.expect(batched.limit_grass_tiles(small), "limit the batching cache");
    int max_batches = 1;
    for (const sequence& each : sequences) {
        difference worst_evict;
        difference worst_batch;
        std::size_t pixels = 1;
        double whole_ms = 0.0, evicting_ms = 0.0, batched_ms = 0.0;
        int frames_batched = 0;
        const std::uint64_t again = misses();
        for (const rig_camera& camera : each.cameras) {
            whole.capture(camera);
            const auto timed = [](picture_rig& rig, const rig_camera& pose, double& total) {
                const auto began = std::chrono::steady_clock::now();
                rig_capture picture = rig.capture(pose);
                total += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - began).count();
                return picture;
            };
            const rig_capture reference = timed(whole, camera, whole_ms);
            pixels = reference.scene.size();
            evicting.capture(camera);
            const rig_capture one = timed(evicting, camera, evicting_ms);
            batched.capture(camera);
            const rig_capture two = timed(batched, camera, batched_ms);
            const difference evict = compare(reference, one);
            const difference batch = compare(reference, two);
            worst_evict.differing = std::max(worst_evict.differing, evict.differing);
            worst_evict.peak = std::max(worst_evict.peak, evict.peak);
            worst_batch.differing = std::max(worst_batch.differing, batch.differing);
            worst_batch.visible = std::max(worst_batch.visible, batch.visible);
            worst_batch.mean = std::max(worst_batch.mean, batch.mean);
            const int batches = batched.drawn_by().grass().last_batches();
            frames_batched += batches > 1 ? 1 : 0;
            max_batches = std::max(max_batches, batches);
            failed.expect(evicting.drawn_by().grass().last_batches() <= 1, "the cache sized to one view needed batches");
        }
        const double share = static_cast<double>(worst_batch.visible) / static_cast<double>(pixels);
        std::printf("    %-12s evicting: %lld pixels over the noise (peak %.4f); batched (%d frames in several batches): mean %.5f, %.3f%% visibly different\n",
                    each.name, worst_evict.differing, static_cast<double>(worst_evict.peak), frames_batched, worst_batch.mean, share * 100.0);
        const double frames = static_cast<double>(each.cameras.size());
        std::printf("    %-12s whole frame ms (draw, readback and all): resident %.2f, evicting %.2f, batched %.2f\n", each.name,
                    whole_ms / frames, evicting_ms / frames, batched_ms / frames);
        failed.expect(worst_evict.differing == 0, "eviction and reload changed the picture");
        failed.expect(worst_batch.mean <= batch_mean_limit && share <= batch_share_limit, "the batched draw differs beyond blend order");
        failed.expect(misses() == again, "a warm pass had a draw upload tiles");
    }
    std::printf("  grass tiles: evicted %llu and reloaded across the sequences; batches up to %d\n",
                static_cast<unsigned long long>(evicting.drawn_by().grass().tiles().tiles_evicted()), max_batches);
    failed.expect(evicting.drawn_by().grass().tiles().tiles_evicted() > 0 || resident <= most_wanted, "the evicting cache never evicted");
    whole.shutdown();
    evicting.shutdown();
    batched.shutdown();
    if (with_motion) {
        // The widest tuning lighting_fields.h accepts (100 m reach, four rings: 800 m): far more
        // tiles than fit, drawn in batches. A stress result, not a shipped setting.
        lighting_palette stress = palette;
        stress.grass.reach_meters = 100.0f;
        stress.grass.rings = 4.0f;
        picture_rig wide;
        if (wide.init(window, course, content, stress, asset_root, shown_lines * factor, shown_lines)) {
            const rig_camera pose = pose_at(course, at, heading, 60.0f, 0.0f);
            wide.capture(pose);
            const auto began = std::chrono::steady_clock::now();
            const rig_capture picture = wide.capture(pose);
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - began).count();
            failed.expect(!picture.scene.empty(), "the widest tuning drew nothing");
            std::printf("  grass tiles: widest tuning, %zu tiles wanted by one view, %d batches, %.1f ms a frame (capture), %llu tiles sent\n",
                        wide.drawn_by().grass().last_tiles_wanted(), wide.drawn_by().grass().last_batches(), ms,
                        static_cast<unsigned long long>(wide.drawn_by().grass().tiles().tiles_uploaded()));
            failed.expect(wide.drawn_by().grass().last_tiles_wanted() <= static_cast<std::size_t>(wide.drawn_by().grass_tile_capacity()) ||
                              wide.drawn_by().grass().last_batches() > 1,
                          "more tiles than fit were not drawn in batches");
            failed.expect(wide.drawn_by().grass_gpu_bytes() <= (8ull << 20), "the tile cache grew with the tuning");
            wide.shutdown();
        }
        // The same walks and turns over the rough, every frame drawn in batches of a few tiles.
        constexpr int batch_layers = 6;
        const grass_motion_result unlimited = run_grass_motion(course, content, palette, asset_root, window, {shown_lines}, out);
        const grass_motion_result batches = run_grass_motion(course, content, palette, asset_root, window, {shown_lines}, out,
                                                             std::nullopt, batch_layers);
        failed.expect(unlimited.ran && batches.ran && grass_motion_passes(batches), "the rough's motion check fails in batches");
        failed.expect(unlimited.measures.size() == batches.measures.size(), "the batched motion check ran other sequences");
        for (std::size_t i = 0; i < std::min(unlimited.measures.size(), batches.measures.size()); ++i) {
            const motion_measure& a = unlimited.measures[i];
            const motion_measure& b = batches.measures[i];
            const auto close = [](const double x, const double y) { return std::abs(x - y) <= 0.03 * std::abs(x) + 2.0e-4; };
            failed.expect(close(a.alias, b.alias) && close(a.scene_change, b.scene_change) && close(a.mean_luma, b.mean_luma),
                          "a motion measure changed when the tiles were drawn in batches");
        }
        std::printf("  grass tiles: rough motion with %d tile layers (every frame in batches): %zu sequences compared\n", batch_layers,
                    batches.measures.size());
    }
    result.failures = failed.failures;
    return result;
}
