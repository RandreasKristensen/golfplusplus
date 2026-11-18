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
    loaded.terrain = make_ground_render_mesh(area, revision, palette.colors.ground, light);
    loaded.overlay = with_cart_paths(make_terrain_render_mesh({&area.material_overlay}, revision, palette.colors.ground, light, &area, true),
                                     area, palette.colors.ground.cart_path, light);
    const auto grass_start=std::chrono::steady_clock::now();
    loaded.grass = bake_grass_field(loaded.terrain, loaded.overlay, area.tee_boxes);
    const double grass_seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-grass_start).count();
    std::printf("  grass bake: %.3f s, %d x %d nodes\n",grass_seconds,loaded.grass.columns,loaded.grass.rows);
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
