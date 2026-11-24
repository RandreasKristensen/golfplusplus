#include "renderer/renderer.h"
#include "renderer/gl_loader.h"
#include "renderer/prop_glow.h"
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

void renderer::render_scene(const glm::mat4& view, const glm::mat4& proj, const render_data& data, const draw_target& target,
                            frame_profile* profile, const bool reflected, const glm::mat4* sky_projection, const float reflection_level,
                            const bool zoomed, const hidden_disc& hidden) {
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
    terrain_shader_.use();
    terrain_shader_.set_vec2("u_patch_look", glm::vec2(grass_style_.patch_share, grass_style_.patch_meters));
    terrain_shader_.set_vec3("u_fibre_fairway", glm::vec3(grass_style_.fibre_fairway_meters, grass_style_.fibre_fairway_shade, grass_style_.fibre_fairway_slope));
    terrain_shader_.set_float("u_lamp_cap", grass_style_.lamp_light_cap);
    terrain_shader_.set_vec3("u_fibre_green", glm::vec3(grass_style_.fibre_green_meters, grass_style_.fibre_green_shade, grass_style_.fibre_green_slope));
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

    view_frustum frustum = make_view_frustum(view_proj);
    frustum.hidden = hidden;
    const render_lod_view ground_lod = make_render_lod_view(
        data.camera_position, proj, target.height,
        reflected ? terrain_lod_style_.mirror_max_error_pixels : 0.0f);
    const terrain_cull_stats terrain_stats =
        terrain_renderer_.draw(terrain_shader_, data.terrain_mesh, data.material_overlay_mesh, view_proj, frustum,
                               overlay_depth::pulled_forward, ground_lod, profile);
    cull_stats_ = renderer_cull_stats{terrain_stats.terrain, terrain_stats.material_overlay, false};
    // Under the blobs, which lie on them as on the ground.
    tee_box_renderer_.shader().use();
    shadow_map_pass_.bind(tee_box_renderer_.shader(), shadow_map_unit);
    const std::array<glm::vec4, 2> glow = glow_floor_table(colors_.glow);
    tee_box_renderer_.draw(tee_box_batch_, marker_view::scene, view_proj, glow, profile);
    gpu_timers_.end();
    grass_renderer_.draw(grass_frame{data.grass, view_proj,
                                     data.camera_position,
                                     colors_.ground.rough, glm::vec2(data.weather.surface_wind.x, data.weather.surface_wind.z),
                                     data.clock_seconds, target.height, hidden},
                         grass_style_, shadow_map_pass_, shadow_map_unit, gpu_timers_, profile);

    gpu_timers_.begin(gpu_profile_stage::shadows);
    blob_shadow_renderer_.draw(data.tree_shadows, data.tree_shadows_revision, data.blob_shadows, view_proj, profile);
    gpu_timers_.end();

    gpu_timers_.begin(gpu_profile_stage::trees);
    tree_renderer_.shader().use();
    shadow_map_pass_.bind(tree_renderer_.shader(), shadow_map_unit);
    tree_renderer_.image_shader().use();
    shadow_map_pass_.bind(tree_renderer_.image_shader(), shadow_map_unit);
    tree_renderer_.leaf_shader().use();
    shadow_map_pass_.bind(tree_renderer_.leaf_shader(), shadow_map_unit);
    cull_stats_.trees_visible =
        tree_renderer_.draw(view_proj, frustum, data.clock_seconds, glm::vec2(data.weather.surface_wind.x, data.weather.surface_wind.z), profile,
                            tree_eye{data.camera_position, proj[1][1] * 0.5f * static_cast<float>(target.height),
                                     static_cast<float>(target.width) * static_cast<float>(target.height),
                                     !lighting_uniforms_.lights().empty()});
    hole_sign_renderer_.draw(data.hole_signs, view_proj, profile);
    fence_renderer_.draw_posts(data.fences, view_proj, profile);
    floodlight_renderer_.draw(data.floodlights, view_proj, profile);
    gpu_timers_.end();

    cup_renderer_.draw(cup_batch_, marker_view::scene, view_proj, glow, profile);
    const marker_view markers = reflected ? marker_view::mirror : zoomed ? marker_view::zoom : marker_view::scene;
    world_marker_renderer_.draw(world_marker_batch_, markers, view_proj, glow, profile);

    // Behind everything solid, before anything blends over it.
    gpu_timers_.begin(gpu_profile_stage::sky);
    backdrop_pass_.draw(view, sky_projection ? *sky_projection : proj, data.clock_seconds, data.weather.lightning, reflected);
    gpu_timers_.end();

    // After the sky: smoke and beer blend over it.
    draw_scene_props(reflected, view_proj, profile);
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
    std::vector<water_candidate> candidates;
    if (!reflected && data.water && !data.water->triangles.empty()) {
        gpu_timers_.begin(gpu_profile_stage::reflection);
        candidates = water_renderer_.candidates(data.water, view_proj, data.camera_position, water_style_, data.weather, profile);
        const auto main_cull=cull_stats_;
        if (!reflection_pass_.draw(view, proj, data.camera_position, target, candidates,
            [&](const water_reflection_view& mirror) {
                const bool zoom = profile && profile->work.active_pass == profile_pass::zoom;
                const profile_pass_scope pass(profile, zoom ? profile_pass::zoom_reflection : profile_pass::reflection);
                const render_data mirrored=make_reflected_scene(data,mirror.eye);
                render_scene(mirror.view,mirror.projection,mirrored,mirror.target,profile,true,&mirror.sky_projection,mirror.level);
            }, profile)) {SDL_Log("Cannot allocate the water reflection target.");}
        cull_stats_=main_cull;
        lighting_uniforms_.upload(data.lighting,
            weathered_haze(haze,weather_haze{weather.haze_gain,weather.haze_distance_share,weather.haze_grey}),data.lights,profile);
        gpu_timers_.end();
    }
    // Water and nets blend over everything solid, the ball included.
    if (!reflected) {
        water_renderer_.draw_surfaces(data.water, candidates, view_proj, target, water_style_, data.weather, reflection_pass_.surfaces(),
                                      data.clock_seconds, gpu_timers_, profile);
    }
    fence_renderer_.draw_nets(data.fences, view_proj, profile);
    gpu_timers_.begin(gpu_profile_stage::weather);
    weather_renderer_.draw(data.weather, view_proj, profile);
    if (!reflected) {
        world_bolt_renderer_.draw(data.weather.lightning, data.camera_position, view_proj, profile);
    }
    gpu_timers_.end();
}

bool renderer::water_may_show(const render_data& data, const glm::mat4& view_proj) const {
    if (data.water == nullptr || data.water->triangles.empty()) {
        return false;
    }
    const water_lookup_reach reach =
        water_reflection_reach(water_style_, glm::length(data.weather.surface_wind), data.weather.surface_rain);
    return !water_candidates(*data.water, view_proj, data.camera_position, reach).empty();
}

void renderer::finish_profile(const frame_profile_sink& sink) {
    auto* frame = allocations_.frame;
    allocations_.frame = nullptr;
    if (frame) {
        frame->visible_chunks = cull_stats_.terrain.chunks_visible + cull_stats_.material_overlay.chunks_visible;
        frame->culled_chunks = cull_stats_.terrain.chunks_culled + cull_stats_.material_overlay.chunks_culled;
        frame->chunk_draw_ranges = cull_stats_.terrain.draw_ranges + cull_stats_.material_overlay.draw_ranges;
        frame->chunk_indices_drawn = cull_stats_.terrain.indices_drawn + cull_stats_.material_overlay.indices_drawn;
        frame->chunk_indices_total = cull_stats_.terrain.indices_total + cull_stats_.material_overlay.indices_total;
        frame->trees_visible = cull_stats_.trees_visible;
        frame->gpu_texture_bytes = allocations_.textures.bytes();
        frame->gpu_buffer_bytes = allocations_.buffers.bytes();
        frame->gpu_renderbuffer_bytes = allocations_.renderbuffers.bytes();
        gpu_timers_.finish(*frame, sink);
    }
}
