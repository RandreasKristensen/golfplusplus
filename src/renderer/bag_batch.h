#pragma once

// Golf bags and carried clubs as drawn (their models in assets/world/props/):
// every cart with the bags strapped at its mounts, a bag set down standing
// leant back on its stand, one slung across a figure's back, and the club a
// figure carries walking, held at its side head down; and a bag or club
// being handled, on its way between two of those. Each bag in its owner's
// bag paint. Drawn into the prop batch (prop_batch.h). GL-free.

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

// A club stood head up in the bag whose frame is `bag_frame`, its grip down
// in the bag, its head just out of `bag`'s mouth.
glm::mat4 club_in_bag_frame(const glm::mat4& bag_frame, const prop_model& bag, const prop_model& club);

// The club and the hand holding it during a return (grip first) or draw
// (lifted by its head, then turned and passed to the grip). Progress is 0..1
// for that half of a swap. The figure may keep walking throughout.
struct handled_club_pose {
    glm::mat4 frame{1.0f};
    glm::vec3 hand{0.0f};
};
handled_club_pose handling_club_pose(const render_remote_avatar& figure, float eye_height,
                                    const glm::mat4& bag_frame, const prop_model& bag, const prop_model& club,
                                    float progress, bool drawing);
// Where `prop` is on its way: `t` of the way from its `from` frame to its
// `to`, turning between them, lifted by its arc.
glm::mat4 handled_prop_frame(const render_handled_prop& prop);

// Every cart in `data` with its strapped bags, every bag set down, and every
// bag and club being handled.
void append_carts_and_bags(prop_batch& batch, const render_data& data, const prop_catalog& props, const scene_colors& colors);

// What `figure` carries: its bag on its back and the club in its hand while walking.
void append_figure_gear(prop_batch& batch, const render_remote_avatar& figure, float eye_height, const prop_catalog& props,
                        const scene_colors& colors);
