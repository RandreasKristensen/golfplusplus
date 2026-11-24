#pragma once

// The game content compiled into the module: every file matching the content
// patterns of CMakeLists.txt, keyed by its path relative to assets/
// ("world/courses/x/holes/h01.json"). The files are one gzipped bundle
// (embedded_gzip, embedded_content_size bytes once inflated); each entry is
// where its file sits in the inflated bundle. The definitions are generated
// at build time by embed_content.cmake.

#include <cstddef>

struct embedded_file {
    const char* path;
    std::size_t offset;
    std::size_t size;
};

extern const unsigned char embedded_gzip[];
extern const std::size_t embedded_gzip_size;
extern const std::size_t embedded_content_size;
extern const embedded_file embedded_files[];
extern const std::size_t embedded_file_count;
