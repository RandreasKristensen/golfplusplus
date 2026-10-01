#include "game/asset_resolver.h"

#include <filesystem>
#include <system_error>

std::string resolve_asset_root(const std::string& executable_directory) {
    std::error_code error;
    if (!executable_directory.empty()) {
        const std::filesystem::path adjacent = std::filesystem::path(executable_directory) / "assets";
        if (std::filesystem::is_directory(adjacent, error)) {
            return adjacent.string();
        }
    }
    return GOLFPP_ASSETS_DIR;
}
