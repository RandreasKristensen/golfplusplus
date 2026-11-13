#include "core/app.h"
#include "core/event_loop.h"
#include "game/hub_flow.h"
#include "game/mode_dispatch.h"
#include "game/course_session.h"
#include "game/online_play.h"
#include "game/text_ids.h"
#include "audio/sound_ids.h"
#include <SDL.h>
#include <algorithm>

bool app::arrive_at_place(const std::string& place_id) {
    const std::string destination = place_id;
    if (!enter_place(game_, destination)) { return false; }
    tee_book_ = tee_book_state{};
    menu_.confirm_active = false;
    if (is_online(game_)) { adopt_online_arrival(game_); }
    camera_transition_ = camera_transition_state{};
    weather_shown_ = weather_presentation{};
    return true;
}

void app::start_play(const startup_menu_result& result) {
    game_.play = result.play;
    game_.takes_cart = menu_.takes_cart;
    if (arrive_at_place(result.course.id)) {
        touring_.reset(); enter_playing(menu_);
        audio_.start_ambience(sound_ambience_course_day);
        return;
    }
    SDL_Log("Place %s failed to load", result.course.id.c_str());
    if (is_online(game_)) {
        carry_out({online_request{online_request_type::leave_room, {}}});
        return_to_main_menu(menu_);
    }
}

void app::update_hub_menu(const float dt, frame_profile* profile) {
    if (!is_online(game_) && !game_.hub_flow.travel_course.empty() && tape_tracking_.fade >= 1.0f) {
        if (!arrive_at_place(game_.hub_flow.travel_course)) {
            SDL_Log("Offline destination failed to load"); return_to_menu();
        }
    }
    set_mouse_captured(false);
    int width = 1, height = 1;
    SDL_GetWindowSize(window_.sdl_window(), &width, &height);
    const glm::vec2 point(static_cast<float>(input_.mouse_x) / std::max(1, width) * 2 - 1,
        1 - static_cast<float>(input_.mouse_y) / std::max(1, height) * 2);
    menu_pointer pointer;
    if (input_.mouse_dx || input_.mouse_dy) { pointer.hover = point; }
    if (input_.mouse_left.pressed) { pointer.click = point; }
    const auto sounds = update_tee_book(tee_book_, game_, catalog_, input_, pointer, online_status().reducer_failures);
    for (const auto sound : sounds) {
        audio_.play(sound == ui_sound::move ? sound_ui_move : sound == ui_sound::back ? sound_ui_back : sound_ui_select);
    }
    update_game(game_, game_input{}, dt, profile);
    render_data data = make_frame(0, false, profile);
    if (game_.hub_flow.screen != hub_screen::none) { data.startup_menu = render_tee_book(tee_book_, game_, catalog_, text_); }
    if (!game_.hub_flow.travel_course.empty()) {
        data.startup_menu.screen = startup_menu_screen::form;
        data.startup_menu.title = lookup_text(text_, text_hub_bus_title);
        data.startup_menu.message = lookup_text(text_, text_hub_travel);
    }
    present_frame(data, profile);
}

void app::watch_online_round() {
#if GOLFPP_NET
    if (menu_.flow != startup_flow::playing || !is_online(game_)) {
        return;
    }
    for (const reducer_failure& failure : online_->refusals()) {
        SDL_Log("The server refused %s: %s", failure.reducer.c_str(), failure.error.c_str());
        if (failure.reducer == "take_bus" || failure.reducer == "return_to_hub") {
            game_.hub_flow.travel_course.clear();
            game_.hub_flow.screen = hub_screen::bus;
            tee_book_.message = online_error_text_key(failure.error);
            adopt_online_arrival(game_);
        }
    }
    const online_round_status round = online_->watch_round(game_);
    if (round.event == online_round_event::restart) {
        if (game_.online.room_ready && !arrive_at_place(game_.online.room->course_id)) {
            SDL_Log("Travel destination %s failed to load", game_.online.room->course_id.c_str());
            return_to_menu();
        }
    } else if (round.event == online_round_event::lost) {
        return_to_menu();
        if (round.message_key.empty()) {
            open_online_login(menu_);
        } else {
            menu_.message_key = round.message_key;
        }
    }
#endif
}

