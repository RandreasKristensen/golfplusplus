// Repeatable photographs of a prop through the game's loader, lit prop
// renderer, board text and complete recording pipeline: the four inspection
// views, its rear and a close look at its board. The Python driver writes PNGs.
#include "core/window.h"
#include "core/screenshot.h"
#include "core/sky_frame.h"
#include "game/content_files.h"
#include "game/game_content.h"
#include "game/prop_loader.h"
#include "game/model_kinds.h"
#include "game/text_ids.h"
#include "renderer/hole_sign_batch.h"
#include "renderer/renderer.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <glm/geometric.hpp>

int main(const int argc, char** argv) {
    if (argc != 3 && argc != 4) {
        std::fprintf(stderr, "usage: golfpp-preview-prop PROP.json OUTPUT_DIRECTORY [BOARD_LABEL]\n"); return 2;
    }
    const std::string root = GOLFPP_ASSETS_DIR;
    const auto source = read_text_file(argv[1]);
    const auto model = source ? parse_prop_from_text(*source) : std::nullopt;
    auto props = load_props(root);
    const auto content = load_game_content(root);
    const auto text = load_text_assets(root);
    const auto picture = load_picture_style(root);
    const auto light = load_lighting(root);
    if (!model || !props.props || !content.content || !text || !picture || !light) {
        std::fprintf(stderr, "cannot load prop or game content: %s %s\n", props.error.c_str(), content.error.c_str()); return 1;
    }
    const std::string id = std::filesystem::path(argv[1]).stem().string();
    (*props.props)[id] = *model;
    game_state game = make_game_state(*content.content, save_data{});
    game.place = content.content->hub;
    game.world.latitude_deg = 56; game.world.longitude_deg = 10;
    sky_controls sky; sky.pinned_hour = 14;
    window display;
    if (!display.init("golf++ prop preview", 1280, 960, false, root + "/interface/icons/golfpp-icon.bmp", true)) { return 1; }
    renderer draw;
    if (!draw.init(display.sdl_window(), root, *picture, *light, *props.props)) { display.shutdown(); return 1; }
    play_area area;
    area.props = {{id, glm::vec3(0)}};
    // The book shows its title; the bus the longest shipped destination.
    std::string label = argc == 4 ? argv[3] : model->kind == "hub_book" ? lookup_text(*text, text_hub_book_title) : "";
    for (const auto& course : content.content->courses) {
        if (argc != 4 && model->kind == "hub_bus" && course.name.size() > label.size()) { label = course.name; }
    }
    const render_hole_signs boards = build_render_hole_signs(area, {}, prop_boards(area, *props.props, {label}), *text,
        light->colors, props.props->at(hole_sign_model), 1);
    const float front = model->kind == "hub_book" ? -1.0f : 1.0f;
    const glm::vec3 board = model->points.count("board_center") ? model->point("board_center") : glm::vec3(0, 1.5f, 0);
    const std::array<glm::vec3, 6> eyes{{{-12,1.7f,0}, {0,1.7f,12*front},
        {-8.485281f,1.7f,8.485281f*front}, {-17.67767f,1.7f,17.67767f*front}, {0,1.7f,-12*front},
        {0, std::max(1.7f, board.y), board.z + 3*front}}};
    const std::array<const char*,6> names{{"side", "front", "three_quarter", "tape_25m", "rear", "board"}};
    bool ok = true;
    for (std::size_t view = 0; view < eyes.size(); ++view) {
        render_data frame;
        frame.world_props = &area.props; frame.trees_revision = 1; frame.hole_signs = &boards;
        frame.camera_position = eyes[view];
        frame.camera_target = view == 5 ? board : glm::vec3(0, model->kind == "hub_book" ? 1.0f : 1.5f, 0);
        frame.camera_fov_degrees = 50; frame.viewmodel_fov_degrees = 50;
        frame.area_extent = 80; frame.area_center = {0,2,0}; // backdrop ground at y=0
        frame.lighting = make_render_lighting(game, *light, sky, 0);
        frame.glow = light->colors.glow; frame.show_hud = false;
        // Allow the actual iris and white balance to settle for each viewpoint.
        for (int tick = 0; tick < 90; ++tick) {
            frame.clock_seconds = static_cast<float>(view * 90 + static_cast<std::size_t>(tick)) / 60;
            draw.render(frame, *text);
        }
        const auto path = std::filesystem::path(argv[2]) / (std::string(names[view]) + ".bmp");
        ok = save_screenshot(display.sdl_window(), path) && ok;
    }
    draw.shutdown(); display.shutdown();
    return ok ? 0 : 1;
}
