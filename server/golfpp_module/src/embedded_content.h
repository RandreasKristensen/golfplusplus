#pragma once

// The game content compiled into the module: every JSON file of the content
// directories, keyed by its path relative to assets/ ("holes/x.json"). The
// definitions are generated at build time by embed_content.cmake.

#include <cstddef>
#include <string_view>

struct embedded_file {
    const char* path;
    std::string_view text;
};

extern const embedded_file embedded_files[];
extern const std::size_t embedded_file_count;
