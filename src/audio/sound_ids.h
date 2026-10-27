#pragma once

// Ids of sounds that code plays, from assets/sound/sounds.json. A test checks
// every id here exists in the manifest with an existing file, so keep one
// constant per line in this exact form. Club hit sounds are data (each club's
// "hit_sound"), as is the sound an item's use plays (its "use"'s "sound").

inline constexpr const char* sound_ui_move = "ui_move";
inline constexpr const char* sound_ui_select = "ui_select";
inline constexpr const char* sound_ui_back = "ui_back";
inline constexpr const char* sound_swing_start = "swing_start";
inline constexpr const char* sound_ball_land_grass = "ball_land_grass";
inline constexpr const char* sound_ball_land_bunker = "ball_land_bunker";
inline constexpr const char* sound_ball_splash = "ball_splash";
inline constexpr const char* sound_ball_tree_hit = "ball_tree_hit";
inline constexpr const char* sound_ball_pole_hit = "ball_pole_hit";
inline constexpr const char* sound_ball_board_hit = "ball_board_hit";
inline constexpr const char* sound_ball_flag_hit = "ball_flag_hit";
inline constexpr const char* sound_ball_person_hit = "ball_person_hit";
inline constexpr const char* sound_ball_ball_hit = "ball_ball_hit";
inline constexpr const char* sound_ball_cup_rattle = "ball_cup_rattle";
inline constexpr const char* sound_ball_leaves = "ball_leaves";
inline constexpr const char* sound_ball_cup = "ball_cup";
inline constexpr const char* sound_club_change = "club_change";
inline constexpr const char* sound_bag_move = "bag_move";
inline constexpr const char* sound_cart_start = "cart_start";
inline constexpr const char* sound_cart_drive_loop = "cart_drive_loop";
inline constexpr const char* sound_cart_crash = "cart_crash";
inline constexpr const char* sound_cart_tip = "cart_tip";
inline constexpr const char* sound_cart_sink = "cart_sink";
inline constexpr const char* sound_cart_righted = "cart_righted";
inline constexpr const char* sound_player_bump = "player_bump";
inline constexpr const char* sound_thunder_close = "thunder_close";
inline constexpr const char* sound_thunder_far = "thunder_far";
inline constexpr const char* sound_ambience_course_day = "ambience_course_day";
inline constexpr const char* sound_ambience_menu_vcr = "ambience_menu_vcr";
