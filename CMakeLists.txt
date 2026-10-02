cmake_minimum_required(VERSION 3.25)
project(golfpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(GOLFPP_BUILD_TESTS "Build tests" OFF)

# --- Third-party -------------------------------------------------------------

find_package(SDL2 REQUIRED)
find_package(OpenGL REQUIRED)

find_package(SDL2_mixer QUIET)
if (TARGET SDL2_mixer::SDL2_mixer)
    set(GOLFPP_SDL2_MIXER SDL2_mixer::SDL2_mixer)
elseif (TARGET SDL2_mixer::SDL2_mixer-static)
    set(GOLFPP_SDL2_MIXER SDL2_mixer::SDL2_mixer-static)
elseif (TARGET SDL2::SDL2_mixer)
    set(GOLFPP_SDL2_MIXER SDL2::SDL2_mixer)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2_MIXER REQUIRED IMPORTED_TARGET SDL2_mixer)
    set(GOLFPP_SDL2_MIXER PkgConfig::SDL2_MIXER)
endif()

find_package(glm CONFIG REQUIRED)

# Vendored header-only libraries (nlohmann/json, doctest) are SYSTEM so their
# warnings stay out of ours.
add_library(golfpp-third-party INTERFACE)
target_include_directories(golfpp-third-party SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/vendor)
target_link_libraries(golfpp-third-party INTERFACE glm::glm)

add_library(golfpp-warnings INTERFACE)
if (MSVC)
    target_compile_options(golfpp-warnings INTERFACE /W4)
else()
    target_compile_options(golfpp-warnings INTERFACE -Wall -Wextra -Wpedantic -Wshadow)
endif()

file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/assets" GOLFPP_ASSETS_DIR)

# Every library below is free of SDL and OpenGL so tests can link it without a
# window. Only the golfpp executable touches SDL/GL.
function(golfpp_library name)
    add_library(${name} STATIC ${ARGN})
    target_include_directories(${name} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(${name} PUBLIC golfpp-third-party PRIVATE golfpp-warnings)
endfunction()

# Pure functional zone: see AGENTS.md.
golfpp_library(golfpp-physics
    src/physics/ball_physics.cpp
    src/physics/collision.cpp
    src/physics/flight_model.cpp
    src/physics/ground_contact.cpp
    src/physics/ground_mesh.cpp
    src/physics/terrain.cpp
    src/physics/tree_collision.cpp
    src/physics/wind.cpp
)

golfpp_library(golfpp-profiling
    src/profiling/profiling.cpp
)

golfpp_library(golfpp-game
    src/game/asset_resolver.cpp
    src/game/club_loader.cpp
    src/game/course_loader.cpp
    src/game/course_session.cpp
    src/game/course_world_loader.cpp
    src/game/game_content.cpp
    src/game/game_state.cpp
    src/game/hole_loader.cpp
    src/game/json_util.cpp
    src/game/pixel_font_data.cpp
    src/game/play_area.cpp
    src/game/progress_rules.cpp
    src/game/progression.cpp
    src/game/reward_rules.cpp
    src/game/round_state.cpp
    src/game/save_data.cpp
    src/game/save_manager.cpp
    src/game/scorecard.cpp
    src/game/string_table.cpp
    src/game/swing.cpp
    src/game/text_assets.cpp
    src/game/text_style.cpp
    src/game/tuning_loader.cpp
    src/game/utf8.cpp
)
target_link_libraries(golfpp-game PUBLIC golfpp-physics golfpp-profiling)
target_compile_definitions(golfpp-game PRIVATE GOLFPP_ASSETS_DIR="${GOLFPP_ASSETS_DIR}")

# GL-free render data: overlay/menu drawing, world marker and cart batches,
# culling, camera blending. The GL side lives in the executable.
golfpp_library(golfpp-render-data
    src/renderer/camera_local.cpp
    src/renderer/camera_transition.cpp
    src/renderer/cart_batch.cpp
    src/renderer/control_icons.cpp
    src/renderer/course_map_fill.cpp
    src/renderer/frustum.cpp
    src/renderer/hud_overlay.cpp
    src/renderer/menu_overlay.cpp
    src/renderer/overlay_batch.cpp
    src/renderer/pixel_font.cpp
    src/renderer/primitive_mesh.cpp
    src/renderer/render_mesh_chunks.cpp
    src/renderer/render_tree.cpp
    src/renderer/scorecard_overlay.cpp
    src/renderer/terrain_palette.cpp
    src/renderer/terrain_render_mesh.cpp
    src/renderer/text_input.cpp
    src/renderer/ui_rect.cpp
    src/renderer/world_marker_batch.cpp
)
target_link_libraries(golfpp-render-data PUBLIC golfpp-game)

# SDL-free app logic: startup options, menu flow, audio manifest.
golfpp_library(golfpp-app-logic
    src/audio/audio_manifest.cpp
    src/core/input.cpp
    src/core/key_bindings.cpp
    src/core/render_frame.cpp
    src/core/startup_flow.cpp
    src/core/startup_options.cpp
)
target_link_libraries(golfpp-app-logic PUBLIC golfpp-render-data)

add_executable(golfpp
    src/audio/audio_engine.cpp
    src/core/app.cpp
    src/core/event_loop.cpp
    src/core/main.cpp
    src/core/window.cpp
    src/renderer/dynamic_buffer.cpp
    src/renderer/framebuffer.cpp
    src/renderer/gl_loader.cpp
    src/renderer/gl_timer.cpp
    src/renderer/overlay_pass.cpp
    src/renderer/renderer.cpp
    src/renderer/shader.cpp
    src/renderer/tree_renderer.cpp
    src/renderer/world_marker_renderer.cpp
)
set_target_properties(golfpp PROPERTIES OUTPUT_NAME "golf++")
target_compile_definitions(golfpp PRIVATE SDL_MAIN_HANDLED)
target_link_libraries(golfpp PRIVATE
    golfpp-app-logic
    golfpp-warnings
    SDL2::SDL2
    ${GOLFPP_SDL2_MIXER}
    OpenGL::GL
)
if (WIN32)
    target_sources(golfpp PRIVATE src/platform/windows/golfpp.rc)
endif()

if (GOLFPP_BUILD_TESTS)
    add_executable(golfpp-tests
        tests/audio_manifest_tests.cpp
        tests/camera_transition_tests.cpp
        tests/content_tests.cpp
        tests/course_map_fill_tests.cpp
        tests/course_tests.cpp
        tests/dynamic_buffer_tests.cpp
        tests/gameplay_tests.cpp
        tests/overlay_batch_tests.cpp
        tests/physics_tests.cpp
        tests/profiling_tests.cpp
        tests/progression_tests.cpp
        tests/render_culling_tests.cpp
        tests/render_frame_tests.cpp
        tests/shader_tests.cpp
        tests/startup_flow_tests.cpp
        tests/startup_options_tests.cpp
        tests/text_assets_tests.cpp
        tests/text_input_tests.cpp
        tests/text_layout_tests.cpp
        tests/utf8_tests.cpp
        tests/world_marker_tests.cpp
    )
    set_target_properties(golfpp-tests PROPERTIES OUTPUT_NAME "golf++-tests")
    target_link_libraries(golfpp-tests PRIVATE golfpp-app-logic)
    target_include_directories(golfpp-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures" GOLFPP_TEST_FIXTURES_DIR)
    file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/src" GOLFPP_SOURCE_DIR)
    target_compile_definitions(golfpp-tests PRIVATE
        GOLFPP_ASSETS_DIR="${GOLFPP_ASSETS_DIR}"
        GOLFPP_TEST_FIXTURES_DIR="${GOLFPP_TEST_FIXTURES_DIR}"
        GOLFPP_SOURCE_DIR="${GOLFPP_SOURCE_DIR}"
    )
endif()
