#include "net/online_config.h"

#include "game/json_util.h"

#include <utility>

std::optional<online_config> parse_online_config_from_text(const std::string& text) {
    const std::optional<json> root = parse_json(text);
    if (!root || !root->is_object()) {
        return std::nullopt;
    }
    online_config config;
    const std::pair<const char*, std::string*> fields[] = {
        {"server_uri", &config.server_uri},
        {"database", &config.database},
        {"auth_issuer", &config.auth_issuer},
        {"auth_client_id", &config.auth_client_id},
        {"auth_scopes", &config.auth_scopes},
        {"auth_authorization_endpoint", &config.auth_authorization_endpoint},
        {"auth_token_endpoint", &config.auth_token_endpoint},
    };
    for (const auto& [key, field] : fields) {
        std::optional<std::string> value = json_string(*root, key);
        const bool may_be_empty = field == &config.auth_client_id;
        if (!value || (value->empty() && !may_be_empty)) {
            return std::nullopt;
        }
        *field = std::move(*value);
    }
    return config;
}

online_config with_overrides(online_config config,
                             const std::string& server_uri,
                             const std::string& database,
                             const bool anonymous) {
    if (!server_uri.empty()) {
        config.server_uri = server_uri;
    }
    if (!database.empty()) {
        config.database = database;
    }
    config.anonymous = anonymous;
    return config;
}
