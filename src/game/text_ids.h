#pragma once

// Keys into the string table (assets/text/en.json) and names of text styles
// (assets/ui/text_styles.json). Tests check that every `text_*` id here exists
// in en.json and every `style_*` id exists in text_styles.json, so keep one
// constant per line in this exact form.
//
// Skill names are looked up as "skill.<skill_id>" (see skill_text_key).

#include <string>

// The browser page after signing in online (not drawn by the game)
inline constexpr const char* text_online_browser_signed_in = "online.browser.signed_in";
inline constexpr const char* text_online_browser_failed = "online.browser.failed";
// Taken out of an online round's room while still connected
inline constexpr const char* text_online_room_lost = "online.room_lost";
// The server's tee_in_use refusal, shown before asking when the tee is taken.
inline constexpr const char* text_online_tee_in_use = "online.error.tee_in_use";
// Starting a hole with the last holed ball still in its cup, offline and the
// server's ball_in_cup refusal.
inline constexpr const char* text_ball_in_cup = "online.error.ball_in_cup";

// Startup menus
inline constexpr const char* text_menu_loading = "menu.loading";
inline constexpr const char* text_menu_main_title = "menu.main.title";
inline constexpr const char* text_menu_main_subtitle = "menu.main.subtitle";
inline constexpr const char* text_menu_main_signed_in = "menu.main.signed_in";
inline constexpr const char* text_menu_main_signed_in_unnamed = "menu.main.signed_in_unnamed";
inline constexpr const char* text_menu_main_footer = "menu.main.footer";
inline constexpr const char* text_menu_main_version = "menu.main.version";
inline constexpr const char* text_menu_main_credits = "menu.main.credits";
inline constexpr const char* text_menu_main_play_online = "menu.main.play_online";
inline constexpr const char* text_menu_main_play_online_hint = "menu.main.play_online.hint";
inline constexpr const char* text_menu_main_play_offline = "menu.main.play_offline";
inline constexpr const char* text_menu_main_play_offline_hint = "menu.main.play_offline.hint";
inline constexpr const char* text_menu_main_link_login = "menu.main.link_login";
inline constexpr const char* text_menu_main_link_login_hint = "menu.main.link_login.hint";
inline constexpr const char* text_menu_main_sign_out = "menu.main.sign_out";
inline constexpr const char* text_menu_main_sign_out_hint = "menu.main.sign_out.hint";
inline constexpr const char* text_menu_main_help = "menu.main.help";
inline constexpr const char* text_menu_main_help_hint = "menu.main.help.hint";
inline constexpr const char* text_menu_main_settings = "menu.main.settings";
inline constexpr const char* text_menu_main_settings_hint = "menu.main.settings.hint";
inline constexpr const char* text_menu_main_quit = "menu.main.quit";
inline constexpr const char* text_menu_main_quit_hint = "menu.main.quit.hint";
inline constexpr const char* text_menu_help_title = "menu.help.title";
inline constexpr const char* text_menu_help_subtitle = "menu.help.subtitle";
inline constexpr const char* text_menu_help_footer = "menu.help.footer";
inline constexpr const char* text_menu_course_picker_title = "menu.course_picker.title";
inline constexpr const char* text_menu_course_picker_subtitle = "menu.course_picker.subtitle";
inline constexpr const char* text_menu_course_picker_footer = "menu.course_picker.footer";
inline constexpr const char* text_menu_course_picker_tile = "menu.course_picker.tile";
inline constexpr const char* text_menu_online_course_picker_subtitle = "menu.online_course_picker.subtitle";
inline constexpr const char* text_menu_confirm_title = "menu.confirm.title";
inline constexpr const char* text_menu_exit_title = "menu.exit.title";
inline constexpr const char* text_menu_confirm_yes = "menu.confirm.yes";
inline constexpr const char* text_menu_confirm_no = "menu.confirm.no";
inline constexpr const char* text_menu_confirm_settings = "menu.confirm.settings";
inline constexpr const char* text_menu_settings_title = "menu.settings.title";
inline constexpr const char* text_menu_settings_subtitle = "menu.settings.subtitle";
inline constexpr const char* text_menu_settings_footer = "menu.settings.footer";
inline constexpr const char* text_menu_settings_back = "menu.settings.back";
inline constexpr const char* text_menu_settings_back_hint = "menu.settings.back.hint";
inline constexpr const char* text_menu_settings_search_hint = "menu.settings.search_hint";
inline constexpr const char* text_menu_settings_search = "menu.settings.search";

// Online menus
inline constexpr const char* text_menu_online_title = "menu.online.title";
inline constexpr const char* text_menu_online_footer = "menu.online.footer";
inline constexpr const char* text_menu_login_subtitle = "menu.login.subtitle";
inline constexpr const char* text_menu_login_unavailable = "menu.login.unavailable";
inline constexpr const char* text_menu_login_needed = "menu.login.needed";
inline constexpr const char* text_menu_login_browser = "menu.login.browser";
inline constexpr const char* text_menu_login_signing_in = "menu.login.signing_in";
inline constexpr const char* text_menu_login_sign_in = "menu.login.sign_in";
inline constexpr const char* text_menu_login_sign_in_hint = "menu.login.sign_in.hint";
inline constexpr const char* text_menu_login_retry = "menu.login.retry";
inline constexpr const char* text_menu_login_retry_hint = "menu.login.retry.hint";
inline constexpr const char* text_menu_login_cancel = "menu.login.cancel";
inline constexpr const char* text_menu_login_cancel_hint = "menu.login.cancel.hint";
inline constexpr const char* text_menu_login_back = "menu.login.back";
inline constexpr const char* text_menu_login_back_hint = "menu.login.back.hint";
inline constexpr const char* text_menu_name_title = "menu.name.title";
inline constexpr const char* text_menu_name_subtitle = "menu.name.subtitle";
inline constexpr const char* text_menu_name_footer = "menu.name.footer";
inline constexpr const char* text_menu_name_waiting = "menu.name.waiting";
inline constexpr const char* text_menu_name_ok = "menu.name.ok";
inline constexpr const char* text_menu_name_ok_hint = "menu.name.ok.hint";
inline constexpr const char* text_menu_name_have_account = "menu.name.have_account";
inline constexpr const char* text_menu_name_have_account_hint = "menu.name.have_account.hint";
inline constexpr const char* text_menu_link_entry_title = "menu.link_entry.title";
inline constexpr const char* text_menu_link_entry_subtitle = "menu.link_entry.subtitle";
inline constexpr const char* text_menu_link_entry_footer = "menu.link_entry.footer";
inline constexpr const char* text_menu_link_entry_waiting = "menu.link_entry.waiting";
inline constexpr const char* text_menu_link_entry_ok = "menu.link_entry.ok";
inline constexpr const char* text_menu_link_entry_ok_hint = "menu.link_entry.ok.hint";
inline constexpr const char* text_menu_link_entry_back_hint = "menu.link_entry.back.hint";
inline constexpr const char* text_menu_link_code_title = "menu.link_code.title";
inline constexpr const char* text_menu_link_code_subtitle = "menu.link_code.subtitle";
inline constexpr const char* text_menu_link_code_footer = "menu.link_code.footer";
inline constexpr const char* text_menu_link_code_waiting = "menu.link_code.waiting";
inline constexpr const char* text_menu_link_code_expires = "menu.link_code.expires";
inline constexpr const char* text_menu_link_code_done = "menu.link_code.done";
inline constexpr const char* text_menu_link_code_done_hint = "menu.link_code.done.hint";
inline constexpr const char* text_menu_joining_subtitle = "menu.joining.subtitle";
inline constexpr const char* text_menu_joining_message = "menu.joining.message";
inline constexpr const char* text_menu_joining_cancel_hint = "menu.joining.cancel.hint";

// Controls help screen: one key per control, lines separated by '\n'
inline constexpr const char* text_help_wasd = "help.wasd";
inline constexpr const char* text_help_arrows = "help.arrows";
inline constexpr const char* text_help_mouse = "help.mouse";
inline constexpr const char* text_help_key_f = "help.key_f";
inline constexpr const char* text_help_swing = "help.swing";
inline constexpr const char* text_help_space = "help.space";
inline constexpr const char* text_help_shift = "help.shift";
inline constexpr const char* text_help_key_m = "help.key_m";
inline constexpr const char* text_help_tab = "help.tab";
inline constexpr const char* text_help_caps_lock = "help.caps_lock";
inline constexpr const char* text_help_escape = "help.escape";
inline constexpr const char* text_help_key_1 = "help.key_1";
inline constexpr const char* text_help_key_2 = "help.key_2";
inline constexpr const char* text_help_key_g = "help.key_g";
inline constexpr const char* text_controls_key_w = "controls.key_w";
inline constexpr const char* text_controls_key_a = "controls.key_a";
inline constexpr const char* text_controls_key_s = "controls.key_s";
inline constexpr const char* text_controls_key_d = "controls.key_d";
inline constexpr const char* text_controls_key_e = "controls.key_e";
inline constexpr const char* text_controls_key_f = "controls.key_f";
inline constexpr const char* text_controls_key_q = "controls.key_q";
inline constexpr const char* text_controls_key_m = "controls.key_m";
inline constexpr const char* text_controls_key_tab = "controls.key_tab";
inline constexpr const char* text_controls_key_caps_lock = "controls.key_caps_lock";
inline constexpr const char* text_controls_mouse = "controls.mouse";
inline constexpr const char* text_controls_key_1 = "controls.key_1";
inline constexpr const char* text_controls_key_2 = "controls.key_2";
inline constexpr const char* text_controls_key_g = "controls.key_g";
inline constexpr const char* text_controls_key_escape = "controls.key_escape";

// HUD
inline constexpr const char* text_hud_fps = "hud.fps";
// The swing clock's hours: the backswing from address (6) to the top (12).
inline constexpr const char* text_hud_swing_six = "hud.swing.six";
inline constexpr const char* text_hud_swing_nine = "hud.swing.nine";
inline constexpr const char* text_hud_swing_twelve = "hud.swing.twelve";
inline constexpr const char* text_hud_cart = "hud.cart";
inline constexpr const char* text_hud_cart_drive = "hud.cart.drive";
// The rangefinder: its reading and unit, the reading at the sky, its flag lock and the wind.
inline constexpr const char* text_hud_rangefinder = "hud.rangefinder";
inline constexpr const char* text_hud_rangefinder_unit = "hud.rangefinder.unit";
inline constexpr const char* text_hud_rangefinder_none = "hud.rangefinder.none";
inline constexpr const char* text_hud_rangefinder_flag = "hud.rangefinder.flag";
inline constexpr const char* text_hud_rangefinder_wind = "hud.rangefinder.wind";
// The height of what it reads over the ground under me, signed up or down hill.
inline constexpr const char* text_hud_rangefinder_height = "hud.rangefinder.height";
inline constexpr const char* text_hud_rangefinder_uphill = "hud.rangefinder.uphill";
inline constexpr const char* text_hud_rangefinder_downhill = "hud.rangefinder.downhill";
inline constexpr const char* text_hud_xp_drop = "hud.xp_drop";
// A shot lost in water, as the ball goes back
inline constexpr const char* text_hud_water_penalty = "hud.water_penalty";
inline constexpr const char* text_hud_mode_online = "hud.mode.online";
inline constexpr const char* text_hud_mode_reconnecting = "hud.mode.reconnecting";

// The weather's kinds (game/weather.h) and the compass points the wind comes from
inline constexpr const char* text_weather_clear = "weather.clear";
inline constexpr const char* text_weather_cloudy = "weather.cloudy";
inline constexpr const char* text_weather_light_fog = "weather.light_fog";
inline constexpr const char* text_weather_fog = "weather.fog";
inline constexpr const char* text_weather_light_rain = "weather.light_rain";
inline constexpr const char* text_weather_rain = "weather.rain";
inline constexpr const char* text_weather_heavy_rain = "weather.heavy_rain";
inline constexpr const char* text_weather_thunderstorm = "weather.thunderstorm";
inline constexpr const char* text_weather_sun_shower = "weather.sun_shower";
inline constexpr const char* text_compass_n = "compass.n";
inline constexpr const char* text_compass_ne = "compass.ne";
inline constexpr const char* text_compass_e = "compass.e";
inline constexpr const char* text_compass_se = "compass.se";
inline constexpr const char* text_compass_s = "compass.s";
inline constexpr const char* text_compass_sw = "compass.sw";
inline constexpr const char* text_compass_w = "compass.w";
inline constexpr const char* text_compass_nw = "compass.nw";

// The god menu (core/god_menu.h)
inline constexpr const char* text_god_title = "god.title";
inline constexpr const char* text_god_time = "god.time";
inline constexpr const char* text_god_weather = "god.weather";
inline constexpr const char* text_god_cloud = "god.cloud";
inline constexpr const char* text_god_rain = "god.rain";
inline constexpr const char* text_god_fog = "god.fog";
inline constexpr const char* text_god_thunder = "god.thunder";
inline constexpr const char* text_god_wind = "god.wind";
inline constexpr const char* text_god_wind_direction = "god.wind_direction";
inline constexpr const char* text_god_wetness = "god.wetness";
inline constexpr const char* text_god_hours = "god.hours";
inline constexpr const char* text_god_speed = "god.speed";
inline constexpr const char* text_god_percent = "god.percent";
inline constexpr const char* text_god_keys = "god.keys";
inline constexpr const char* text_god_online = "god.online";

// The camcorder's datestamp and the VCR's on-screen display (core/vcr_frame.h)
inline constexpr const char* text_camcorder_datestamp_time = "camcorder.datestamp.time";
inline constexpr const char* text_camcorder_datestamp_date = "camcorder.datestamp.date";
inline constexpr const char* text_camcorder_am = "camcorder.am";
inline constexpr const char* text_camcorder_pm = "camcorder.pm";
inline constexpr const char* text_camcorder_offline = "camcorder.offline";
inline constexpr const char* text_camcorder_month_jan = "camcorder.month.jan";
inline constexpr const char* text_camcorder_month_feb = "camcorder.month.feb";
inline constexpr const char* text_camcorder_month_mar = "camcorder.month.mar";
inline constexpr const char* text_camcorder_month_apr = "camcorder.month.apr";
inline constexpr const char* text_camcorder_month_may = "camcorder.month.may";
inline constexpr const char* text_camcorder_month_jun = "camcorder.month.jun";
inline constexpr const char* text_camcorder_month_jul = "camcorder.month.jul";
inline constexpr const char* text_camcorder_month_aug = "camcorder.month.aug";
inline constexpr const char* text_camcorder_month_sep = "camcorder.month.sep";
inline constexpr const char* text_camcorder_month_oct = "camcorder.month.oct";
inline constexpr const char* text_camcorder_month_nov = "camcorder.month.nov";
inline constexpr const char* text_camcorder_month_dec = "camcorder.month.dec";
inline constexpr const char* text_vcr_play = "vcr.play";
inline constexpr const char* text_vcr_pause = "vcr.pause";

// Hole signs at the tees
inline constexpr const char* text_hole_sign_number = "hole_sign.number";
inline constexpr const char* text_hole_sign_par = "hole_sign.par";
inline constexpr const char* text_hole_sign_length = "hole_sign.length";

// The course map (held up with M)
inline constexpr const char* text_course_map_hole_number = "course_map.hole_number";

// Skills panel
inline constexpr const char* text_skills_title = "skills.title";
inline constexpr const char* text_skills_header_name = "skills.header.name";
inline constexpr const char* text_skills_header_level = "skills.header.level";
inline constexpr const char* text_skills_header_xp = "skills.header.xp";
inline constexpr const char* text_skills_header_next = "skills.header.next";

// Scorecard
inline constexpr const char* text_scorecard_title = "scorecard.title";
inline constexpr const char* text_scorecard_results_title = "scorecard.results_title";
inline constexpr const char* text_scorecard_header_hole = "scorecard.header.hole";
inline constexpr const char* text_scorecard_header_par = "scorecard.header.par";
inline constexpr const char* text_scorecard_header_score = "scorecard.header.score";
inline constexpr const char* text_scorecard_header_relative = "scorecard.header.relative";
inline constexpr const char* text_scorecard_out = "scorecard.out";
inline constexpr const char* text_scorecard_in = "scorecard.in";
inline constexpr const char* text_scorecard_total = "scorecard.total";
inline constexpr const char* text_scorecard_results_hint = "scorecard.results_hint";
inline constexpr const char* text_scorecard_results_hint_online = "scorecard.results_hint_online";
inline constexpr const char* text_scorecard_even = "scorecard.even";
inline constexpr const char* text_scorecard_over = "scorecard.over";
inline constexpr const char* text_scorecard_under = "scorecard.under";
inline constexpr const char* text_scorecard_waiting_for_group = "scorecard.waiting_for_group";

// Text styles
inline constexpr const char* style_title = "title";
inline constexpr const char* style_subtitle = "subtitle";
inline constexpr const char* style_footer = "footer";
inline constexpr const char* style_corner_note = "corner_note";
inline constexpr const char* style_body = "body";
inline constexpr const char* style_input = "input";
inline constexpr const char* style_error = "error";
inline constexpr const char* style_link_code = "link_code";
inline constexpr const char* style_tile_label = "tile_label";
inline constexpr const char* style_tile_hint = "tile_hint";
inline constexpr const char* style_tile_label_small = "tile_label_small";
inline constexpr const char* style_tile_hint_small = "tile_hint_small";
inline constexpr const char* style_control_key = "control_key";
inline constexpr const char* style_hud_mode = "hud_mode";
inline constexpr const char* style_name_tag = "name_tag";
inline constexpr const char* style_name_tag_group = "name_tag_group";
inline constexpr const char* style_hud_scale = "hud_scale";
inline constexpr const char* style_hud_club = "hud_club";
inline constexpr const char* style_hud_club_name = "hud_club_name";
inline constexpr const char* style_god_title = "god_title";
inline constexpr const char* style_god_label = "god_label";
inline constexpr const char* style_god_live = "god_live";
inline constexpr const char* style_god_held = "god_held";
inline constexpr const char* style_god_note = "god_note";
inline constexpr const char* style_cart_label = "cart_label";
inline constexpr const char* style_cart_drive = "cart_drive";
inline constexpr const char* style_xp_drop = "xp_drop";
inline constexpr const char* style_rangefinder = "rangefinder";
inline constexpr const char* style_rangefinder_unit = "rangefinder_unit";
inline constexpr const char* style_rangefinder_tag = "rangefinder_tag";
inline constexpr const char* style_rangefinder_wind = "rangefinder_wind";
inline constexpr const char* style_rangefinder_height = "rangefinder_height";
inline constexpr const char* style_hole_sign_number = "hole_sign_number";
inline constexpr const char* style_hole_sign_detail = "hole_sign_detail";
inline constexpr const char* style_course_map_hole_number = "course_map_hole_number";
inline constexpr const char* style_panel_title = "panel_title";
inline constexpr const char* style_panel_header = "panel_header";
inline constexpr const char* style_panel_value = "panel_value";
inline constexpr const char* style_scorecard_title = "scorecard_title";
inline constexpr const char* style_scorecard_subtitle = "scorecard_subtitle";
inline constexpr const char* style_scorecard_header = "scorecard_header";
inline constexpr const char* style_scorecard_row = "scorecard_row";
inline constexpr const char* style_scorecard_row_current = "scorecard_row_current";
inline constexpr const char* style_scorecard_total = "scorecard_total";
inline constexpr const char* style_scorecard_hint = "scorecard_hint";
inline constexpr const char* style_datestamp = "datestamp";
inline constexpr const char* style_vcr_tag = "vcr_tag";
inline constexpr const char* style_burn_in_shadow = "burn_in_shadow";
inline constexpr const char* style_debug_fps = "debug_fps";
inline constexpr const char* style_debug_profile = "debug_profile";

inline std::string skill_text_key(const std::string& skill_id) {
    return "skill." + skill_id;
}

// Online failures arrive as ids: the server's reducer errors
// (game/server_errors.h) and the bridge's own
// (net_failure_* in game/net_types.h). Tests check each has a string.
inline std::string online_error_text_key(const std::string& error_id) {
    return "online.error." + error_id;
}

// What a link code redemption did (link_result_* in game/net_types.h, the
// my_link_status view).
inline std::string online_link_text_key(const std::string& result_id) {
    return "online.link." + result_id;
}
