#pragma once

// What the picture checks share: a course loaded as the game loads it, with
// its render meshes, read back pictures and pictures written for a person to
// look at when a check fails.

#include "game/game_state.h"
#include "game/lighting_loader.h"
#include "renderer/framebuffer.h"
#include "renderer/render_mesh.h"
#include "renderer/grass_field.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <glm/vec4.hpp>

// A course started as GOLFPP_COURSE starts it, and its ground as the
// renderer draws it (core/app_scene.cpp).
struct check_course {
    game_state game;
    render_static_mesh terrain;
    render_static_mesh overlay;
    grass_field grass;
};

check_course load_check_course(const game_content& content, const course_definition& course, const lighting_palette& palette);

std::string check_shader_path(const std::string& asset_root, const char* name);

// The framebuffer's colour in its corner `width` by `height`, row by row
// from the bottom.
std::vector<glm::vec4> read_colors(const framebuffer& target, int width, int height);

// A 24-bit BMP of `pixels` (row by row from the bottom). False when it
// cannot be written.
bool write_check_bmp(const std::filesystem::path& path, int width, int height,
                     const std::vector<std::array<std::uint8_t, 3>>& pixels);
