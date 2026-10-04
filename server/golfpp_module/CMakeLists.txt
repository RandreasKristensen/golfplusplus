cmake_minimum_required(VERSION 3.20)
project(golfpp_module LANGUAGES C CXX)

# The golf++ server: a SpacetimeDB module built to WASM with Emscripten
# (`spacetime build`). It compiles the game's own physics, shot simulation,
# loaders and progress rules from ../../src, and every content file it reads
# is compiled in (embed_content.cmake).

# ------------------------------------------------------------------------------
# SpacetimeDB C++ bindings (from the `spacetime init --lang cpp` template)
#
# Default: Fetches from GitHub using SPACETIMEDB_CPP_VERSION
#   - MAJOR.MINOR (e.g., "1.12") → fetches release/MAJOR.MINOR branch, gets latest patch
#   - MAJOR.MINOR.PATCH (e.g., "1.12.0") → fetches vMAJOR.MINOR.PATCH tag, exact version
#
# Advanced options:
#   - SPACETIMEDB_CPP_REF: Override Git ref directly (e.g., "release/latest", "main", "v1.12.0")
#   - SPACETIMEDB_CPP_DIR: Use a local bindings clone (env var or cache variable).
#     The directory should contain the bindings' CMakeLists.txt at its root.
# ------------------------------------------------------------------------------

set(SPACETIMEDB_CPP_VERSION "2.10.2" CACHE STRING "Version selector: MAJOR.MINOR (uses release/MAJOR.MINOR) or MAJOR.MINOR.PATCH (uses tag vMAJOR.MINOR.PATCH)")
set(SPACETIMEDB_CPP_REF "" CACHE STRING "Override Git ref directly (e.g. release/1.0, release/latest, v1.0.0)")
set(SPACETIMEDB_CPP_DIR "" CACHE PATH "Path to a local clone of SpacetimeDB C++ bindings (overrides FetchContent)")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
  set(CMAKE_CXX_FLAGS_RELEASE "-O2")
endif()

if(NOT SPACETIMEDB_CPP_DIR AND DEFINED ENV{SPACETIMEDB_CPP_DIR})
  set(SPACETIMEDB_CPP_DIR "$ENV{SPACETIMEDB_CPP_DIR}")
endif()

if (SPACETIMEDB_CPP_REF STREQUAL "")
  if (SPACETIMEDB_CPP_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    set(SPACETIMEDB_CPP_REF "v${SPACETIMEDB_CPP_VERSION}")
  elseif (SPACETIMEDB_CPP_VERSION MATCHES "^[0-9]+\\.[0-9]+$")
    set(SPACETIMEDB_CPP_REF "release/${SPACETIMEDB_CPP_VERSION}")
  else()
    message(FATAL_ERROR "SPACETIMEDB_CPP_VERSION must be MAJOR.MINOR or MAJOR.MINOR.PATCH")
  endif()
endif()

if(SPACETIMEDB_CPP_DIR)
  message(STATUS "Using local SpacetimeDB C++ bindings: ${SPACETIMEDB_CPP_DIR}")
  add_subdirectory("${SPACETIMEDB_CPP_DIR}" "${CMAKE_BINARY_DIR}/_deps/spacetimedb_cpp-build")
else()
  include(FetchContent)
  FetchContent_Declare(
    spacetimedb_cpp_sdk
    GIT_REPOSITORY https://github.com/clockworklabs/spacetimedb-bindings-cpp.git
    GIT_TAG ${SPACETIMEDB_CPP_REF}
  )
  FetchContent_MakeAvailable(spacetimedb_cpp_sdk)
endif()

# ------------------------------------------------------------------------------
# Shared game code and content
# ------------------------------------------------------------------------------

get_filename_component(GOLFPP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

# GLM is header-only. Its directory is searched after the system headers
# (-idirafter), so a MinGW include directory cannot shadow Emscripten's libc.
find_path(GOLFPP_GLM_INCLUDE_DIR glm/glm.hpp
  HINTS "$ENV{GLM_INCLUDE_DIR}" "C:/msys64/mingw64/include" "/usr/include" "/usr/local/include" "/opt/homebrew/include"
  NO_CMAKE_FIND_ROOT_PATH)
if (NOT GOLFPP_GLM_INCLUDE_DIR)
  message(FATAL_ERROR "GLM not found: set GOLFPP_GLM_INCLUDE_DIR to the directory that holds glm/glm.hpp")
endif()

set(GOLFPP_SHARED_SOURCES
  src/physics/ball_physics.cpp
  src/physics/collision.cpp
  src/physics/flight_model.cpp
  src/physics/ground_contact.cpp
  src/physics/ground_mesh.cpp
  src/physics/terrain.cpp
  src/physics/tree_collision.cpp
  src/physics/wind.cpp
  src/game/club_loader.cpp
  src/game/course_loader.cpp
  src/game/course_world_loader.cpp
  src/game/hole_loader.cpp
  src/game/json_util.cpp
  src/game/pixel_font_data.cpp
  src/game/play_area.cpp
  src/game/progress_rules.cpp
  src/game/progression.cpp
  src/game/reward_rules.cpp
  src/game/round_state.cpp
  src/game/shot_simulation.cpp
  src/game/tuning_loader.cpp
  src/game/utf8.cpp
)
list(TRANSFORM GOLFPP_SHARED_SOURCES PREPEND "${GOLFPP_ROOT}/")

# Every JSON file of these asset directories is compiled in.
set(GOLFPP_CONTENT_DIRECTORIES holes courses course_worlds clubs progression tuning fonts)
set(GOLFPP_ASSET_ROOT "${GOLFPP_ROOT}/assets")
set(GOLFPP_CONTENT_FILES "")
foreach(directory IN LISTS GOLFPP_CONTENT_DIRECTORIES)
  file(GLOB files CONFIGURE_DEPENDS "${GOLFPP_ASSET_ROOT}/${directory}/*.json")
  list(APPEND GOLFPP_CONTENT_FILES ${files})
endforeach()
set(GOLFPP_EMBEDDED_SOURCE "${CMAKE_BINARY_DIR}/embedded_content.cpp")
add_custom_command(
  OUTPUT "${GOLFPP_EMBEDDED_SOURCE}"
  COMMAND "${CMAKE_COMMAND}"
    "-DASSET_ROOT=${GOLFPP_ASSET_ROOT}"
    "-DDIRECTORIES=${GOLFPP_CONTENT_DIRECTORIES}"
    "-DOUTPUT=${GOLFPP_EMBEDDED_SOURCE}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/embed_content.cmake"
  DEPENDS ${GOLFPP_CONTENT_FILES} "${CMAKE_CURRENT_SOURCE_DIR}/embed_content.cmake"
  COMMENT "Embedding game content"
  VERBATIM)

add_executable(lib
  src/account_rules.cpp
  src/lib.cpp
  src/module_cache.cpp
  src/play_rules.cpp
  src/server_content.cpp
  ${GOLFPP_SHARED_SOURCES}
  "${GOLFPP_EMBEDDED_SOURCE}"
)
target_include_directories(lib PRIVATE "${GOLFPP_ROOT}/src" src)
target_include_directories(lib SYSTEM PRIVATE "${GOLFPP_ROOT}/vendor")
# No FMA fusing, as in the game's build: shots must land where the game puts them.
target_compile_options(lib PRIVATE -ffp-contract=off "SHELL:-idirafter \"${GOLFPP_GLM_INCLUDE_DIR}\"")
target_link_libraries(lib PRIVATE spacetimedb_cpp_library)

if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
  set(EXPORTED_FUNCS "['_malloc','_free','___describe_module__','___call_reducer__','___call_view__','___call_view_anon__','___call_procedure__']")

  # Applied to both the module and the bindings, so everything sees the same flags.
  target_compile_options(lib PRIVATE -fno-exceptions -O2 -g0)
  target_compile_options(spacetimedb_cpp_library PRIVATE -fno-exceptions -O2 -g0)

  # A course's terrain is the largest thing the module holds (module_cache.h),
  # so memory may grow past the template's fixed 16 MB.
  target_link_options(lib PRIVATE
    "SHELL:-sSTANDALONE_WASM=1"
    "SHELL:-sWASM=1"
    "SHELL:--no-entry"
    "SHELL:-sEXPORTED_FUNCTIONS=${EXPORTED_FUNCS}"
    "SHELL:-sERROR_ON_UNDEFINED_SYMBOLS=1"
    "SHELL:-sFILESYSTEM=0"
    "SHELL:-sDISABLE_EXCEPTION_CATCHING=1"
    "SHELL:-sALLOW_MEMORY_GROWTH=1"
    "SHELL:-sINITIAL_MEMORY=16MB"
    "SHELL:-sSUPPORT_LONGJMP=0"
    "SHELL:-sSUPPORT_ERRNO=0"
    "SHELL:-std=c++20"
    "SHELL:-O2"
    "SHELL:-g0"
  )

  set_target_properties(lib PROPERTIES OUTPUT_NAME "lib" SUFFIX ".wasm")
endif()
