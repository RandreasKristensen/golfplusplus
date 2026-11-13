#pragma once

// Compatibility fingerprint of the JSON the server embeds and the game reads.
// Paths are relative to assets/, sorted by the map. CRLF and LF agree; every
// other byte matters. This detects different content, not a dishonest client.

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>

using content_files = std::map<std::string, std::string_view>;

// Keep the module's GOLFPP_CONTENT_PATTERNS in step with these folders.
inline constexpr std::array<const char*, 7> simulation_content_folders = {
    "play/tuning", "play/progression", "play/items", "interface/fonts", "world/courses", "world/props", "world/hub"};

std::uint64_t simulation_content_hash(const content_files& files);
