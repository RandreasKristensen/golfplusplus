#pragma once

// Golf bags and carried clubs as drawn (their models in assets/world/props/):
// every cart with the bags strapped at its mounts, a bag set down standing
// leant back on its stand, one slung across a figure's back, and the club a
// figure carries walking, held at its side head down. Drawn into the prop
// batch (prop_batch.h). GL-free.

#include "game/lighting_loader.h"
#include "game/prop_loader.h"
#include "renderer/prop_batch.h"
#include "renderer/render_data.h"

#include <glm/mat4x4.hpp>

// A bag's model frame: set down, and on `figure`'s back (`eye_height` its eyes').
glm::mat4 ground_bag_frame(const render_ground_bag& bag, const prop_model& model);
glm::mat4 back_bag_frame(const render_remote_avatar& figure, float eye_height, const prop_model& model);
// The club `figure` carries: its grip in the hand on the golfer's side, its head ahead, just off the ground.
glm::mat4 carried_club_frame(const render_remote_avatar& figure, float eye_height, const prop_model& model);

// Every cart in `data` with its strapped bags, and every bag set down.
void append_carts_and_bags(prop_batch& batch, const render_data& data, const prop_catalog& props, const glow_floors& glow);

// What `figure` carries: its bag on its back and the club in its hand while walking.
void append_figure_gear(prop_batch& batch, const render_remote_avatar& figure, float eye_height, const prop_catalog& props,
                        const glow_floors& glow);
