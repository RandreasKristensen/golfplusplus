#include "renderer/renderer.h"

#include "renderer/marker_scene.h"

// What moves, built and uploaded once a frame: every view and the sun's shadow
// map draw the parts they show from these uploads (prop_frame.h,
// world_marker_batch.h's marker_view).
void renderer::stage_frame_geometry(const render_data& data, frame_profile* profile) {
    colors_.glow = data.glow;
    prop_frame_.build(data, props_, colors_, bus_door_progress_);
    prop_renderer_.stage(prop_frame_, profile);

    const world_marker_scene markers = marker_scene_for(data, picture_.height);
    build_world_marker_batch(world_marker_batch_, markers, colors_);
    world_marker_renderer_.stage(world_marker_batch_, profile);
}
