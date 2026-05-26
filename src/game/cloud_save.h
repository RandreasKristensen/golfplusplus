#pragma once

#include "game/save_data.h"
#include "game/save_manager.h"

#include <cstdint>
#include <optional>
#include <string>

enum class cloud_sync_status {
    offline,
    up_to_date,
    uploaded,
    downloaded,
    conflict,
    error
};

struct cloud_sync_request {
    save_data local_save;
    local_profile_metadata profile;
    bool local_dirty = false;
};

struct cloud_sync_result {
    cloud_sync_status status = cloud_sync_status::offline;
    std::optional<save_data> remote_save;
    std::uint64_t remote_revision = 0;
    std::string message;
};

class cloud_save_client {
public:
    virtual ~cloud_save_client() = default;
    virtual cloud_sync_result sync(const cloud_sync_request& request) = 0;
};

class offline_cloud_save_client final : public cloud_save_client {
public:
    cloud_sync_result sync(const cloud_sync_request& request) override;
};

std::string save_payload_hash(const save_data& save);
