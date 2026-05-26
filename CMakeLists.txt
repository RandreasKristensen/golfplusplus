cmake_minimum_required(VERSION 3.25)
project(golfpp LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(VCR_GOLF_BUILD_TESTS "Build tests" OFF)

set(VCR_GOLF_SOURCES
    core/main.cpp
    core/app.cpp
    core/window.cpp
    core/event_loop.cpp
    core/gl_loader.cpp
    src/audio/audio_manifest.cpp
    src/audio/audio_engine.cpp
    src/renderer/shader.cpp
    src/renderer/framebuffer.cpp
    src/renderer/renderer.cpp
)

add_executable(golfpp ${VCR_GOLF_SOURCES})
set_target_properties(golfpp PROPERTIES OUTPUT_NAME "golf++")

target_include_directories(golfpp PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
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
    target_compile_definitions(golfpp PRIVATE VCR_GOLF_USE_GLAD=1)
endif()

set(VCR_GOLF_PHYSICS_SOURCES
    src/physics/ball_physics.cpp
    src/physics/flight_model.cpp
    src/physics/collision.cpp
    src/physics/tree_collision.cpp
    src/physics/terrain.cpp
    src/physics/wind.cpp
)

set(VCR_GOLF_GAME_SOURCES
    src/game/asset_resolver.cpp
    src/game/club_loader.cpp
    src/game/course_loader.cpp
    src/game/game_content.cpp
    src/game/hole_loader.cpp
    src/game/save_data.cpp
    src/game/round_state.cpp
    src/game/scorecard.cpp
    src/game/game_tuning.cpp
    src/game/game_state.cpp
    src/game/swing.cpp
    quest/quest_loader.cpp
    quest/quest_engine.cpp
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

file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/assets" VCR_GOLF_ASSETS_DIR)
target_include_directories(golfpp PRIVATE ${GLM_INCLUDE_DIR})
target_compile_definitions(golfpp PRIVATE VCR_GOLF_ASSETS_DIR=\"${VCR_GOLF_ASSETS_DIR}\")

add_library(vcr-golf-physics ${VCR_GOLF_PHYSICS_SOURCES})
target_include_directories(vcr-golf-physics PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${GLM_INCLUDE_DIR}
)

target_link_libraries(golfpp PRIVATE vcr-golf-physics)

add_library(vcr-golf-game ${VCR_GOLF_GAME_SOURCES})
target_include_directories(vcr-golf-game PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor
    ${GLM_INCLUDE_DIR}
)
target_link_libraries(vcr-golf-game PUBLIC vcr-golf-physics)
target_compile_definitions(vcr-golf-game PUBLIC VCR_GOLF_ASSETS_DIR=\"${VCR_GOLF_ASSETS_DIR}\")

target_link_libraries(golfpp PRIVATE vcr-golf-game)

if (WIN32)
    target_sources(golfpp PRIVATE windows/golfpp.rc)
endif()

if (VCR_GOLF_BUILD_TESTS)
    add_executable(golfpp-tests
        tests/physics_tests.cpp
        tests/game_tests.cpp
        tests/audio_manifest_tests.cpp
        src/audio/audio_manifest.cpp
    )
    set_target_properties(golfpp-tests PROPERTIES OUTPUT_NAME "golf++-tests")
    target_link_libraries(golfpp-tests PRIVATE vcr-golf-physics vcr-golf-game)
    target_include_directories(golfpp-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_include_directories(golfpp-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vendor)
endif()
