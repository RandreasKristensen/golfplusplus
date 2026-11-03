#include "game/content_hash.h"

#include <cstddef>

namespace {
bool simulation_file(const std::string& path) {
    if (path.size() < 5 || path.compare(path.size() - 5, 5, ".json") != 0) {
        return false;
    }
    for (const char* folder : simulation_content_folders) {
        const std::string prefix = std::string(folder) + "/";
        if (path.compare(0, prefix.size(), prefix) == 0) {
            return true;
        }
    }
    return false;
}
}

std::uint64_t simulation_content_hash(const content_files& files) {
    std::uint64_t hash = 14695981039346656037ull;  // FNV-1a, fixed byte order
    const auto byte = [&hash](const unsigned char value) { hash = (hash ^ value) * 1099511628211ull; };
    const auto text = [&byte](const std::string_view value) {
        std::uint64_t length = 0;
        for (std::size_t i = 0; i < value.size(); ++i) {
            length += value[i] != '\r' || i + 1 == value.size() || value[i + 1] != '\n';
        }
        // Lengths frame both paths and texts, including embedded NUL bytes.
        for (unsigned shift = 0; shift < 64; shift += 8) {
            byte(static_cast<unsigned char>(length >> shift));
        }
        for (std::size_t i = 0; i < value.size(); ++i) {
            if (value[i] != '\r' || i + 1 == value.size() || value[i + 1] != '\n') {
                byte(static_cast<unsigned char>(value[i]));
            }
        }
    };
    for (const auto& [path, contents] : files) {
        if (simulation_file(path)) {
            text(path);
            text(contents);
        }
    }
    return hash;
}
