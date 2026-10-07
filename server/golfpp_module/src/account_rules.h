#pragma once

// Rules for online accounts, as plain functions the module's reducers call
// and the native tests check: who may connect, what a player name may be,
// link codes, and rate limits. Errors are the ids in server_errors.h.

#include "game/game_tuning.h"
#include "game/net_types.h"
#include "game/pixel_font_data.h"
#include "game/server_errors.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// What redeem_link_code reports (the my_link_status view) is link_result_*
// in game/net_types.h: failures are not reducer errors, so the attempt still
// counts against the rate limit.

// --- Names -------------------------------------------------------------------

struct name_check {
    std::string name;   // trimmed, as shown
    std::string key;    // case-folded, for uniqueness
    std::string error;  // empty when the name is allowed
};

// Trimmed of outer spaces, the name must be name_min_length to
// name_max_length code points of letters, digits and single inner spaces,
// each with a glyph in `font`. The key folds case like to_upper_letter in
// game/utf8.h, so "Åse" and "åSE" are the same name.
name_check check_player_name(const std::string& requested, const pixel_font_data& font, const server_tuning& tuning);

// --- Link codes --------------------------------------------------------------

// The code's length and alphabet are in game/net_types.h: the client's code
// entry takes them too.

// A link code grants a whole account, so it must not be guessable. The
// module's own random numbers are seeded from the call's timestamp, so codes
// come from a keyed hash of `nonce` (time, account, connection) under the
// owner's secret link_secret (server_config, set with admin_set_config):
// without the secret, knowing the inputs does not give the code. Linking is
// off until the owner sets a secret at least this long.
inline constexpr std::size_t min_link_secret_length = 32;

// SipHash-2-4 (Aumasson and Bernstein) of `message` under the key k0, k1.
std::uint64_t sip_hash_24(std::uint64_t k0, std::uint64_t k1, const std::string& message);
// SipHash-2-4 of `message` under a key derived from `secret`.
std::uint64_t keyed_hash(const std::string& secret, const std::string& message);
// The code for `nonce` under `secret`.
std::string make_link_code(const std::string& secret, const std::string& nonce);
// A code from 40 bits of `bits`.
std::string link_code_from_bits(std::uint64_t bits);
// What the player typed, uppercased, without spaces or dashes.
std::string normalize_link_code(const std::string& typed);

// --- Rate limits -------------------------------------------------------------

// Uses counted from `start_micros` until `period` later.
struct rate_window {
    std::int64_t start_micros = 0;
    std::uint32_t count = 0;
};

// The window after one more use at `now_micros`, or nullopt when `limit`
// uses already fall within the current window. A window older than `period`
// starts over.
std::optional<rate_window> use_rate_window(const rate_window& window,
                                           std::int64_t now_micros,
                                           std::int64_t period_micros,
                                           std::uint32_t limit);

// --- Logins ------------------------------------------------------------------

enum class login_method {
    browser,
    anonymous
};

// "browser" or "anonymous", as stored in account_login.
const char* login_method_name(login_method method);

// An anonymous login is a guest: the game stores nothing to sign in with it
// again, so it lasts one connection. When it disconnects, its account and
// every row of it are deleted, which frees its name. A guest cannot link
// logins, as its account goes with it.
bool is_guest_login(const std::string& stored_method);

// The caller's token, as the host verified it.
struct login_claims {
    bool present = false;
    std::string issuer;
    std::vector<std::string> audience;
};

// The claims of a JWT payload (JSON): `aud` may be one string or several.
// Not present when the payload is not a JSON object.
login_claims parse_login_claims(const std::string& payload);

struct login_config {
    std::string auth_issuer;
    std::string auth_audience;  // our client id at the auth issuer
    bool allow_anonymous = false;
};

// Tokens from our issuer for our client are browser logins. Any other token
// is an anonymous login, accepted only with allow_anonymous or for the
// module's owner. nullopt: refuse the connection.
std::optional<login_method> check_login(const login_claims& claims, const login_config& config, bool is_owner);
