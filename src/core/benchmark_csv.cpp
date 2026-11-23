#include "core/benchmark_csv.h"
#include <array>

namespace {
constexpr std::array<const char*, 4> groups{"scene", "reflection", "zoom", "zoom_reflection"};
constexpr std::array<const char*, 6> passes{"main", "zoom", "reflection", "zoom_reflection", "sun_shadow", "lamp_shadow"};
static_assert(groups.size() == static_cast<std::size_t>(gpu_stage_group::count));
static_assert(passes.size() == static_cast<std::size_t>(profile_pass::count));
}

void benchmark_csv_header(std::ostream& out) {
    out << "view,phase,nearby,frame,frame_ms,cpu_ms,gpu_ms";
    for (std::size_t s = 0; s < profile_stage_count; ++s) {
        out << ",cpu_" << profile_stage_label(static_cast<profile_stage>(s)) << "_ms";
    }
    for (const auto* group : groups) {
        for (std::size_t s = 0; s < gpu_profile_stage_count; ++s) {
            out << ",gpu_" << group << '_' << gpu_profile_stage_label(static_cast<gpu_profile_stage>(s)) << "_ms";
        }
    }
    out << ",resident,peak_resident,gpu_bytes,gpu_buffers,gpu_textures,gpu_renderbuffers,load_ms,bake_ms"
           ",draws,uniform_sets,uniform_misses,buffer_growths,texture_allocations,static_uploads,static_bytes,dynamic_bytes,state_readbacks"
           ",water_candidates,reflection_levels,captures,capture_pixels,surface_samples,triangles_tested,buffer_allocations,buffer_writes"
           ",buffer_upload_bytes,visible_chunks,culled_chunks,chunk_draw_ranges,chunk_indices_drawn,chunk_indices_total,trees_visible";
    for (const auto* pass : passes) {
        for (const auto* metric : {"tree_instances", "tree_triangles", "tree_images", "grass_instances", "visible_indices", "submitted_indices"}) {
            out << ',' << pass << '_' << metric;
        }
    }
    out << ",player_x,player_z,sun_y,rain,ball_y,moving_ball,zoom,flag_lock,figures,balls,carts\n";
}

void benchmark_csv_frame(std::ostream& out, const benchmark_scenario& scenario, const char* phase,
                         const frame_profile& frame) {
    const auto stage = [&](const profile_stage s) { return frame.stage_ms[static_cast<std::size_t>(s)]; };
    // Query reads inside the render scope wait on the GPU; the CPU budget excludes them.
    const double cpu = stage(profile_stage::update_game) + stage(profile_stage::refresh_render_mesh_cache)
        + stage(profile_stage::make_render_data) + stage(profile_stage::render) - stage(profile_stage::query_read);
    out << scenario.name << ',' << phase << ',' << scenario.nearby << ',' << frame.frame_number << ','
        << frame.frame_ms << ',' << cpu << ',';
    if (frame.gpu_timers_available) { out << frame.gpu_frame_ms; }
    for (const double ms : frame.stage_ms) { out << ',' << ms; }
    for (const auto& group : frame.gpu_ms) {
        for (const double ms : group) {
            out << ',';
            if (frame.gpu_timers_available) { out << ms; }
        }
    }
    out << ',';
    if (frame.memory.resident_bytes) { out << *frame.memory.resident_bytes; }
    out << ',';
    if (frame.memory.peak_resident_bytes) { out << *frame.memory.peak_resident_bytes; }
    const auto& work = frame.work;
    out << ',' << frame.gpu_buffer_bytes + frame.gpu_texture_bytes + frame.gpu_renderbuffer_bytes
        << ',' << frame.gpu_buffer_bytes << ',' << frame.gpu_texture_bytes << ',' << frame.gpu_renderbuffer_bytes
        << ',' << frame.course_load_ms << ',' << frame.course_bake_ms << ',' << frame.draw_calls
        << ',' << frame.uniform_sets << ',' << frame.uniform_location_queries << ',' << work.buffer_growths
        << ',' << work.texture_allocations << ',' << work.static_uploads << ',' << work.static_upload_bytes
        << ',' << work.dynamic_upload_bytes << ',' << work.state_readbacks << ',' << work.water_candidates << ',' << work.reflection_levels << ',' << work.reflection_captures
        << ',' << work.reflection_pixels << ',' << frame.terrain_sample_calls << ',' << frame.terrain_triangles_tested
        << ',' << frame.buffer_reallocations << ',' << frame.buffer_writes << ',' << frame.buffer_upload_bytes
        << ',' << frame.visible_chunks << ',' << frame.culled_chunks << ',' << frame.chunk_draw_ranges
        << ',' << frame.chunk_indices_drawn << ',' << frame.chunk_indices_total << ',' << frame.trees_visible;
    for (const auto& pass : work.passes) {
        out << ',' << pass.tree_instances << ',' << pass.tree_triangles << ',' << pass.tree_images << ',' << pass.grass_instances
            << ',' << pass.terrain_indices_visible << ',' << pass.terrain_indices_submitted;
    }
}
