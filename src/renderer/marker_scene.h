#pragma once

// What a frame's world markers show (renderer/world_marker_batch.h), read off
// the frame's render_data, so the renderer and the tests build the same batch.
// The scene points into `data`; poles are widened to a pixel of the main camera
// at `scene_height_pixels` tall. Its cups are left to build_cup_batch: they
// change only with the hole being played. GL-free.

#include "renderer/render_data.h"
#include "renderer/world_marker_batch.h"

world_marker_scene marker_scene_for(const render_data& data, int scene_height_pixels);
