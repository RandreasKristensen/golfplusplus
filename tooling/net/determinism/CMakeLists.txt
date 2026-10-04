cmake_minimum_required(VERSION 3.20)
project(golfpp_golden_shots LANGUAGES CXX)

# golden_shots_main.cpp with the game's shot code, built the way the server
# module builds it (no FMA fusing, -O2). check_determinism.ps1 configures this
# once natively and once with emcmake (run under node with NODERAWFS).

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if (NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

get_filename_component(GOLFPP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)

find_path(GOLFPP_GLM_INCLUDE_DIR glm/glm.hpp
  HINTS "$ENV{GLM_INCLUDE_DIR}" "C:/msys64/mingw64/include" "/usr/include" "/usr/local/include" "/opt/homebrew/include"
  NO_CMAKE_FIND_ROOT_PATH)
if (NOT GOLFPP_GLM_INCLUDE_DIR)
  message(FATAL_ERROR "GLM not found: set GOLFPP_GLM_INCLUDE_DIR to the directory that holds glm/glm.hpp")
endif()

set(sources
  tooling/net/determinism/golden_shots_main.cpp
  server/golfpp_module/src/server_content.cpp
  src/physics/ball_physics.cpp
  src/physics/collision.cpp
  src/physics/flight_model.cpp
  src/physics/ground_contact.cpp
  src/physics/ground_mesh.cpp
  src/physics/terrain.cpp
  src/physics/tree_collision.cpp
  src/physics/wind.cpp
  src/game/club_loader.cpp
  src/game/content_files.cpp
  src/game/course_loader.cpp
  src/game/course_world_loader.cpp
  src/game/hole_loader.cpp
  src/game/json_util.cpp
  src/game/pixel_font_data.cpp
  src/game/play_area.cpp
  src/game/reward_rules.cpp
  src/game/shot_simulation.cpp
  src/game/tuning_loader.cpp
  src/game/utf8.cpp
)
list(TRANSFORM sources PREPEND "${GOLFPP_ROOT}/")

add_executable(golden_shots ${sources})
target_include_directories(golden_shots PRIVATE "${GOLFPP_ROOT}/src" "${GOLFPP_ROOT}/server/golfpp_module/src")
target_include_directories(golden_shots SYSTEM PRIVATE "${GOLFPP_ROOT}/vendor")
target_compile_options(golden_shots PRIVATE -O2 -ffp-contract=off "SHELL:-idirafter \"${GOLFPP_GLM_INCLUDE_DIR}\"")

if (CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
  target_compile_options(golden_shots PRIVATE -fno-exceptions)
  target_link_options(golden_shots PRIVATE "SHELL:-sNODERAWFS=1" "SHELL:-sALLOW_MEMORY_GROWTH=1" "SHELL:-sEXIT_RUNTIME=1")
  set_target_properties(golden_shots PROPERTIES SUFFIX ".js")
endif()
