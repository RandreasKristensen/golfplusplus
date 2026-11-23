#include "check_support.h"

#include "game/course_session.h"
#include "renderer/cart_path_mesh.h"
#include "renderer/gl_loader.h"
#include "renderer/sky_visibility.h"
#include "renderer/terrain_render_mesh.h"

#include <fstream>
#include <chrono>
#include <cstdio>


check_course load_check_course(const game_content& content, const course_definition& course, const lighting_palette& palette) {
    check_course loaded;
    loaded.game = make_game_state(content, save_data{});
    start_course(loaded.game, course);
    const play_area& area = loaded.game.area;
    const ground_light_bake light = bake_ground_light(area.ground);
    const std::uint64_t revision = loaded.game.terrain_render_revision;
    const auto ground_start = std::chrono::steady_clock::now();
    loaded.terrain = make_ground_render_mesh(area, revision, palette.colors.ground, light, palette.terrain_lod);
    const double ground_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - ground_start).count();
    for (std::size_t level = 0; level < loaded.terrain.lod_error_meters.size(); ++level) {
        std::uint64_t kept = 0;
        for (const render_mesh_chunk& chunk : loaded.terrain.chunks) {
            kept += chunk.coarse[level].count > 0U ? chunk.coarse[level].count : chunk.index_count;
        }
        std::printf("  level %zu (%.2f m): %.1f%% of the triangles\n", level + 1U, static_cast<double>(loaded.terrain.lod_error_meters[level]),
                    100.0 * static_cast<double>(kept) / static_cast<double>(loaded.terrain.indices.size()));
    }
    std::printf("  ground: %.3f s, %zu triangles, coarse levels %.2f MiB beside %.2f MiB of indices (%zu levels)\n", ground_seconds,
                loaded.terrain.indices.size() / 3U, static_cast<double>(loaded.terrain.lod_indices.size() * 4U) / 1048576.0,
                static_cast<double>(loaded.terrain.indices.size() * 4U) / 1048576.0, loaded.terrain.lod_error_meters.size());
    loaded.overlay = with_cart_paths(make_terrain_render_mesh({&area.material_overlay}, revision, palette.colors.ground, light, &area),
                                     area, palette.colors.ground.cart_path, light);
    const auto grass_start=std::chrono::steady_clock::now();
    loaded.grass = bake_grass_field(loaded.terrain, loaded.overlay, area.tee_boxes, std::filesystem::temp_directory_path() / "golfpp_picture_check_grass");
    const double grass_seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-grass_start).count();
    std::size_t kept=0;
    for(const grass_tile_info& tile:loaded.grass.tiles) {kept+=tile.max_clearance>0.0f ? 1:0;}
    std::printf("  grass bake: %.3f s, %d x %d nodes, %zu of %zu tiles kept, %.2f MiB packed, %.2f MiB bake indices\n",grass_seconds,
                loaded.grass.columns,loaded.grass.rows,kept,loaded.grass.tiles.size(),
                static_cast<double>(grass_field_memory_bytes(loaded.grass))/1048576.0,
                static_cast<double>(loaded.grass.bake_index_bytes)/1048576.0);
    return loaded;
}

std::string check_shader_path(const std::string& asset_root, const char* name) {
    return (std::filesystem::path(asset_root) / "picture" / "shaders" / name).string();
}

std::vector<glm::vec4> read_colors(const framebuffer& target, const int width, const int height) {
    std::vector<glm::vec4> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    target.bind();
    glReadPixels(0, 0, width, height, GL_RGBA, GL_FLOAT, pixels.data());
    return pixels;
}

bool write_check_bmp(const std::filesystem::path& path, const int width, const int height,
                     const std::vector<std::array<std::uint8_t, 3>>& pixels) {
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    const int row = (width * 3 + 3) / 4 * 4;
    const std::uint32_t size = 54U + static_cast<std::uint32_t>(row * height);
    const auto put = [&file](const std::uint32_t value, const int bytes) {
        for (int i = 0; i < bytes; ++i) {
            file.put(static_cast<char>((value >> (8 * i)) & 0xffU));
        }
    };
    file.put('B');
    file.put('M');
    put(size, 4);
    put(0, 4);
    put(54, 4);
    put(40, 4);
    put(static_cast<std::uint32_t>(width), 4);
    put(static_cast<std::uint32_t>(height), 4);
    put(1, 2);
    put(24, 2);
    put(0, 4);
    put(static_cast<std::uint32_t>(row * height), 4);
    put(2835, 4);
    put(2835, 4);
    put(0, 4);
    put(0, 4);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::array<std::uint8_t, 3>& rgb = pixels[static_cast<std::size_t>(y * width + x)];
            file.put(static_cast<char>(rgb[2]));
            file.put(static_cast<char>(rgb[1]));
            file.put(static_cast<char>(rgb[0]));
        }
        for (int pad = width * 3; pad < row; ++pad) {
            file.put(0);
        }
    }
    return static_cast<bool>(file);
}
