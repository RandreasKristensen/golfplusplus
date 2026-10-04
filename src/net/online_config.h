#pragma once

// Where online play connects and how it signs in: assets/online.json, with
// the server and database overridable from the command line or environment
// for development and self-hosted servers. None of it is secret: the game is
// a public client (no client secret), and PKCE protects the sign-in. At
// SpacetimeAuth that is a client with "Private Client" and "Web Application"
// off and the redirect URI http://127.0.0.1/callback (any port then matches).

#include <optional>
#include <string>

inline constexpr const char* online_config_path = "online.json";

struct online_config {
    std::string server_uri;  // e.g. https://maincloud.spacetimedb.com
    std::string database;
    std::string auth_issuer;
    std::string auth_client_id;  // empty until the owner registers the game's client
    std::string auth_scopes;     // space-separated
    std::string auth_authorization_endpoint;
    std::string auth_token_endpoint;
    // Connect without signing in: only servers with allow_anonymous accept it.
    bool anonymous = false;
};

// Every field is required (auth_client_id may be empty).
std::optional<online_config> parse_online_config_from_text(const std::string& text);

// Development and self-hosting overrides: a non-empty server or database
// replaces the file's.
online_config with_overrides(online_config config,
                             const std::string& server_uri,
                             const std::string& database,
                             bool anonymous);
