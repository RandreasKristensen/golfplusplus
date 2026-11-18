#include "renderer/renderer.h"
#include "renderer/gl_loader.h"
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

void renderer::render_scene(const glm::mat4& view, const glm::mat4& proj, const render_data& data, frame_profile* profile,
                            const bool reflected, const glm::mat4* sky_projection, const float reflection_level) {
    const glm::mat4 view_proj = proj * view;
    gpu_timers_.begin(gpu_profile_stage::terrain);
    const float lowest_terrain = data.terrain_mesh != nullptr && data.terrain_mesh->bounds.valid
        ? data.terrain_mesh->bounds.min.y
        : data.area_center.y;
    const render_sky_weather& weather = data.lighting.weather;
    scene_haze haze = backdrop_pass_.prepare(data.backdrop, data.camera_position, data.area_extent, data.lighting.haze_tint);
    haze.mist_floor = lowest_terrain;
    if (reflected) { haze.reflection_level = reflection_level; }
    lighting_uniforms_.upload(data.lighting,
                              weathered_haze(haze, weather_haze{weather.haze_gain, weather.haze_distance_share, weather.haze_grey}),
                              data.lights, profile);
    colors_.glow = data.glow;
    terrain_shader_.use();
    terrain_shader_.set_vec3("u_grass_look", glm::vec3(grass_style_.stripe_shade, grass_style_.patch_share, grass_style_.patch_meters));
    terrain_shader_.set_vec2("u_mow_widths", glm::vec2(grass_style_.stripe_fairway_meters, grass_style_.stripe_green_meters));
    grass_texture_.bind(1);
    path_texture_.bind(6);
    shadow_map_pass_.bind(terrain_shader_, shadow_map_unit);

    // Backdrop ground under the whole area so the horizon is never empty.
    const float ground_extent = std::max(1.0f, data.area_extent * 2.0f);
    const glm::mat4 ground_model = glm::scale(
        glm::translate(glm::mat4(1.0f), glm::vec3(data.area_center.x, lowest_terrain - 2.0f, data.area_center.z)),
        glm::vec3(ground_extent, 1.0f, ground_extent));
    set_terrain_draw_state(terrain_shader_, ground_model, view_proj, colors_.ground.backdrop, 1.0f, 0.0f, false);
    glBindVertexArray(ground_.vao);
    glDrawArrays(GL_TRIANGLES, 0, ground_.vertex_count);
    record_draw_call(profile);

    const view_frustum frustum = make_view_frustum(view_proj);
    const terrain_cull_stats terrain_stats =
        terrain_renderer_.draw(terrain_shader_, data.terrain_mesh, data.material_overlay_mesh, view_proj, frustum,
                               overlay_depth::pulled_forward, profile);
    cull_stats_ = renderer_cull_stats{terrain_stats.terrain, terrain_stats.material_overlay, false};
    // Under the blobs, which lie on them as on the ground.
    build_tee_box_batch(tee_box_batch_, data.tee_boxes, colors_.markers);
    tee_box_renderer_.shader().use();
    shadow_map_pass_.bind(tee_box_renderer_.shader(), shadow_map_unit);
    tee_box_renderer_.draw(tee_box_batch_, view_proj, profile);
    gpu_timers_.end();
    grass_renderer_.draw(grass_frame{data.terrain_mesh, data.material_overlay_mesh, data.grass, view_proj,
                                     data.camera_position,
                                     colors_.ground.rough, glm::vec2(data.weather.surface_wind.x, data.weather.surface_wind.z),
                                     data.clock_seconds},
                         grass_style_, shadow_map_pass_, shadow_map_unit, gpu_timers_, profile);

    gpu_timers_.begin(gpu_profile_stage::shadows);
    blob_shadow_renderer_.draw(data.tree_shadows, data.tree_shadows_revision, data.blob_shadows, view_proj, profile);
    gpu_timers_.end();

    gpu_timers_.begin(gpu_profile_stage::trees);
    const std::vector<tree_body> no_trees;
    tree_renderer_.shader().use();
    shadow_map_pass_.bind(tree_renderer_.shader(), shadow_map_unit);
    cull_stats_.trees_visible =
        tree_renderer_.draw(data.trees != nullptr ? *data.trees : no_trees, view_proj, frustum, data.clock_seconds, glm::vec2(data.weather.surface_wind.x, data.weather.surface_wind.z), profile);
    hole_sign_renderer_.draw(data.hole_signs, view_proj, profile);
    fence_renderer_.draw_posts(data.fences, view_proj, profile);
    floodlight_renderer_.draw(data.floodlights, view_proj, profile);
    gpu_timers_.end();

    world_marker_scene markers;
    markers.show_hole = data.show_hole;
    markers.pin_position = data.pin_position;
    markers.pin_markers = &data.pin_markers;
    markers.pin_marker_heights = &data.pin_marker_heights;
    markers.flags_out = &data.flags_out;
    markers.lying_flags = &data.lying_flags;
    markers.collectibles = &data.collectibles;
    markers.cup_radius_meters = data.cup_radius_meters;
    markers.cup_depth_meters = data.cup_depth_meters;
    markers.pin_visual_height_meters = data.pin_visual_height_meters;
    markers.flagstick_radius_meters = data.flagstick_radius_meters;
    markers.flag = flag_pose{data.weather.flag_wind, data.weather.flag_lift, data.weather.flag_phase, data.weather.flag_flap};
    markers.address_feet = data.address_feet ? &*data.address_feet : nullptr;
    markers.ball_visual_radius_meters = data.ball_visual_radius_meters;
    markers.remote_avatars = &data.remote_avatars;
    markers.remote_balls = &data.remote_balls;
    markers.avatar_eye_height = data.avatar_eye_height;
    build_world_marker_batch(world_marker_batch_, markers, colors_);
    world_marker_renderer_.draw(world_marker_batch_, view_proj, profile);

    // Behind everything solid, before anything blends over it.
    gpu_timers_.begin(gpu_profile_stage::sky);
    backdrop_pass_.draw(view, sky_projection ? *sky_projection : proj, data.clock_seconds, data.weather.lightning, reflected);
    gpu_timers_.end();

    // After the sky: smoke and beer blend over it.
    build_prop_batch(data);
    draw_props(prop_batch_, view_proj, profile);
    draw_props(world_props_, view_proj, profile);
    render_flight_path(view, proj, data, profile);

    if (data.show_ball) {
        const glm::mat4 ball_model = glm::scale(glm::translate(glm::mat4(1.0f), data.ball_position),
                                                glm::vec3(data.ball_visual_radius_meters));
        ball_shader_.use();
        ball_shader_.set_mat4("u_mvp", view_proj * ball_model);
        ball_shader_.set_mat4("u_model", ball_model);
        ball_shader_.set_vec3("u_color", colors_.markers.ball);
        ball_shader_.set_float("u_glow", colors_.glow.ball);
        glBindVertexArray(ball_.vao);
        glDrawArrays(GL_TRIANGLES, 0, ball_.vertex_count);
        record_draw_call(profile);
        glBindVertexArray(0);
    }
    if (!reflected && data.water && !data.water->triangles.empty()) {
        gpu_timers_.begin(gpu_profile_stage::reflection);
        const auto visible=water_renderer_.visible_levels(data.water,view_proj,profile);
        const auto main_cull=cull_stats_;
        if (!reflection_pass_.draw(data.water, view, proj, data.camera_position, gpu_timers_,
            [&](const water_reflection_view& mirror) {
                const render_data mirrored=make_reflected_scene(data,mirror.eye);
                render_scene(mirror.view,mirror.projection,mirrored,profile,true,&proj,mirror.level);
            },&visible)) {SDL_Log("Cannot allocate the water reflection target.");}
        cull_stats_=main_cull;
        lighting_uniforms_.upload(data.lighting,
            weathered_haze(haze,weather_haze{weather.haze_gain,weather.haze_distance_share,weather.haze_grey}),data.lights,profile);
        gpu_timers_.end();
    }
    // Water and nets blend over everything solid, the ball included.
    if (!reflected) {
        water_renderer_.draw_surfaces(data.water, view_proj, water_style_, data.weather, reflection_pass_.surfaces(),
                                      data.clock_seconds, gpu_timers_, profile);
    }
    fence_renderer_.draw_nets(data.fences, view_proj, profile);
    gpu_timers_.begin(gpu_profile_stage::weather);
    weather_renderer_.draw(data.weather, view_proj, profile);
    gpu_timers_.end();
}

