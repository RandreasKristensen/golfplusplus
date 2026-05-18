cmake_minimum_required(VERSION 3.25)
project(vcr-golf LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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
