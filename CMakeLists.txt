cmake_minimum_required(VERSION 3.25)
project(golfpp LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(GOLFPP_BUILD_TESTS "Build tests" OFF)

set(GOLFPP_SOURCES
    src/core/main.cpp
    src/core/app.cpp
    src/core/startup_flow.cpp
    src/core/startup_options.cpp
    src/core/window.cpp
    src/core/event_loop.cpp
    src/core/gl_loader.cpp
    src/audio/audio_manifest.cpp
    src/audio/audio_engine.cpp
    src/renderer/shader.cpp
    src/renderer/framebuffer.cpp
    src/renderer/dynamic_buffer.cpp
    src/renderer/frustum.cpp
    src/renderer/camera_transition.cpp
    src/renderer/render_mesh_chunks.cpp
    src/renderer/gl_timer.cpp
    src/renderer/overlay_pass.cpp
    src/renderer/renderer.cpp
    src/renderer/tree_renderer.cpp
    src/renderer/primitive_mesh.cpp
    src/renderer/cart_batch.cpp
    src/renderer/world_marker_batch.cpp
    src/renderer/world_marker_renderer.cpp
)

add_executable(golfpp ${GOLFPP_SOURCES})
set_target_properties(golfpp PROPERTIES OUTPUT_NAME "golf++")

target_include_directories(golfpp PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor
)

target_compile_definitions(golfpp PRIVATE SDL_MAIN_HANDLED)

find_package(SDL2 REQUIRED)
target_link_libraries(golfpp PRIVATE SDL2::SDL2)

find_package(SDL2_mixer QUIET)
if (NOT TARGET SDL2_mixer::SDL2_mixer AND
    NOT TARGET SDL2_mixer::SDL2_mixer-static AND
    NOT TARGET SDL2::SDL2_mixer AND
    NOT SDL2_MIXER_LIBRARY AND
    NOT SDL2_MIXER_LIBRARIES)
    find_package(PkgConfig QUIET)
    if (PkgConfig_FOUND)
        pkg_check_modules(SDL2_MIXER QUIET SDL2_mixer)
    endif()
endif()
if (NOT TARGET SDL2_mixer::SDL2_mixer AND
    NOT TARGET SDL2_mixer::SDL2_mixer-static AND
    NOT TARGET SDL2::SDL2_mixer AND
    NOT SDL2_MIXER_LIBRARY AND
    NOT SDL2_MIXER_LIBRARIES)
    message(FATAL_ERROR "SDL2_mixer not found. Install SDL2_mixer or provide an SDL2_mixer CMake/pkg-config package.")
endif()
if (TARGET SDL2_mixer::SDL2_mixer)
    target_link_libraries(golfpp PRIVATE SDL2_mixer::SDL2_mixer)
elseif (TARGET SDL2_mixer::SDL2_mixer-static)
    target_link_libraries(golfpp PRIVATE SDL2_mixer::SDL2_mixer-static)
elseif (TARGET SDL2::SDL2_mixer)
    target_link_libraries(golfpp PRIVATE SDL2::SDL2_mixer)
else()
    target_include_directories(golfpp PRIVATE ${SDL2_MIXER_INCLUDE_DIRS} ${SDL2_MIXER_INCLUDE_DIR})
    target_link_libraries(golfpp PRIVATE ${SDL2_MIXER_LIBRARIES} ${SDL2_MIXER_LIBRARY})
endif()

find_package(OpenGL REQUIRED)
target_link_libraries(golfpp PRIVATE OpenGL::GL)

set(GLAD_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/src/glad/glad.c)
if (EXISTS ${GLAD_SOURCE})
    target_sources(golfpp PRIVATE ${GLAD_SOURCE})
    target_include_directories(golfpp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/glad)
    target_compile_definitions(golfpp PRIVATE GOLFPP_USE_GLAD=1)
endif()

set(GOLFPP_PHYSICS_SOURCES
    src/physics/ball_physics.cpp
    src/physics/flight_model.cpp
    src/physics/collision.cpp
    src/physics/tree_collision.cpp
    src/physics/terrain.cpp
    src/physics/wind.cpp
)

set(GOLFPP_GAME_SOURCES
    src/game/asset_resolver.cpp
    src/game/club_loader.cpp
    src/game/course_loader.cpp
    src/game/course_world_loader.cpp
    src/game/game_content.cpp
    src/game/hole_loader.cpp
    src/game/progression.cpp
    src/game/save_data.cpp
    src/game/save_manager.cpp
    src/game/round_state.cpp
    src/game/scorecard.cpp
    src/game/game_tuning.cpp
    src/game/game_state.cpp
    src/game/swing.cpp
)

set(GLM_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/vendor/glm)
if (EXISTS ${GLM_ROOT}/glm/vec3.hpp)
    set(GLM_INCLUDE_DIR ${GLM_ROOT})
else()
    find_path(GLM_INCLUDE_DIR glm/vec3.hpp)
endif()

if (NOT GLM_INCLUDE_DIR)
    message(FATAL_ERROR "GLM not found. Install it or add vendor/glm.")
endif()

file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/assets" GOLFPP_ASSETS_DIR)
target_include_directories(golfpp PRIVATE ${GLM_INCLUDE_DIR})
target_compile_definitions(golfpp PRIVATE GOLFPP_ASSETS_DIR=\"${GOLFPP_ASSETS_DIR}\")

add_library(golfpp-profiling src/profiling/profiling.cpp)
target_include_directories(golfpp-profiling PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(golfpp PRIVATE golfpp-profiling)

# Text assets as data: pixel font, string table, text styles (unit tested).
# Shared by game code and the GL-free overlay drawing.
add_library(golfpp-text
    src/game/pixel_font_data.cpp
    src/game/string_table.cpp
    src/game/text_style.cpp
    src/game/text_assets.cpp
)
target_include_directories(golfpp-text PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor
    ${GLM_INCLUDE_DIR}
)

# GL-free overlay batch builder, pixel text, menus and widgets (unit tested).
add_library(golfpp-overlay
    src/renderer/overlay_batch.cpp
    src/renderer/course_map_fill.cpp
    src/renderer/pixel_font.cpp
    src/renderer/control_icons.cpp
    src/renderer/menu_overlay.cpp
    src/renderer/text_input.cpp
)
target_include_directories(golfpp-overlay PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${GLM_INCLUDE_DIR}
)
target_link_libraries(golfpp-overlay PUBLIC golfpp-text)

target_link_libraries(golfpp PRIVATE golfpp-overlay)

add_library(golfpp-physics ${GOLFPP_PHYSICS_SOURCES})
target_include_directories(golfpp-physics PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${GLM_INCLUDE_DIR}
)

target_link_libraries(golfpp PRIVATE golfpp-physics)

add_library(golfpp-game ${GOLFPP_GAME_SOURCES})
target_include_directories(golfpp-game PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor
    ${GLM_INCLUDE_DIR}
)
target_link_libraries(golfpp-game PUBLIC golfpp-physics golfpp-profiling golfpp-text)
target_compile_definitions(golfpp-game PUBLIC GOLFPP_ASSETS_DIR=\"${GOLFPP_ASSETS_DIR}\")

target_link_libraries(golfpp PRIVATE golfpp-game)

if (WIN32)
    target_sources(golfpp PRIVATE src/platform/windows/golfpp.rc)
endif()

if (GOLFPP_BUILD_TESTS)
    add_executable(golfpp-tests
        tests/physics_tests.cpp
        tests/startup_options_tests.cpp
        tests/game_tests.cpp
        tests/profiling_tests.cpp
        tests/shader_tests.cpp
        tests/world_marker_tests.cpp
        tests/render_culling_tests.cpp
        tests/camera_transition_tests.cpp
        tests/audio_manifest_tests.cpp
        tests/overlay_batch_tests.cpp
        tests/dynamic_buffer_tests.cpp
        tests/course_map_fill_tests.cpp
        tests/text_assets_tests.cpp
        tests/text_input_tests.cpp
        tests/startup_flow_tests.cpp
        src/audio/audio_manifest.cpp
        src/core/startup_flow.cpp
        src/core/startup_options.cpp
        src/renderer/primitive_mesh.cpp
        src/renderer/cart_batch.cpp
        src/renderer/world_marker_batch.cpp
        src/renderer/frustum.cpp
        src/renderer/camera_transition.cpp
        src/renderer/render_mesh_chunks.cpp
    )
    set_target_properties(golfpp-tests PROPERTIES OUTPUT_NAME "golf++-tests")
    target_link_libraries(golfpp-tests PRIVATE golfpp-physics golfpp-game golfpp-profiling)
    target_link_libraries(golfpp-tests PRIVATE golfpp-overlay)
    target_include_directories(golfpp-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/vendor)
    file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures" GOLFPP_TEST_FIXTURES_DIR)
    target_compile_definitions(golfpp-tests PRIVATE GOLFPP_TEST_FIXTURES_DIR=\"${GOLFPP_TEST_FIXTURES_DIR}\")
    file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/src" GOLFPP_SOURCE_DIR)
    target_compile_definitions(golfpp-tests PRIVATE GOLFPP_SOURCE_DIR=\"${GOLFPP_SOURCE_DIR}\")
endif()
