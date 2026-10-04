#pragma once

// The online screens of the menu flow (core/startup_flow.h): signing in,
// picking a name, linking logins, picking a course and joining its room.
// They move on by themselves as the connection and the server's answers
// arrive (online_menu_status), and ask app for what the server should do
// (online_request).

#include "core/input.h"
#include "core/startup_flow.h"
#include "game/text_assets.h"
#include "renderer/menu_overlay.h"

#include <optional>

#include <glm/vec2.hpp>

bool is_online_flow(startup_flow flow);

// PLAY ONLINE from the main menu: the course picker when signed in with a
// name, else the sign-in screen, starting a silent sign-in.
void open_play_online(startup_flow_state& state, const online_menu_status& online, startup_menu_result& result);
// LINK ANOTHER LOGIN from the main menu.
void open_link_code_show(startup_flow_state& state, const online_menu_status& online, startup_menu_result& result);

void update_online_menu(startup_flow_state& state,
                        const input_state& input,
                        std::optional<glm::vec2> click,
                        const startup_catalog& catalog,
                        const online_menu_status& online,
                        startup_menu_result& result);

void make_online_menu_render_data(const startup_flow_state& state,
                                  const startup_catalog& catalog,
                                  const text_assets& text,
                                  const online_menu_status& online,
                                  render_startup_menu& menu);
