#include "core/app.h"

#include "core/event_loop.h"
#include "game/asset_resolver.h"
#include "game/course_loader.h"
#include "game/game_content.h"
#include "game/hole_data.h"
#include "game/hole_loader.h"
#include "game/progression.h"
#include "game/save_manager.h"
#include "game/scorecard.h"
#include "game/shop.h"
#include "physics/terrain.h"

#include <SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

namespace {
constexpr float pi = 3.14159265358979323846f;

glm::vec3 aim_direction(const float aim_angle) {
    return glm::normalize(glm::vec3(std::sin(aim_angle), 0.0f, std::cos(aim_angle)));
}

float radians(const float degrees) {
    return degrees * pi / 180.0f;
}

float terrain_height_at(const game_tuning& tuning, const glm::vec3& position) {
    return sample_terrain_mesh(tuning.terrain_mesh_data, position, tuning.ground_y).point.y;
}

glm::vec3 terrain_anchor_at(const game_tuning& tuning, const glm::vec3& anchor) {
    return terrain_anchor_position(tuning, anchor);
}

glm::vec3 render_color_for_terrain_material(const terrain_material material, const float distance_from_center, const float width) {
    const float half_width = std::max(0.001f, width * 0.5f);
    const float edge_amount = std::max(0.0f, std::min(1.0f, std::abs(distance_from_center) / half_width));

    switch (material) {
    case terrain_material::fairway:
        return glm::vec3(0.17f + edge_amount * 0.04f,
                         0.44f - edge_amount * 0.08f,
                         0.17f + edge_amount * 0.02f);
    case terrain_material::rough:
        return glm::vec3(0.12f, 0.27f, 0.12f);
    case terrain_material::green:
        return glm::vec3(0.20f, 0.68f, 0.28f);
    case terrain_material::bunker:
        return glm::vec3(0.66f, 0.55f, 0.22f);
    case terrain_material::water:
        return glm::vec3(0.10f, 0.22f, 0.60f);
    default:
        return glm::vec3(0.16f, 0.38f, 0.16f);
    }
}

std::vector<render_terrain_vertex> make_render_terrain_vertices(const terrain_mesh& mesh) {
    std::vector<render_terrain_vertex> vertices;
    vertices.reserve(mesh.vertices.size());

    for (const terrain_vertex& vertex : mesh.vertices) {
        render_terrain_vertex render_vertex;
        render_vertex.position = vertex.position;
        render_vertex.normal = vertex.normal;
        render_vertex.color = render_color_for_terrain_material(vertex.material, vertex.distance_from_center, mesh.width);
        vertices.push_back(render_vertex);
    }

    return vertices;
}

void set_material_overlay_render_mesh(render_data& data, const game_tuning& tuning) {
    constexpr float overlay_lift = 0.045f;
    const terrain_mesh overlay_mesh = build_material_overlay_mesh(tuning.terrain_mesh_data,
                                                                  tuning.course.material_zones,
                                                                  overlay_lift);
    data.material_overlay_vertices = make_render_terrain_vertices(overlay_mesh);
    data.material_overlay_indices = overlay_mesh.indices;
}

void append_render_terrain_mesh(std::vector<render_terrain_vertex>& vertices,
                                std::vector<std::uint32_t>& indices,
                                const terrain_mesh& mesh) {
    if (mesh.vertices.empty() || mesh.indices.empty()) {
        return;
    }

    const std::uint32_t index_offset = static_cast<std::uint32_t>(vertices.size());
    const std::vector<render_terrain_vertex> appended_vertices = make_render_terrain_vertices(mesh);
    vertices.insert(vertices.end(), appended_vertices.begin(), appended_vertices.end());
    indices.reserve(indices.size() + mesh.indices.size());
    for (const std::uint32_t index : mesh.indices) {
        indices.push_back(index_offset + index);
    }
}

std::vector<render_tree> make_render_trees(const game_tuning& tuning) {
    std::vector<render_tree> trees;
    trees.reserve(tuning.course.trees.size());
    for (const tree_instance& tree : tuning.course.trees) {
        render_tree render;
        render.base = tree_base_position(tuning, tree);
        render.trunk_radius = tree.trunk_radius;
        render.trunk_height = tree.trunk_height;
        render.leaf_radius = tree.leaf_radius;
        render.leaf_height = tree.leaf_height;
        trees.push_back(render);
    }
    return trees;
}


void set_walking_camera(render_data& data, const game_state& game) {
    const glm::vec3 forward = aim_direction(game.player.yaw);
    data.camera_position = game.player.position + game.tuning.camera.walking_eye_offset;
    data.camera_target = data.camera_position + forward * game.tuning.camera.walking_target_distance;
}

void set_cart_camera(render_data& data, const game_state& game) {
    const glm::vec3 forward = aim_direction(game.cart.yaw);
    const glm::vec3 up(0.0f, 1.0f, 0.0f);
    const glm::vec3 right = glm::normalize(glm::cross(forward, up));
    const glm::vec3 offset = right * game.tuning.camera.cart_eye_offset.x +
        up * game.tuning.camera.cart_eye_offset.y;

    data.camera_position = game.player.position + offset;
    data.camera_target = data.camera_position + forward * game.tuning.camera.cart_target_distance;
    data.camera_fov_degrees = game.tuning.camera.cart_fov_degrees;
}

void set_aiming_camera(render_data& data, const game_state& game) {
    const glm::vec3 forward = aim_direction(game.aim_angle);
    data.camera_position = game.ball.position - forward * 2.0f + glm::vec3(0.0f, 1.55f, 0.0f);
    data.camera_target = game.ball.position + forward * 12.0f + glm::vec3(0.0f, 1.05f, 0.0f);
}

void set_address_camera(render_data& data, const game_state& game) {
    const glm::vec3 forward = aim_direction(game.aim_angle);
    const glm::vec3 left = glm::normalize(glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), forward));
    data.camera_position = game.ball.position + left * 2.4f - forward * 0.7f + glm::vec3(0.0f, 2.0f, 0.0f);
    data.camera_target = game.ball.position + forward * 0.7f + glm::vec3(0.0f, 0.25f, 0.0f);
}

void set_follow_camera(render_data& data, const game_state& game) {
    data.camera_position = game.shot_camera_position;
    data.camera_target = follow_camera_target(game.ball.position);
}

std::vector<glm::vec3> estimate_aim_arc(const game_state& game) {
    std::vector<glm::vec3> points;
    if (game.selected_club >= game.tuning.clubs.size()) {
        return points;
    }

    const club_stats& club = game.tuning.clubs[game.selected_club].stats;
    const glm::vec3 forward = aim_direction(game.aim_angle);
    const float loft = radians(club.loft_degrees);
    const glm::vec3 launch_dir = glm::normalize(forward * std::cos(loft) + glm::vec3(0.0f, std::sin(loft), 0.0f));
    const glm::vec3 velocity = launch_dir * club.power;

    constexpr float gravity = -9.81f;
    constexpr float step_seconds = 0.18f;
    constexpr int max_points = 28;
    for (int i = 1; i <= max_points; ++i) {
        const float t = static_cast<float>(i) * step_seconds;
        glm::vec3 point = game.ball.position + velocity * t + glm::vec3(0.0f, 0.5f * gravity * t * t, 0.0f);
        const float terrain_height = terrain_height_at(game.tuning, point);
        if (point.y < terrain_height) {
            point.y = terrain_height;
            points.push_back(point);
            break;
        }
        points.push_back(point);
    }

    return points;
}

controls_overlay_state make_controls_overlay_state(const input_state& input) {
    controls_overlay_state controls;
    controls.key_1_down = input.key_1.is_down;
    controls.key_2_down = input.key_2.is_down;
    controls.left_down = input.left.is_down;
    controls.right_down = input.right.is_down;
    controls.up_down = input.up.is_down;
    controls.down_down = input.down.is_down;
    controls.space_down = input.space.is_down;
    controls.shift_down = input.left_shift.is_down;
    controls.enter_down = input.enter.is_down;
    controls.backspace_down = input.backspace.is_down;
    controls.retee_down = input.retee.is_down;
    return controls;
}

std::vector<render_skill_progress> make_render_skills(const skill_progression& progression) {
    const std::array<std::pair<const char*, const char*>, 3> skills{{
        {golf_swing_skill_id(), "GOLF SWING"},
        {smoking_skill_id(), "SMOKING"},
        {fitness_skill_id(), "FITNESS"}
    }};

    std::vector<render_skill_progress> rows;
    rows.reserve(skills.size());
    for (const auto& skill : skills) {
        render_skill_progress row;
        row.label = skill.second;
        row.xp = skill_xp(progression, skill.first);
        row.level = skill_level(row.xp);
        row.xp_to_next = xp_to_next_level(progression, skill.first);
        rows.push_back(row);
    }
    return rows;
}

render_data make_render_data(const game_state& game, const input_state& input) {
    render_data data;
    data.ball_position = game.ball.position;
    data.player_position = game.player.position;
    data.player_yaw = game.player.yaw;
    data.tee_position = terrain_anchor_at(game.tuning, game.tuning.course.tee_position);
    data.pin_position = terrain_anchor_at(game.tuning, game.tuning.course.pin_position);
    data.cup_radius = game.tuning.course.cup_radius;
    data.ball_visual_radius_meters = game.tuning.scale.ball_visual_radius_meters;
    data.cup_visual_radius_meters = game.tuning.scale.cup_visual_radius_meters;
    data.pin_visual_height_meters = game.tuning.scale.pin_visual_height_meters;
    data.course_extent = game.tuning.course.extent;
    data.terrain_vertices = make_render_terrain_vertices(game.tuning.terrain_mesh_data);
    data.terrain_indices = game.tuning.terrain_mesh_data.indices;
    append_render_terrain_mesh(data.terrain_vertices, data.terrain_indices, game.tuning.terrain_apron_mesh_data);
    set_material_overlay_render_mesh(data, game.tuning);
    data.trees = make_render_trees(game.tuning);
    data.aim_angle = game.aim_angle;
    data.camera_fov_degrees = 60.0f;
    if (game.mode == game_mode::aiming) {
        data.aim_arc_points = estimate_aim_arc(game);
    }
    data.ball_moving = ball_is_moving(game.ball, game.tuning);
    data.flight_path_points = game.flight_path_points;
    data.flight_path_color = game.tuning.flight_path.color;
    data.flight_path_alpha = game.tuning.flight_path.alpha;
    data.flight_path_width = game.tuning.flight_path.line_width;
    data.show_flight_path = data.ball_moving && !data.flight_path_points.empty();
    data.show_interact_prompt = game.mode == game_mode::walking && can_interact_with_ball(game);
    data.show_aim_indicator = game.mode == game_mode::aiming || game.mode == game_mode::addressing;
    data.shot_addressing = game.mode == game_mode::addressing;
    data.swing_timing = game.swing.phase == swing_phase::timing;
    data.show_power_meter = game.mode == game_mode::aiming || game.mode == game_mode::addressing || data.swing_timing;
    data.swing_power = game.swing.power;
    data.stroke_count = game.stroke_count;
    if (game.selected_club < game.tuning.clubs.size()) {
        data.selected_club_label = game.tuning.clubs[game.selected_club].label;
    }
    data.show_rangefinder = game.rangefinder_active;
    data.rangefinder_distance_meters = game.rangefinder_distance_meters;
    data.rangefinder_distance_label = game.rangefinder_distance_label;
    data.show_course_map = game.course_map_active;
    data.show_scorecard = game.scorecard_active;
    data.show_skills_panel = game.skills_panel_active;
    data.show_course_results = game.round.finished;
    data.scorecard = build_scorecard_data(game);
    data.skills = make_render_skills(game.save.skills);
    data.cart_active = game.cart.active;
    data.cart_drifting = game.cart.drift_timer > 0.0f;
    data.cart_yaw = game.cart.active ? game.cart.yaw : game.player.yaw;
    data.cart_speed = game.cart.velocity;
    data.smoke_emote_active = game.smoke_emote.active;
    data.beer_emote_active = game.beer_emote.active;
    data.smoke_emote_elapsed = game.smoke_emote.elapsed;
    data.beer_emote_elapsed = game.beer_emote.elapsed;
    data.controls = make_controls_overlay_state(input);

    if (game.mode == game_mode::walking && game.cart.active) {
        set_cart_camera(data, game);
    } else if (game.mode == game_mode::walking) {
        set_walking_camera(data, game);
    } else if (game.mode == game_mode::aiming) {
        set_aiming_camera(data, game);
    } else if (game.mode == game_mode::addressing) {
        set_address_camera(data, game);
    } else {
        set_follow_camera(data, game);
    }

    return data;
}

std::string format_fps_label(const int fps) {
    char buffer[16] = {};
    std::snprintf(buffer, sizeof(buffer), "FPS %d", std::max(0, fps));
    return std::string(buffer);
}

std::string relative_asset_path(const std::filesystem::path& asset_root, const std::filesystem::path& path) {
    std::error_code error;
    const std::filesystem::path relative = std::filesystem::relative(path, asset_root, error);
    if (error) {
        return path.string();
    }
    return relative.generic_string();
}

std::vector<startup_hole_option> load_startup_holes(const std::string& asset_root) {
    std::vector<startup_hole_option> options;
    const std::filesystem::path root(asset_root);
    const std::filesystem::path holes_dir = root / "holes";
    if (!std::filesystem::exists(holes_dir) || !std::filesystem::is_directory(holes_dir)) {
        return options;
    }

    std::vector<std::filesystem::path> files;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(holes_dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            files.push_back(entry.path());
        }
    }

    std::sort(files.begin(), files.end());
    for (const std::filesystem::path& path : files) {
        std::optional<hole_data> hole = load_hole_from_file(path.string());
        if (!hole) {
            continue;
        }
        if (hole->id.empty()) {
            hole->id = path.stem().string();
        }
        startup_hole_option option;
        option.path = relative_asset_path(root, path);
        option.hole = *hole;
        options.push_back(option);
    }
    return options;
}

render_hole_preview make_hole_preview(const hole_data& hole) {
    render_hole_preview preview;
    preview.tee_position = hole.tee_position;
    preview.pin_position = hole.pin_position;
    preview.control_points = hole.spline.control_points;
    preview.fairway_width = hole.spline.width;
    preview.material_zones = hole.material_zones;
    return preview;
}

std::string par_label(const int par) {
    char buffer[24] = {};
    std::snprintf(buffer, sizeof(buffer), "PAR %d", std::max(1, par));
    return std::string(buffer);
}

int course_total_par(const course_definition& course, const std::string& asset_root) {
    int total = 0;
    for (std::size_t i = 0; i < course.holes.size(); ++i) {
        const std::optional<hole_data> hole = load_hole_from_file(course_hole_path(asset_root, course, i));
        if (hole) {
            total += std::max(1, hole->par);
        }
    }
    return total;
}

std::optional<render_hole_preview> course_preview(const course_definition& course, const std::string& asset_root) {
    if (course.holes.empty()) {
        return std::nullopt;
    }
    const std::optional<hole_data> hole = load_hole_from_file(course_hole_path(asset_root, course, 0));
    if (!hole) {
        return std::nullopt;
    }
    return make_hole_preview(*hole);
}

std::string course_subtitle(const course_definition& course, const std::string& asset_root) {
    char buffer[48] = {};
    const int total_par = course_total_par(course, asset_root);
    std::snprintf(buffer,
                  sizeof(buffer),
                  "%d HOLES  PAR %d",
                  std::max(0, course.hole_count),
                  std::max(0, total_par));
    return std::string(buffer);
}

startup_menu_screen startup_screen_for_flow(const startup_flow flow) {
    switch (flow) {
    case startup_flow::main:
        return startup_menu_screen::main;
    case startup_flow::help:
        return startup_menu_screen::help;
    case startup_flow::hole_picker:
        return startup_menu_screen::hole_picker;
    case startup_flow::course_picker:
        return startup_menu_screen::course_picker;
    case startup_flow::shop_picker:
        return startup_menu_screen::shop_picker;
    case startup_flow::shop_inventory:
        return startup_menu_screen::shop_inventory;
    default:
        return startup_menu_screen::none;
    }
}

glm::vec2 startup_tile_center_for_hit(const startup_menu_screen screen, const int index) {
    if (screen == startup_menu_screen::main) {
        return glm::vec2(0.0f, 0.26f - static_cast<float>(index) * 0.24f);
    }

    constexpr int columns = 3;
    const int row = index / columns;
    const int column = index % columns;
    return glm::vec2(-0.58f + static_cast<float>(column) * 0.58f,
                     0.36f - static_cast<float>(row) * 0.38f);
}

glm::vec2 startup_tile_half_for_hit(const startup_menu_screen screen) {
    return screen == startup_menu_screen::main ? glm::vec2(0.42f, 0.095f) : glm::vec2(0.25f, 0.165f);
}

int startup_hit_index(const startup_menu_screen screen,
                      const int count,
                      const input_state& input,
                      SDL_Window* window) {
    if (!input.mouse_left.pressed || count <= 0 || window == nullptr) {
        return -1;
    }

    int width = 1;
    int height = 1;
    SDL_GetWindowSize(window, &width, &height);
    const float x = static_cast<float>(input.mouse_x) / static_cast<float>(std::max(1, width)) * 2.0f - 1.0f;
    const float y = 1.0f - static_cast<float>(input.mouse_y) / static_cast<float>(std::max(1, height)) * 2.0f;
    const glm::vec2 mouse(x, y);
    const glm::vec2 half = startup_tile_half_for_hit(screen);

    for (int i = 0; i < count; ++i) {
        const glm::vec2 center = startup_tile_center_for_hit(screen, i);
        if (std::abs(mouse.x - center.x) <= half.x && std::abs(mouse.y - center.y) <= half.y) {
            return i;
        }
    }
    return -1;
}

int startup_item_count(const startup_flow flow,
                       const std::vector<startup_hole_option>& holes,
                       const std::vector<course_definition>& courses,
                       const game_content& content,
                       const int active_shop_index) {
    if (flow == startup_flow::main) {
        return 5;
    }
    if (flow == startup_flow::help) {
        return 0;
    }
    if (flow == startup_flow::hole_picker) {
        return static_cast<int>(holes.size());
    }
    if (flow == startup_flow::course_picker) {
        return static_cast<int>(courses.size());
    }
    if (flow == startup_flow::shop_picker) {
        return static_cast<int>(content.shops.size());
    }
    if (flow == startup_flow::shop_inventory &&
        active_shop_index >= 0 &&
        active_shop_index < static_cast<int>(content.shops.size())) {
        return static_cast<int>(content.shops[static_cast<std::size_t>(active_shop_index)].items.size());
    }
    return 0;
}

void move_startup_selection(startup_flow flow,
                            int& selection,
                            const int count,
                            const input_state& input) {
    if (count <= 0) {
        selection = 0;
        return;
    }

    const int columns = flow == startup_flow::main ? 1 : 3;
    if (input.left.pressed) {
        selection = (selection + count - 1) % count;
    }
    if (input.right.pressed) {
        selection = (selection + 1) % count;
    }
    if (input.up.pressed) {
        selection = (selection + count - columns) % count;
    }
    if (input.down.pressed) {
        selection = (selection + columns) % count;
    }
    selection = std::max(0, std::min(selection, count - 1));
}

course_definition single_hole_course(const startup_hole_option& option) {
    course_definition course;
    course.id = option.hole.id.empty() ? "single_hole" : "single_" + option.hole.id;
    course.name = option.hole.name.empty() ? option.hole.id : option.hole.name;
    course.hole_count = 1;
    course.holes = {option.path};
    return course;
}

render_startup_menu make_startup_menu_render_data(const startup_flow flow,
                                                  const int selection,
                                                  const std::vector<startup_hole_option>& holes,
                                                  const game_content& content,
                                                  const save_data& save,
                                                  const int active_shop_index) {
    render_startup_menu menu;
    menu.screen = startup_screen_for_flow(flow);
    if (menu.screen == startup_menu_screen::none) {
        return menu;
    }

    if (flow == startup_flow::main) {
        menu.title = "GOLF++";
        menu.subtitle = "SELECT ROUND TYPE";
        menu.footer = "ARROWS MOVE  ENTER SELECT  ESC QUIT";
        const std::array<std::pair<const char*, const char*>, 5> items{{
            {"PLAY HOLE", "PICK ONE HOLE"},
            {"PLAY COURSE", "PLAY ORDERED HOLES"},
            {"SHOP", "BUY UNLOCKS"},
            {"HELP", "SHOW CONTROLS"},
            {"QUIT", "RETURN TO DESKTOP"}
        }};
        for (std::size_t i = 0; i < items.size(); ++i) {
            render_startup_tile tile;
            tile.title = items[i].first;
            tile.subtitle = items[i].second;
            tile.selected = static_cast<int>(i) == selection;
            menu.tiles.push_back(tile);
        }
        return menu;
    }

    if (flow == startup_flow::shop_picker) {
        menu.title = "SHOP";
        menu.subtitle = "CHOOSE A COUNTER";
        menu.footer = "ARROWS MOVE  ENTER OPEN  BACKSPACE BACK";
        for (std::size_t i = 0; i < content.shops.size(); ++i) {
            render_startup_tile tile;
            tile.title = content.shops[i].title;
            tile.subtitle = content.shops[i].subtitle;
            tile.selected = static_cast<int>(i) == selection;
            menu.tiles.push_back(tile);
        }
        return menu;
    }

    if (flow == startup_flow::shop_inventory) {
        if (active_shop_index < 0 || active_shop_index >= static_cast<int>(content.shops.size())) {
            menu.title = "SHOP";
            menu.subtitle = "NO SHOP";
            menu.footer = "BACKSPACE BACK";
            return menu;
        }

        const shop_definition& shop = content.shops[static_cast<std::size_t>(active_shop_index)];
        menu.title = shop.title;
        menu.subtitle = "GOLD " + std::to_string(std::max(0, save.money));
        menu.footer = "ARROWS MOVE  ENTER BUY  BACKSPACE BACK";
        for (std::size_t i = 0; i < shop.items.size(); ++i) {
            const shop_item_definition& item = shop.items[i];
            render_startup_tile tile;
            tile.title = item.title;
            tile.subtitle = shop_item_status_label(save, item);
            tile.selected = static_cast<int>(i) == selection;
            menu.tiles.push_back(tile);
        }
        return menu;
    }

    if (flow == startup_flow::help) {
        menu.title = "CONTROLS";
        menu.subtitle = "CURRENT GAMEPLAY INPUTS";
        menu.footer = "BACKSPACE BACK  ESC BACK";
        return menu;
    }

    if (flow == startup_flow::hole_picker) {
        menu.title = "PLAY HOLE";
        menu.subtitle = "CHOOSE A SINGLE HOLE";
        menu.footer = "ARROWS MOVE  ENTER START  BACKSPACE BACK";
        for (std::size_t i = 0; i < holes.size(); ++i) {
            render_startup_tile tile;
            tile.title = holes[i].hole.name.empty() ? holes[i].hole.id : holes[i].hole.name;
            tile.subtitle = par_label(holes[i].hole.par);
            tile.selected = static_cast<int>(i) == selection;
            tile.has_preview = true;
            tile.preview = make_hole_preview(holes[i].hole);
            menu.tiles.push_back(tile);
        }
        return menu;
    }

    menu.title = "PLAY COURSE";
    menu.subtitle = "CHOOSE A COURSE";
    menu.footer = "ARROWS MOVE  ENTER START  BACKSPACE BACK";
    for (std::size_t i = 0; i < content.courses.size(); ++i) {
        render_startup_tile tile;
        tile.title = content.courses[i].name.empty() ? content.courses[i].id : content.courses[i].name;
        tile.subtitle = course_subtitle(content.courses[i], content.asset_root);
        tile.selected = static_cast<int>(i) == selection;
        const std::optional<render_hole_preview> preview = course_preview(content.courses[i], content.asset_root);
        if (preview) {
            tile.has_preview = true;
            tile.preview = *preview;
        }
        menu.tiles.push_back(tile);
    }
    return menu;
}

render_startup_menu make_confirm_menu_render_data(const int selection) {
    render_startup_menu menu;
    menu.screen = startup_menu_screen::main;
    menu.title = "ARE YOU SURE";

    const std::array<const char*, 2> items{{"YES", "NO"}};
    for (std::size_t i = 0; i < items.size(); ++i) {
        render_startup_tile tile;
        tile.title = items[i];
        tile.selected = static_cast<int>(i) == selection;
        menu.tiles.push_back(tile);
    }
    return menu;
}

std::string club_hit_sound_id(const std::string& club_id) {
    if (club_id == "putter") {
        return "club_hit_putter";
    }
    if (club_id.find("wedge") != std::string::npos) {
        return "club_hit_wedge";
    }
    if (club_id.find("driver") != std::string::npos || club_id.find("wood") != std::string::npos) {
        return "club_hit_driver";
    }
    return "club_hit_iron";
}

std::string ball_land_sound_id(const terrain_material material) {
    if (material == terrain_material::water) {
        return "ball_splash";
    }
    if (material == terrain_material::bunker) {
        return "ball_land_bunker";
    }
    return "ball_land_grass";
}

void play_audio_event(audio_engine& audio, const audio_event& event) {
    switch (event.type) {
    case audio_event_type::swing_start:
        audio.play("swing_start");
        break;
    case audio_event_type::club_hit:
        audio.play(club_hit_sound_id(event.club_id));
        break;
    case audio_event_type::ball_land:
        audio.play(ball_land_sound_id(event.material));
        break;
    case audio_event_type::ball_tree_hit:
        audio.play("ball_tree_hit");
        break;
    case audio_event_type::ball_cup:
        audio.play("ball_cup");
        break;
    case audio_event_type::hole_complete:
        audio.play("hole_complete");
        break;
    case audio_event_type::club_change:
        audio.play("club_change");
        break;
    case audio_event_type::cart_start:
        audio.play("cart_start");
        break;
    case audio_event_type::cart_drift:
        audio.play("cart_drift");
        break;
    case audio_event_type::emote_smoke:
        audio.play("emote_smoke");
        break;
    case audio_event_type::emote_beer:
        audio.play("emote_beer");
        break;
    }
}

void drain_audio_events(audio_engine& audio, game_state& game) {
    for (const audio_event& event : game.audio_events) {
        play_audio_event(audio, event);
    }
    game.audio_events.clear();
}

bool skills_changed(const skill_progression& before, const skill_progression& after) {
    if (before.size() != after.size()) {
        return true;
    }

    for (const auto& skill : before) {
        const auto it = after.find(skill.first);
        if (it == after.end() || it->second.xp != skill.second.xp) {
            return true;
        }
    }
    return false;
}

bool save_progress_changed(const save_data& before, const save_data& after) {
    return before.money != after.money ||
        before.unlocked_items != after.unlocked_items ||
        before.completed_quest_ids != after.completed_quest_ids ||
        before.completed_course_ids != after.completed_course_ids ||
        before.current_course_id != after.current_course_id ||
        before.current_hole_index != after.current_hole_index ||
        before.hole_scores != after.hole_scores ||
        skills_changed(before.skills, after.skills);
}

bool save_completion_progress_changed(const save_data& before, const save_data& after) {
    return before.completed_course_ids != after.completed_course_ids ||
        before.current_course_id != after.current_course_id ||
        before.current_hole_index != after.current_hole_index ||
        before.hole_scores != after.hole_scores;
}
}

void app::mark_current_save_dirty() {
    if (!save_initialized_) {
        return;
    }
    mark_save_slot_dirty(save_slot_, game_.save);
}

bool app::persist_current_save() {
    if (!save_initialized_) {
        return false;
    }

    save_slot_.save = game_.save;
    const bool saved = persist_save_slot(save_paths_, save_slot_);
    if (saved) {
        sync_current_save();
    }
    return saved;
}

void app::sync_current_save() {
    if (!save_initialized_) {
        return;
    }

    cloud_sync_request request;
    request.local_save = save_slot_.save;
    request.profile = save_slot_.profile;
    request.local_dirty = save_slot_.profile.dirty;
    cloud_save_.sync(request);
}

bool app::init() {
    if (!window_.init("golf++", 1280, 720)) {
        return false;
    }

    if (!renderer_.init(window_.sdl_window())) {
        window_.shutdown();
        return false;
    }

    char* base_path = SDL_GetBasePath();
    const std::string asset_root = resolve_asset_root(base_path != nullptr ? base_path : "");
    game_ = make_initial_game_state(asset_root);
    content_ = load_game_content(asset_root);
    hole_options_ = load_startup_holes(asset_root);

    char* pref_path = SDL_GetPrefPath("golfplusplus", "golf++");
    const std::string save_root = pref_path != nullptr
        ? std::string(pref_path)
        : (std::filesystem::path(asset_root) / "saves").string();
    save_paths_ = default_save_paths(save_root);
    save_slot_ = load_or_create_save_slot(save_paths_, game_.save);
    game_.save = save_slot_.save;
    refresh_unlocked_clubs(game_);
    save_initialized_ = true;
    if (!save_slot_.loaded_existing_profile || !save_slot_.loaded_existing_save) {
        persist_current_save();
    } else {
        sync_current_save();
    }
    if (pref_path != nullptr) {
        SDL_free(pref_path);
    }

    audio_.init();
    audio_.load_manifest(std::filesystem::path(asset_root) / "audio" / "sounds.json");
    audio_.start_ambience("ambience_menu_vcr");
    if (base_path != nullptr) {
        SDL_free(base_path);
    }
    running_ = true;
    return true;
}

void app::run() {
    Uint64 previous_counter = SDL_GetPerformanceCounter();
    const double performance_frequency = static_cast<double>(SDL_GetPerformanceFrequency());
    bool cart_loop_active = false;

    while (running_) {
        const Uint64 current_counter = SDL_GetPerformanceCounter();
        const double elapsed = static_cast<double>(current_counter - previous_counter) / performance_frequency;
        previous_counter = current_counter;
        const float dt = std::min(static_cast<float>(elapsed), 0.05f);

        input_.reset_frame();
        poll_events(input_);

        if (input_.ctrl.pressed) {
            show_fps_ = !show_fps_;
        }

        fps_elapsed_seconds_ += std::max(0.0f, dt);
        ++fps_frame_count_;
        if (fps_elapsed_seconds_ >= 0.25f) {
            displayed_fps_ = static_cast<int>(std::floor(static_cast<float>(fps_frame_count_) / fps_elapsed_seconds_ + 0.5f));
            fps_elapsed_seconds_ = 0.0f;
            fps_frame_count_ = 0;
        }

        if (startup_flow_ != startup_flow::playing) {
            if (input_.quit_requested) {
                running_ = false;
            }

            const int count = startup_item_count(startup_flow_, hole_options_, content_.courses, content_, active_shop_index_);
            const startup_menu_screen screen = startup_screen_for_flow(startup_flow_);
            const int previous_selection = startup_selection_;
            const int hit = startup_hit_index(screen, count, input_, window_.sdl_window());
            if (hit >= 0) {
                startup_selection_ = hit;
            }
            move_startup_selection(startup_flow_, startup_selection_, count, input_);
            if (startup_selection_ != previous_selection) {
                audio_.play("ui_move");
            }

            const bool accept = input_.enter.pressed || input_.space.pressed || hit >= 0;
            const bool back = input_.backspace.pressed || input_.escape.pressed;
            if (back) {
                audio_.play("ui_back");
                if (startup_flow_ == startup_flow::main) {
                    running_ = false;
                } else if (startup_flow_ == startup_flow::shop_inventory) {
                    startup_flow_ = startup_flow::shop_picker;
                    startup_selection_ = std::max(0, std::min(active_shop_index_, static_cast<int>(content_.shops.size()) - 1));
                } else {
                    startup_flow_ = startup_flow::main;
                    startup_selection_ = 0;
                }
            } else if (accept) {
                if (startup_flow_ == startup_flow::main) {
                    audio_.play("ui_select");
                    if (startup_selection_ == 0) {
                        startup_flow_ = startup_flow::hole_picker;
                        startup_selection_ = 0;
                    } else if (startup_selection_ == 1) {
                        startup_flow_ = startup_flow::course_picker;
                        startup_selection_ = 0;
                    } else if (startup_selection_ == 2) {
                        startup_flow_ = startup_flow::shop_picker;
                        startup_selection_ = 0;
                    } else if (startup_selection_ == 3) {
                        startup_flow_ = startup_flow::help;
                        startup_selection_ = 0;
                    } else {
                        running_ = false;
                    }
                } else if (startup_flow_ == startup_flow::hole_picker && count > 0) {
                    audio_.play("ui_select");
                    if (start_game_course(game_, single_hole_course(hole_options_[static_cast<std::size_t>(startup_selection_)]))) {
                        startup_flow_ = startup_flow::playing;
                        audio_.start_ambience("ambience_course_day");
                    }
                } else if (startup_flow_ == startup_flow::course_picker && count > 0) {
                    audio_.play("ui_select");
                    if (start_game_course(game_, content_.courses[static_cast<std::size_t>(startup_selection_)])) {
                        startup_flow_ = startup_flow::playing;
                        audio_.start_ambience("ambience_course_day");
                    }
                } else if (startup_flow_ == startup_flow::shop_picker && count > 0) {
                    audio_.play("ui_select");
                    active_shop_index_ = startup_selection_;
                    startup_flow_ = startup_flow::shop_inventory;
                    startup_selection_ = 0;
                } else if (startup_flow_ == startup_flow::shop_inventory &&
                           active_shop_index_ >= 0 &&
                           active_shop_index_ < static_cast<int>(content_.shops.size()) &&
                           count > 0) {
                    const shop_definition& shop = content_.shops[static_cast<std::size_t>(active_shop_index_)];
                    const save_data save_before_purchase = game_.save;
                    const shop_purchase_result result = purchase_shop_item(game_.save,
                                                                           shop.items[static_cast<std::size_t>(startup_selection_)]);
                    if (result == shop_purchase_result::purchased &&
                        save_progress_changed(save_before_purchase, game_.save)) {
                        refresh_unlocked_clubs(game_);
                        mark_current_save_dirty();
                        persist_current_save();
                    }
                    audio_.play(result == shop_purchase_result::purchased ? "ui_select" : "ui_back");
                }
            }

            render_data data = make_render_data(game_, input_);
            data.show_fps = show_fps_;
            data.fps_label = format_fps_label(displayed_fps_);
            data.startup_menu = make_startup_menu_render_data(startup_flow_,
                                                              startup_selection_,
                                                              hole_options_,
                                                              content_,
                                                              game_.save,
                                                              active_shop_index_);
            renderer_.render(data);
            window_.swap();
            continue;
        }

        if (game_.round.finished) {
            if (input_.quit_requested) {
                running_ = false;
            }

            const bool leave_results = input_.enter.pressed ||
                input_.space.pressed ||
                input_.escape.pressed ||
                input_.backspace.pressed;
            if (leave_results) {
                startup_flow_ = startup_flow::main;
                startup_selection_ = 0;
                confirm_menu_active_ = false;
                confirm_selection_ = 1;
                game_ = make_initial_game_state(game_.asset_root);
                game_.save = save_slot_.save;
                refresh_unlocked_clubs(game_);
                audio_.stop_loop("cart_drive_loop");
                cart_loop_active = false;
                audio_.play("ui_select");
                audio_.start_ambience("ambience_menu_vcr");
            }

            render_data data = make_render_data(game_, input_);
            data.show_fps = show_fps_;
            data.fps_label = format_fps_label(displayed_fps_);
            if (startup_flow_ != startup_flow::playing) {
                data.startup_menu = make_startup_menu_render_data(startup_flow_,
                                                                  startup_selection_,
                                                                  hole_options_,
                                                                  content_,
                                                                  game_.save,
                                                                  active_shop_index_);
            }
            renderer_.render(data);
            window_.swap();
            continue;
        }

        bool confirm_opened_this_frame = false;
        if (!confirm_menu_active_ && input_.escape.pressed && game_.mode == game_mode::walking) {
            confirm_menu_active_ = true;
            confirm_selection_ = 1;
            confirm_opened_this_frame = true;
        }

        if (confirm_menu_active_) {
            if (input_.quit_requested) {
                running_ = false;
            } else if (!confirm_opened_this_frame) {
                const int count = 2;
                const int previous_selection = confirm_selection_;
                const int hit = startup_hit_index(startup_menu_screen::main, count, input_, window_.sdl_window());
                if (hit >= 0) {
                    confirm_selection_ = hit;
                }
                move_startup_selection(startup_flow::main, confirm_selection_, count, input_);
                if (confirm_selection_ != previous_selection) {
                    audio_.play("ui_move");
                }

                const bool accept = input_.enter.pressed || input_.space.pressed || hit >= 0;
                const bool cancel = input_.escape.pressed || input_.backspace.pressed;

                if (accept) {
                    audio_.play("ui_select");
                    if (confirm_selection_ == 0) {
                        confirm_menu_active_ = false;
                        startup_flow_ = startup_flow::main;
                        startup_selection_ = 0;
                        game_ = make_initial_game_state(game_.asset_root);
                        game_.save = save_slot_.save;
                        refresh_unlocked_clubs(game_);
                        audio_.stop_loop("cart_drive_loop");
                        cart_loop_active = false;
                        audio_.start_ambience("ambience_menu_vcr");
                    } else {
                        confirm_menu_active_ = false;
                    }
                } else if (cancel) {
                    audio_.play("ui_back");
                    confirm_menu_active_ = false;
                }
            }

            render_data data = make_render_data(game_, input_);
            data.show_fps = show_fps_;
            data.fps_label = format_fps_label(displayed_fps_);
            data.startup_menu = make_confirm_menu_render_data(confirm_selection_);
            renderer_.render(data);
            window_.swap();
            continue;
        }

        const save_data save_before_update = game_.save;
        update_game(game_, input_, dt);
        if (save_completion_progress_changed(save_before_update, game_.save)) {
            mark_current_save_dirty();
            persist_current_save();
        }
        drain_audio_events(audio_, game_);

        const bool cart_moving = game_.cart.active && std::abs(game_.cart.velocity) > 0.05f;
        if (cart_moving && !cart_loop_active) {
            audio_.play_loop("cart_drive_loop");
            cart_loop_active = true;
        } else if (!cart_moving && cart_loop_active) {
            audio_.stop_loop("cart_drive_loop");
            cart_loop_active = false;
        }

        if (input_.quit_requested) {
            running_ = false;
        }

        render_data data = make_render_data(game_, input_);
        data.show_fps = show_fps_;
        data.fps_label = format_fps_label(displayed_fps_);
        renderer_.render(data);
        window_.swap();
    }

    if (save_initialized_ && startup_flow_ == startup_flow::playing) {
        mark_current_save_dirty();
        persist_current_save();
    } else if (save_initialized_ && save_slot_.profile.dirty) {
        persist_save_slot(save_paths_, save_slot_);
        sync_current_save();
    }
}

void app::shutdown() {
    audio_.shutdown();
    renderer_.shutdown();
    window_.shutdown();
}
