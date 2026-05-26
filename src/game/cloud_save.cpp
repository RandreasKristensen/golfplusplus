#include "game/cloud_save.h"

#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>

namespace {
constexpr std::uint64_t fnv_offset_basis = 14695981039346656037ull;
constexpr std::uint64_t fnv_prime = 1099511628211ull;
}

cloud_sync_result offline_cloud_save_client::sync(const cloud_sync_request& request) {
    cloud_sync_result result;
    result.status = cloud_sync_status::offline;
    result.remote_revision = request.profile.client_revision;
    result.message = "cloud sync unavailable: offline local profile";
    return result;
}

std::string save_payload_hash(const save_data& save) {
    const std::string payload = save_data_to_json(save);
    std::uint64_t hash = fnv_offset_basis;
    for (const unsigned char c : payload) {
        hash ^= static_cast<std::uint64_t>(c);
        hash *= fnv_prime;
    }

    std::ostringstream stream;
    stream << std::hex << std::setw(16) << std::setfill('0') << hash;
    return stream.str();
}
