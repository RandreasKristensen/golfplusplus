#pragma once

// Keys into the string table (assets/text/en.json) and names of text styles
// (assets/ui/text_styles.json). Tests check that every `text_*` id here exists
// in en.json and every `style_*` id exists in text_styles.json, so keep one
// constant per line in this exact form.
//
// Skill names are looked up as "skill.<skill_id>" (see skill_text_key).

#include <string>

// Startup menus
constexpr const char* text_menu_main_title = "menu.main.title";
constexpr const char* text_menu_main_subtitle = "menu.main.subtitle";
constexpr const char* text_menu_main_footer = "menu.main.footer";
constexpr const char* text_menu_main_play_hole = "menu.main.play_hole";
constexpr const char* text_menu_main_play_hole_hint = "menu.main.play_hole.hint";
constexpr const char* text_menu_main_play_course = "menu.main.play_course";
constexpr const char* text_menu_main_play_course_hint = "menu.main.play_course.hint";
constexpr const char* text_menu_main_help = "menu.main.help";
constexpr const char* text_menu_main_help_hint = "menu.main.help.hint";
constexpr const char* text_menu_main_quit = "menu.main.quit";
constexpr const char* text_menu_main_quit_hint = "menu.main.quit.hint";
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
constexpr const char* text_menu_confirm_title = "menu.confirm.title";
constexpr const char* text_menu_confirm_yes = "menu.confirm.yes";
constexpr const char* text_menu_confirm_no = "menu.confirm.no";

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
constexpr const char* text_controls_key_1 = "controls.key_1";
constexpr const char* text_controls_key_2 = "controls.key_2";

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
constexpr const char* text_scorecard_total = "scorecard.total";
constexpr const char* text_scorecard_results_hint = "scorecard.results_hint";
constexpr const char* text_scorecard_hole_fallback = "scorecard.hole_fallback";
constexpr const char* text_scorecard_hole_row = "scorecard.hole_row";
constexpr const char* text_scorecard_even = "scorecard.even";
constexpr const char* text_scorecard_over = "scorecard.over";
constexpr const char* text_scorecard_under = "scorecard.under";

// Text styles
constexpr const char* style_title = "title";
constexpr const char* style_subtitle = "subtitle";
constexpr const char* style_footer = "footer";
constexpr const char* style_body = "body";
constexpr const char* style_error = "error";
constexpr const char* style_input = "input";
constexpr const char* style_tile_label = "tile_label";
constexpr const char* style_tile_hint = "tile_hint";
constexpr const char* style_tile_label_small = "tile_label_small";
constexpr const char* style_tile_hint_small = "tile_hint_small";
constexpr const char* style_control_key = "control_key";
constexpr const char* style_control_key_down = "control_key_down";
constexpr const char* style_hud_label = "hud_label";
constexpr const char* style_hud_scale = "hud_scale";
constexpr const char* style_hud_club = "hud_club";
constexpr const char* style_cart_label = "cart_label";
constexpr const char* style_cart_drive = "cart_drive";
constexpr const char* style_cart_drift = "cart_drift";
constexpr const char* style_xp_drop = "xp_drop";
constexpr const char* style_rangefinder = "rangefinder";
constexpr const char* style_panel_title = "panel_title";
constexpr const char* style_panel_header = "panel_header";
constexpr const char* style_panel_value = "panel_value";
constexpr const char* style_panel_hint = "panel_hint";
constexpr const char* style_scorecard_title = "scorecard_title";
constexpr const char* style_scorecard_title_compact = "scorecard_title_compact";
constexpr const char* style_scorecard_subtitle = "scorecard_subtitle";
constexpr const char* style_scorecard_subtitle_compact = "scorecard_subtitle_compact";
constexpr const char* style_scorecard_header = "scorecard_header";
constexpr const char* style_scorecard_header_compact = "scorecard_header_compact";
constexpr const char* style_scorecard_row = "scorecard_row";
constexpr const char* style_scorecard_row_compact = "scorecard_row_compact";
constexpr const char* style_scorecard_row_current = "scorecard_row_current";
constexpr const char* style_scorecard_row_pending = "scorecard_row_pending";
constexpr const char* style_scorecard_total = "scorecard_total";
constexpr const char* style_scorecard_total_compact = "scorecard_total_compact";
constexpr const char* style_scorecard_hint = "scorecard_hint";
constexpr const char* style_debug_fps = "debug_fps";
constexpr const char* style_debug_profile = "debug_profile";

inline std::string skill_text_key(const std::string& skill_id) {
    return "skill." + skill_id;
}
