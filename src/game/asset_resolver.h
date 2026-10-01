#pragma once

#include <string>

// The assets/ folder: next to the executable when it exists (shipped builds),
// otherwise the source tree's assets/ baked in at build time (development).
std::string resolve_asset_root(const std::string& executable_directory);
