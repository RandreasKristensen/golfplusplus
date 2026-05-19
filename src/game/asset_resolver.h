#pragma once

#include <string>

std::string compile_time_asset_root();
std::string resolve_asset_root(const std::string& executable_path_or_directory);
