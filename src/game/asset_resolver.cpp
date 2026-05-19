#include "game/asset_resolver.h"

#include <filesystem>
#include <string>

std::string compile_time_asset_root() {
#ifdef VCR_GOLF_ASSETS_DIR
    return VCR_GOLF_ASSETS_DIR;
#else
    return "assets";
#endif
}

std::string resolve_asset_root(const std::string& executable_path_or_directory) {
    if (!executable_path_or_directory.empty()) {
        std::filesystem::path base(executable_path_or_directory);
        if (std::filesystem::is_regular_file(base)) {
            base = base.parent_path();
        }

        const std::filesystem::path adjacent_assets = base / "assets";
        if (std::filesystem::exists(adjacent_assets) && std::filesystem::is_directory(adjacent_assets)) {
            return adjacent_assets.string();
        }
    }

    const std::filesystem::path compiled(compile_time_asset_root());
    if (std::filesystem::exists(compiled) && std::filesystem::is_directory(compiled)) {
        return compiled.string();
    }

    return "assets";
}
