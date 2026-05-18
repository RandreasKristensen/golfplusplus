cmake_minimum_required(VERSION 3.25)
project(vcr-golf LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(VCR_GOLF_BUILD_TESTS "Build tests" OFF)

set(VCR_GOLF_SOURCES
    core/main.cpp
    core/app.cpp
    core/window.cpp
    core/event_loop.cpp
)

add_executable(vcr-golf ${VCR_GOLF_SOURCES})

target_include_directories(vcr-golf PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(vcr-golf PRIVATE SDL_MAIN_HANDLED)

find_package(SDL2 REQUIRED)
target_link_libraries(vcr-golf PRIVATE SDL2::SDL2)

find_package(OpenGL REQUIRED)
target_link_libraries(vcr-golf PRIVATE OpenGL::GL)

set(GLAD_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/src/glad/glad.c)
if (EXISTS ${GLAD_SOURCE})
    target_sources(vcr-golf PRIVATE ${GLAD_SOURCE})
    target_include_directories(vcr-golf PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/glad)
    target_compile_definitions(vcr-golf PRIVATE VCR_GOLF_USE_GLAD=1)
endif()

set(VCR_GOLF_PHYSICS_SOURCES
    src/physics/ball_physics.cpp
    src/physics/flight_model.cpp
    src/physics/collision.cpp
    src/physics/wind.cpp
)

find_path(GLM_INCLUDE_DIR glm/vec3.hpp)

add_library(vcr-golf-physics EXCLUDE_FROM_ALL ${VCR_GOLF_PHYSICS_SOURCES})
target_include_directories(vcr-golf-physics PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${GLM_INCLUDE_DIR}
)

if (VCR_GOLF_BUILD_TESTS)
    if (NOT GLM_INCLUDE_DIR)
        message(FATAL_ERROR "GLM not found. Install it or set GLM_INCLUDE_DIR.")
    endif()

    add_executable(vcr-golf-tests tests/physics_tests.cpp)
    target_link_libraries(vcr-golf-tests PRIVATE vcr-golf-physics)
    target_include_directories(vcr-golf-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
endif()
