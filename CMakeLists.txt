cmake_minimum_required(VERSION 3.25)
project(golfpp VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(GOLFPP_BUILD_TESTS "Build tests" OFF)
# Online play: the Rust bridge to the SpacetimeDB server (net/client_bridge),
# built with cargo. Off, the game is offline only and needs no Rust.
option(GOLFPP_NET "Build online play (needs Rust and cargo)" ON)
# Players' builds (tooling/release) open no console window; startup errors
# are then only in SDL's log, so development builds keep the console.
option(GOLFPP_NO_CONSOLE "Build golf++ without a console window (Windows)" OFF)

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
# Shots must land in the same place here and in the server module
# (server/golfpp_module), so floating point is never fused into FMAs.
add_library(golfpp-determinism INTERFACE)
if (NOT MSVC)
    target_compile_options(golfpp-determinism INTERFACE -ffp-contract=off)
endif()

function(golfpp_library name)
    add_library(${name} STATIC ${ARGN})
    target_include_directories(${name} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(${name} PUBLIC golfpp-third-party PRIVATE golfpp-warnings golfpp-determinism)
endfunction()

# Pure functional zone: see AGENTS.md.
golfpp_library(golfpp-physics
    src/physics/ball_physics.cpp
    src/physics/collision.cpp
    src/physics/flight_model.cpp
    src/physics/ground_contact.cpp
    src/physics/ground_mesh.cpp
    src/physics/material_zone.cpp
    src/physics/terrain.cpp
    src/physics/fence_collision.cpp
    src/physics/tree_collision.cpp
    src/physics/wind.cpp
)

golfpp_library(golfpp-profiling
    src/profiling/profiling.cpp
)

golfpp_library(golfpp-game
    src/game/asset_resolver.cpp
    src/game/club_loader.cpp
    src/game/content_files.cpp
    src/game/course_loader.cpp
    src/game/course_session.cpp
    src/game/course_world_loader.cpp
    src/game/game_content.cpp
    src/game/game_state.cpp
    src/game/group_round.cpp
    src/game/hole_loader.cpp
    src/game/hole_sign.cpp
    src/game/tee_box.cpp
    src/game/json_util.cpp
    src/game/mode_dispatch.cpp
    src/game/online_play.cpp
    src/game/motion_sync.cpp
    src/game/pixel_font_data.cpp
    src/game/play_area.cpp
    src/game/progress_rules.cpp
    src/game/progression.cpp
    src/game/remote_players.cpp
    src/game/reward_rules.cpp
    src/game/round_state.cpp
    src/game/save_data.cpp
    src/game/save_manager.cpp
    src/game/settings.cpp
    src/game/scorecard.cpp
    src/game/shot_simulation.cpp
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
    src/renderer/bmp_image.cpp
    src/renderer/camera_local.cpp
    src/renderer/camera_transition.cpp
    src/renderer/cart_batch.cpp
    src/renderer/control_icons.cpp
    src/renderer/course_map_fill.cpp
    src/renderer/course_map_overlay.cpp
    src/renderer/emote_props.cpp
    src/renderer/frustum.cpp
    src/renderer/fence_batch.cpp
    src/renderer/hole_sign_batch.cpp
    src/renderer/water_batch.cpp
    src/renderer/hole_sign_face.cpp
    src/renderer/hud_overlay.cpp
    src/renderer/menu_overlay.cpp
    src/renderer/online_overlay.cpp
    src/renderer/overlay_batch.cpp
    src/renderer/overlay_raster.cpp
    src/renderer/pixel_font.cpp
    src/renderer/primitive_mesh.cpp
    src/renderer/render_mesh_chunks.cpp
    src/renderer/remote_avatar_batch.cpp
    src/renderer/render_tree.cpp
    src/renderer/scorecard_overlay.cpp
    src/renderer/terrain_palette.cpp
    src/renderer/terrain_render_mesh.cpp
    src/renderer/text_input.cpp
    src/renderer/ui_rect.cpp
    src/renderer/world_marker_batch.cpp
)
target_link_libraries(golfpp-render-data PUBLIC golfpp-game)

# The game's side of online play (src/net): plain C++ over the bridge's C
# API. The game links the real bridge; tests link a fake one.
golfpp_library(golfpp-net
    src/net/net_client.cpp
    src/net/online_config.cpp
    src/net/room_rows.cpp
)
target_link_libraries(golfpp-net PUBLIC golfpp-game)

if (GOLFPP_NET)
    find_program(GOLFPP_CARGO cargo REQUIRED)
    set(GOLFPP_BRIDGE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/net/client_bridge")
    set(GOLFPP_BRIDGE_TARGET_DIR "${CMAKE_BINARY_DIR}/client_bridge")
    # MinGW builds link the GNU Rust target; other toolchains use cargo's default.
    if (MINGW)
        set(GOLFPP_BRIDGE_RUST_TARGET x86_64-pc-windows-gnu)
        set(GOLFPP_BRIDGE_LIBRARY "${GOLFPP_BRIDGE_TARGET_DIR}/${GOLFPP_BRIDGE_RUST_TARGET}/release/libgolfpp_client_bridge.a")
        set(GOLFPP_BRIDGE_TARGET_ARGS --target ${GOLFPP_BRIDGE_RUST_TARGET})
    else()
        set(GOLFPP_BRIDGE_LIBRARY "${GOLFPP_BRIDGE_TARGET_DIR}/release/${CMAKE_STATIC_LIBRARY_PREFIX}golfpp_client_bridge${CMAKE_STATIC_LIBRARY_SUFFIX}")
        set(GOLFPP_BRIDGE_TARGET_ARGS)
    endif()
    file(GLOB_RECURSE GOLFPP_BRIDGE_SOURCES CONFIGURE_DEPENDS "${GOLFPP_BRIDGE_DIR}/src/*.rs")
    add_custom_command(
        OUTPUT "${GOLFPP_BRIDGE_LIBRARY}"
        COMMAND "${GOLFPP_CARGO}" build --release --manifest-path "${GOLFPP_BRIDGE_DIR}/Cargo.toml"
                --target-dir "${GOLFPP_BRIDGE_TARGET_DIR}" ${GOLFPP_BRIDGE_TARGET_ARGS}
        # cargo leaves an unchanged library's time alone; touch it so this
        # step does not run again on every build.
        COMMAND "${CMAKE_COMMAND}" -E touch "${GOLFPP_BRIDGE_LIBRARY}"
        DEPENDS ${GOLFPP_BRIDGE_SOURCES} "${GOLFPP_BRIDGE_DIR}/Cargo.toml" "${GOLFPP_BRIDGE_DIR}/Cargo.lock"
        COMMENT "Building the online client bridge (cargo)"
        USES_TERMINAL
        VERBATIM)
    add_custom_target(golfpp-client-bridge-build DEPENDS "${GOLFPP_BRIDGE_LIBRARY}")
    add_library(golfpp-client-bridge STATIC IMPORTED)
    set_target_properties(golfpp-client-bridge PROPERTIES IMPORTED_LOCATION "${GOLFPP_BRIDGE_LIBRARY}")
    add_dependencies(golfpp-client-bridge golfpp-client-bridge-build)
    if (WIN32)
        # What the Rust standard library, tokio, rustls and keyring need.
        set_property(TARGET golfpp-client-bridge PROPERTY INTERFACE_LINK_LIBRARIES
            ws2_32 userenv bcrypt ntdll secur32 crypt32 ncrypt advapi32 ole32 oleaut32 propsys runtimeobject credui)
    endif()
endif()

# SDL-free app logic: startup options, menu flow, audio manifest.
golfpp_library(golfpp-app-logic
    src/audio/audio_manifest.cpp
    src/core/input.cpp
    src/core/key_bindings.cpp
    src/core/menu_controls.cpp
    src/core/online_menus.cpp
    src/core/render_frame.cpp
    src/core/render_online.cpp
    src/core/settings_menu.cpp
    src/core/startup_flow.cpp
    src/core/startup_options.cpp
)
target_link_libraries(golfpp-app-logic PUBLIC golfpp-render-data)
# The version the main menu shows: major.minor, and the patch when there is one.
set(GOLFPP_VERSION_SHOWN "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}")
if (PROJECT_VERSION_PATCH)
    string(APPEND GOLFPP_VERSION_SHOWN ".${PROJECT_VERSION_PATCH}")
endif()
target_compile_definitions(golfpp-app-logic PRIVATE GOLFPP_VERSION="${GOLFPP_VERSION_SHOWN}")

# app's side of online play: between the menus and the network client.
golfpp_library(golfpp-online-session
    src/core/online_session.cpp
)
target_link_libraries(golfpp-online-session PUBLIC golfpp-app-logic golfpp-net)

add_executable(golfpp
    src/audio/audio_engine.cpp
    src/core/app.cpp
    src/core/event_loop.cpp
    src/core/main.cpp
    src/core/window.cpp
    src/renderer/backdrop_pass.cpp
    src/renderer/dynamic_buffer.cpp
    src/renderer/framebuffer.cpp
    src/renderer/gl_loader.cpp
    src/renderer/gl_timer.cpp
    src/renderer/fence_renderer.cpp
    src/renderer/hole_sign_renderer.cpp
    src/renderer/water_renderer.cpp
    src/renderer/overlay_pass.cpp
    src/renderer/renderer.cpp
    src/renderer/shader.cpp
    src/renderer/texture.cpp
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
if (WIN32 AND GOLFPP_NO_CONSOLE)
    set_target_properties(golfpp PROPERTIES WIN32_EXECUTABLE ON)
endif()
if (GOLFPP_NET)
    # The bridge after golfpp-net, which calls it: static libraries link in order.
    target_link_libraries(golfpp PRIVATE golfpp-online-session golfpp-net golfpp-client-bridge)
    target_compile_definitions(golfpp PRIVATE GOLFPP_NET=1)
endif()

# `cmake --install <build> --prefix <dir>`: everything a player's PC needs
# to run the game from <dir>, which tooling/release/build_release.ps1 packs.
# The DLLs are whichever the exe loads from outside Windows, found at install
# time next to the compiler (MinGW's runtime, SDL2, SDL2_mixer and its codecs).
if (POLICY CMP0207)
    cmake_policy(SET CMP0207 NEW)  # match the DLLs' paths with forward slashes
endif()
get_filename_component(GOLFPP_TOOLCHAIN_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
install(TARGETS golfpp RUNTIME_DEPENDENCY_SET golfpp-runtime RUNTIME DESTINATION .)
install(RUNTIME_DEPENDENCY_SET golfpp-runtime
    DIRECTORIES "${GOLFPP_TOOLCHAIN_BIN}"
    PRE_EXCLUDE_REGEXES "^api-ms-" "^ext-ms-"
    POST_EXCLUDE_REGEXES "/[Ww][Ii][Nn][Dd][Oo][Ww][Ss]/"
    DESTINATION .)
install(DIRECTORY assets DESTINATION .)

if (GOLFPP_BUILD_TESTS)
    # The server module's plain C++ (server/golfpp_module/src), without the
    # SpacetimeDB parts, so tests check it natively against the game.
    add_library(golfpp-server-rules STATIC
        server/golfpp_module/src/account_rules.cpp
        server/golfpp_module/src/play_rules.cpp
        server/golfpp_module/src/server_content.cpp
    )
    target_include_directories(golfpp-server-rules PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/server/golfpp_module/src)
    target_link_libraries(golfpp-server-rules PUBLIC golfpp-game PRIVATE golfpp-warnings golfpp-determinism)

    add_executable(golfpp-tests
        tests/account_rules_tests.cpp
        tests/audio_manifest_tests.cpp
        tests/camera_transition_tests.cpp
        tests/content_tests.cpp
        tests/course_map_fill_tests.cpp
        tests/course_tests.cpp
        tests/dynamic_buffer_tests.cpp
        tests/fence_tests.cpp
        tests/gameplay_tests.cpp
        tests/hazard_tests.cpp
        tests/hole_sign_tests.cpp
        tests/tee_box_tests.cpp
        tests/material_zone_tests.cpp
        tests/fake_stdb_bridge.cpp
        tests/mode_dispatch_tests.cpp
        tests/net_client_tests.cpp
        tests/online_menus_tests.cpp
        tests/online_play_tests.cpp
        tests/online_session_tests.cpp
        tests/overlay_batch_tests.cpp
        tests/play_rules_tests.cpp
        tests/physics_tests.cpp
        tests/profiling_tests.cpp
        tests/progression_tests.cpp
        tests/render_culling_tests.cpp
        tests/render_frame_tests.cpp
        tests/server_content_tests.cpp
        tests/shader_tests.cpp
        tests/shot_simulation_tests.cpp
        tests/settings_tests.cpp
        tests/startup_flow_tests.cpp
        tests/startup_options_tests.cpp
        tests/text_assets_tests.cpp
        tests/text_input_tests.cpp
        tests/text_layout_tests.cpp
        tests/utf8_tests.cpp
        tests/world_marker_tests.cpp
    )
    set_target_properties(golfpp-tests PROPERTIES OUTPUT_NAME "golf++-tests")
    target_link_libraries(golfpp-tests PRIVATE golfpp-online-session golfpp-server-rules)
    target_include_directories(golfpp-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures" GOLFPP_TEST_FIXTURES_DIR)
    file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/src" GOLFPP_SOURCE_DIR)
    target_compile_definitions(golfpp-tests PRIVATE
        GOLFPP_ASSETS_DIR="${GOLFPP_ASSETS_DIR}"
        GOLFPP_TEST_FIXTURES_DIR="${GOLFPP_TEST_FIXTURES_DIR}"
        GOLFPP_SOURCE_DIR="${GOLFPP_SOURCE_DIR}"
    )
endif()
