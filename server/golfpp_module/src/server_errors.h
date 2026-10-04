#pragma once

// The ids reducers fail with. They are stable: the client turns them into
// text from its string table, so a new id needs a string there too.

// Accounts
inline constexpr const char* error_server_unavailable = "server_unavailable";
inline constexpr const char* error_owner_only = "owner_only";
inline constexpr const char* error_internal_only = "internal_only";
inline constexpr const char* error_invalid_config = "invalid_config";
inline constexpr const char* error_linking_off = "linking_off";
inline constexpr const char* error_login_rejected = "login_rejected";
inline constexpr const char* error_not_signed_in = "not_signed_in";
inline constexpr const char* error_signed_in_elsewhere = "signed_in_elsewhere";
inline constexpr const char* error_needs_name = "needs_name";
inline constexpr const char* error_has_name = "has_name";
inline constexpr const char* error_invalid_name = "invalid_name";
inline constexpr const char* error_name_taken = "name_taken";
inline constexpr const char* error_too_many_codes = "too_many_codes";

// Rooms and groups
inline constexpr const char* error_unknown_course = "unknown_course";
inline constexpr const char* error_not_in_room = "not_in_room";
inline constexpr const char* error_unknown_group = "unknown_group";
inline constexpr const char* error_group_full = "group_full";
inline constexpr const char* error_in_group = "in_group";
inline constexpr const char* error_not_in_group = "not_in_group";

// Playing
inline constexpr const char* error_wrong_zone = "wrong_zone";
inline constexpr const char* error_too_fast = "too_fast";
inline constexpr const char* error_invalid_motion = "invalid_motion";
inline constexpr const char* error_not_in_hub = "not_in_hub";
inline constexpr const char* error_not_on_hole = "not_on_hole";
inline constexpr const char* error_unknown_hole = "unknown_hole";
inline constexpr const char* error_hole_played = "hole_played";
inline constexpr const char* error_too_far = "too_far";
inline constexpr const char* error_wrong_stroke = "wrong_stroke";
inline constexpr const char* error_unknown_club = "unknown_club";
inline constexpr const char* error_invalid_shot = "invalid_shot";
inline constexpr const char* error_cigarette_out = "cigarette_out";
inline constexpr const char* error_unknown_collectible = "unknown_collectible";
inline constexpr const char* error_not_available = "not_available";
inline constexpr const char* error_unknown_emote = "unknown_emote";
inline constexpr const char* error_too_soon = "too_soon";
