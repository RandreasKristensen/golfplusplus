#pragma once

// Keys into the string table (assets/text/en.json) and names of text styles
// (assets/ui/text_styles.json). Tests check that every `text_*` id here exists
// in en.json and every `style_*` id exists in text_styles.json, so keep one
// constant per line in this exact form.
//
// Skill names are looked up as "skill.<skill_id>" (see skill_text_key).

#include <string>

// The browser page after signing in online (not drawn by the game)
constexpr const char* text_online_browser_signed_in = "online.browser.signed_in";
constexpr const char* text_online_browser_failed = "online.browser.failed";
// Taken out of an online round's room while still connected
constexpr const char* text_online_room_lost = "online.room_lost";
// The server's tee_in_use refusal, shown before asking when the tee is taken.
constexpr const char* text_online_tee_in_use = "online.error.tee_in_use";
// Starting a hole with the last holed ball still in its cup, offline and the
// server's ball_in_cup refusal.
constexpr const char* text_ball_in_cup = "online.error.ball_in_cup";

// Startup menus
constexpr const char* text_menu_loading = "menu.loading";
constexpr const char* text_menu_main_title = "menu.main.title";
constexpr const char* text_menu_main_subtitle = "menu.main.subtitle";
constexpr const char* text_menu_main_signed_in = "menu.main.signed_in";
constexpr const char* text_menu_main_signed_in_unnamed = "menu.main.signed_in_unnamed";
constexpr const char* text_menu_main_footer = "menu.main.footer";
constexpr const char* text_menu_main_play_online = "menu.main.play_online";
constexpr const char* text_menu_main_play_online_hint = "menu.main.play_online.hint";
constexpr const char* text_menu_main_play_offline = "menu.main.play_offline";
constexpr const char* text_menu_main_play_offline_hint = "menu.main.play_offline.hint";
constexpr const char* text_menu_main_link_login = "menu.main.link_login";
constexpr const char* text_menu_main_link_login_hint = "menu.main.link_login.hint";
constexpr const char* text_menu_main_sign_out = "menu.main.sign_out";
constexpr const char* text_menu_main_sign_out_hint = "menu.main.sign_out.hint";
constexpr const char* text_menu_main_help = "menu.main.help";
constexpr const char* text_menu_main_help_hint = "menu.main.help.hint";
constexpr const char* text_menu_main_settings = "menu.main.settings";
constexpr const char* text_menu_main_settings_hint = "menu.main.settings.hint";
constexpr const char* text_menu_main_quit = "menu.main.quit";
constexpr const char* text_menu_main_quit_hint = "menu.main.quit.hint";
constexpr const char* text_menu_offline_title = "menu.offline.title";
constexpr const char* text_menu_offline_subtitle = "menu.offline.subtitle";
constexpr const char* text_menu_offline_footer = "menu.offline.footer";
constexpr const char* text_menu_offline_play_course = "menu.offline.play_course";
constexpr const char* text_menu_offline_play_course_hint = "menu.offline.play_course.hint";
constexpr const char* text_menu_offline_play_hole = "menu.offline.play_hole";
constexpr const char* text_menu_offline_play_hole_hint = "menu.offline.play_hole.hint";
constexpr const char* text_menu_help_title = "menu.help.title";
constexpr const char* text_menu_help_subtitle = "menu.help.subtitle";
constexpr const char* text_menu_help_footer = "menu.help.footer";
constexpr const char* text_menu_hole_picker_title = "menu.hole_picker.title";
constexpr const char* text_menu_hole_picker_subtitle = "menu.hole_picker.subtitle";
constexpr const char* text_menu_hole_picker_footer = "menu.hole_picker.footer";
constexpr const char* text_menu_hole_picker_tile = "menu.hole_picker.tile";
constexpr const char* text_menu_course_picker_title = "menu.course_picker.title";
constexpr const char* text_menu_course_picker_subtitle = "menu.course_picker.subtitle";
constexpr const char* text_menu_course_picker_footer = "menu.course_picker.footer";
constexpr const char* text_menu_course_picker_tile = "menu.course_picker.tile";
constexpr const char* text_menu_online_course_picker_subtitle = "menu.online_course_picker.subtitle";
constexpr const char* text_menu_confirm_title = "menu.confirm.title";
constexpr const char* text_menu_confirm_yes = "menu.confirm.yes";
constexpr const char* text_menu_confirm_no = "menu.confirm.no";
constexpr const char* text_menu_confirm_settings = "menu.confirm.settings";
constexpr const char* text_menu_settings_title = "menu.settings.title";
constexpr const char* text_menu_settings_subtitle = "menu.settings.subtitle";
constexpr const char* text_menu_settings_footer = "menu.settings.footer";
constexpr const char* text_menu_settings_back = "menu.settings.back";
constexpr const char* text_menu_settings_back_hint = "menu.settings.back.hint";

// Online menus
constexpr const char* text_menu_online_title = "menu.online.title";
constexpr const char* text_menu_online_footer = "menu.online.footer";
constexpr const char* text_menu_login_subtitle = "menu.login.subtitle";
constexpr const char* text_menu_login_unavailable = "menu.login.unavailable";
constexpr const char* text_menu_login_needed = "menu.login.needed";
constexpr const char* text_menu_login_browser = "menu.login.browser";
constexpr const char* text_menu_login_signing_in = "menu.login.signing_in";
constexpr const char* text_menu_login_sign_in = "menu.login.sign_in";
constexpr const char* text_menu_login_sign_in_hint = "menu.login.sign_in.hint";
constexpr const char* text_menu_login_retry = "menu.login.retry";
constexpr const char* text_menu_login_retry_hint = "menu.login.retry.hint";
constexpr const char* text_menu_login_cancel = "menu.login.cancel";
constexpr const char* text_menu_login_cancel_hint = "menu.login.cancel.hint";
constexpr const char* text_menu_login_back = "menu.login.back";
constexpr const char* text_menu_login_back_hint = "menu.login.back.hint";
constexpr const char* text_menu_name_title = "menu.name.title";
constexpr const char* text_menu_name_subtitle = "menu.name.subtitle";
constexpr const char* text_menu_name_footer = "menu.name.footer";
constexpr const char* text_menu_name_waiting = "menu.name.waiting";
constexpr const char* text_menu_name_ok = "menu.name.ok";
constexpr const char* text_menu_name_ok_hint = "menu.name.ok.hint";
constexpr const char* text_menu_name_have_account = "menu.name.have_account";
constexpr const char* text_menu_name_have_account_hint = "menu.name.have_account.hint";
constexpr const char* text_menu_link_entry_title = "menu.link_entry.title";
constexpr const char* text_menu_link_entry_subtitle = "menu.link_entry.subtitle";
constexpr const char* text_menu_link_entry_footer = "menu.link_entry.footer";
constexpr const char* text_menu_link_entry_waiting = "menu.link_entry.waiting";
constexpr const char* text_menu_link_entry_ok = "menu.link_entry.ok";
constexpr const char* text_menu_link_entry_ok_hint = "menu.link_entry.ok.hint";
constexpr const char* text_menu_link_entry_back_hint = "menu.link_entry.back.hint";
constexpr const char* text_menu_link_code_title = "menu.link_code.title";
constexpr const char* text_menu_link_code_subtitle = "menu.link_code.subtitle";
constexpr const char* text_menu_link_code_footer = "menu.link_code.footer";
constexpr const char* text_menu_link_code_waiting = "menu.link_code.waiting";
constexpr const char* text_menu_link_code_expires = "menu.link_code.expires";
constexpr const char* text_menu_link_code_done = "menu.link_code.done";
constexpr const char* text_menu_link_code_done_hint = "menu.link_code.done.hint";
constexpr const char* text_menu_joining_subtitle = "menu.joining.subtitle";
constexpr const char* text_menu_joining_message = "menu.joining.message";
constexpr const char* text_menu_joining_cancel_hint = "menu.joining.cancel.hint";

// Controls help screen: one key per control, lines separated by '\n'
constexpr const char* text_help_arrows = "help.arrows";
constexpr const char* text_help_space = "help.space";
constexpr const char* text_help_left_shift = "help.left_shift";
constexpr const char* text_help_shift = "help.shift";
constexpr const char* text_help_enter = "help.enter";
constexpr const char* text_help_backspace = "help.backspace";
constexpr const char* text_help_retee = "help.retee";
constexpr const char* text_help_key_1 = "help.key_1";
constexpr const char* text_help_key_2 = "help.key_2";
constexpr const char* text_help_key_g = "help.key_g";
constexpr const char* text_controls_key_1 = "controls.key_1";
constexpr const char* text_controls_key_2 = "controls.key_2";
constexpr const char* text_controls_key_g = "controls.key_g";

// HUD
constexpr const char* text_hud_fps = "hud.fps";
constexpr const char* text_hud_power = "hud.power";
constexpr const char* text_hud_power_tick_min = "hud.power.tick_min";
constexpr const char* text_hud_power_tick_mid = "hud.power.tick_mid";
constexpr const char* text_hud_power_tick_max = "hud.power.tick_max";
constexpr const char* text_hud_cart = "hud.cart";
constexpr const char* text_hud_cart_drive = "hud.cart.drive";
constexpr const char* text_hud_cart_drift = "hud.cart.drift";
constexpr const char* text_hud_rangefinder = "hud.rangefinder";
constexpr const char* text_hud_xp_drop = "hud.xp_drop";
// A shot lost in water, as the ball goes back
constexpr const char* text_hud_water_penalty = "hud.water_penalty";
constexpr const char* text_hud_mode_offline = "hud.mode.offline";
constexpr const char* text_hud_mode_online = "hud.mode.online";
constexpr const char* text_hud_mode_reconnecting = "hud.mode.reconnecting";

// Hole signs at the tees
constexpr const char* text_hole_sign_number = "hole_sign.number";
constexpr const char* text_hole_sign_par = "hole_sign.par";
constexpr const char* text_hole_sign_length = "hole_sign.length";

// The course map (held up with Enter)
constexpr const char* text_course_map_hole_number = "course_map.hole_number";

// Skills panel
constexpr const char* text_skills_title = "skills.title";
constexpr const char* text_skills_header_name = "skills.header.name";
constexpr const char* text_skills_header_level = "skills.header.level";
constexpr const char* text_skills_header_xp = "skills.header.xp";
constexpr const char* text_skills_header_next = "skills.header.next";
constexpr const char* text_skills_hint = "skills.hint";

// Scorecard
constexpr const char* text_scorecard_title = "scorecard.title";
constexpr const char* text_scorecard_results_title = "scorecard.results_title";
constexpr const char* text_scorecard_header_hole = "scorecard.header.hole";
constexpr const char* text_scorecard_header_par = "scorecard.header.par";
constexpr const char* text_scorecard_header_score = "scorecard.header.score";
constexpr const char* text_scorecard_header_relative = "scorecard.header.relative";
constexpr const char* text_scorecard_group_title = "scorecard.group_title";
constexpr const char* text_scorecard_header_player = "scorecard.header.player";
constexpr const char* text_scorecard_header_thru = "scorecard.header.thru";
constexpr const char* text_scorecard_total = "scorecard.total";
constexpr const char* text_scorecard_results_hint = "scorecard.results_hint";
constexpr const char* text_scorecard_results_hint_online = "scorecard.results_hint_online";
constexpr const char* text_scorecard_hole_row = "scorecard.hole_row";
constexpr const char* text_scorecard_even = "scorecard.even";
constexpr const char* text_scorecard_over = "scorecard.over";
constexpr const char* text_scorecard_under = "scorecard.under";
constexpr const char* text_scorecard_waiting_for_group = "scorecard.waiting_for_group";

// Text styles
constexpr const char* style_title = "title";
constexpr const char* style_subtitle = "subtitle";
constexpr const char* style_footer = "footer";
constexpr const char* style_body = "body";
constexpr const char* style_input = "input";
constexpr const char* style_error = "error";
constexpr const char* style_link_code = "link_code";
constexpr const char* style_tile_label = "tile_label";
constexpr const char* style_tile_hint = "tile_hint";
constexpr const char* style_tile_label_small = "tile_label_small";
constexpr const char* style_tile_hint_small = "tile_hint_small";
constexpr const char* style_control_key = "control_key";
constexpr const char* style_control_key_down = "control_key_down";
constexpr const char* style_hud_label = "hud_label";
constexpr const char* style_hud_mode = "hud_mode";
constexpr const char* style_name_tag = "name_tag";
constexpr const char* style_name_tag_group = "name_tag_group";
constexpr const char* style_name_tag_friend = "name_tag_friend";
constexpr const char* style_hud_scale = "hud_scale";
constexpr const char* style_hud_club = "hud_club";
constexpr const char* style_cart_label = "cart_label";
constexpr const char* style_cart_drive = "cart_drive";
constexpr const char* style_cart_drift = "cart_drift";
constexpr const char* style_xp_drop = "xp_drop";
constexpr const char* style_rangefinder = "rangefinder";
constexpr const char* style_hole_sign_number = "hole_sign_number";
constexpr const char* style_hole_sign_detail = "hole_sign_detail";
constexpr const char* style_course_map_hole_number = "course_map_hole_number";
constexpr const char* style_panel_title = "panel_title";
constexpr const char* style_panel_header = "panel_header";
constexpr const char* style_panel_value = "panel_value";
constexpr const char* style_panel_hint = "panel_hint";
constexpr const char* style_scorecard_title = "scorecard_title";
constexpr const char* style_scorecard_subtitle = "scorecard_subtitle";
constexpr const char* style_scorecard_header = "scorecard_header";
constexpr const char* style_scorecard_row = "scorecard_row";
constexpr const char* style_scorecard_row_current = "scorecard_row_current";
constexpr const char* style_scorecard_row_pending = "scorecard_row_pending";
constexpr const char* style_scorecard_total = "scorecard_total";
constexpr const char* style_scorecard_hint = "scorecard_hint";
constexpr const char* style_debug_fps = "debug_fps";
constexpr const char* style_debug_profile = "debug_profile";

inline std::string skill_text_key(const std::string& skill_id) {
    return "skill." + skill_id;
}

// Online failures arrive as ids: the server's reducer errors
// (server/golfpp_module/src/server_errors.h) and the bridge's own
// (net_failure_* in game/net_types.h). Tests check each has a string.
inline std::string online_error_text_key(const std::string& error_id) {
    return "online.error." + error_id;
}

// What a link code redemption did (link_result_* in game/net_types.h, the
// my_link_status view).
inline std::string online_link_text_key(const std::string& result_id) {
    return "online.link." + result_id;
}
