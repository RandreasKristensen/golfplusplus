#include "profile_check.h"
#include "core/render_frame.h"
#include "core/light_frame.h"
#include "core/sky_frame.h"
#include "core/weather_frame.h"
#include "game/model_kinds.h"
#include "game/sky_clock.h"
#include "renderer/fence_batch.h"
#include "renderer/floodlight_batch.h"
#include "renderer/gl_loader.h"
#include "game/look_reload.h"
#include "renderer/renderer.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <functional>
#include <numeric>
#include <set>
#include <vector>
#include <glm/gtc/matrix_transform.hpp>

namespace {
using clock_type = std::chrono::steady_clock;
constexpr int warmup_frames = 60;
constexpr int measured_frames = 240;

double milliseconds(const clock_type::time_point start) {
    return std::chrono::duration<double,std::milli>(clock_type::now()-start).count();
}

double percentile(std::vector<double> values,const double fraction) {
    std::sort(values.begin(),values.end());
    return values[static_cast<std::size_t>(std::ceil(fraction*values.size()))-1];
}
}

bool profile_picture(const check_course& course,const game_content& content,const lighting_palette& palette,
                     const std::string& root,const std::filesystem::path& out,SDL_Window* window,
                     const bool isolate_components,const bool night) {
    const auto props=load_props(root);
    const auto text=load_text_assets(root);
    const auto picture=load_picture_style(root);
    if(!props.props || !text || !picture) {return false;}
    bool warm_clean=true;
    renderer draw;
    if(!draw.init(window,root,*picture,palette,*props.props)) {draw.shutdown();return false;}
    const auto& game=course.game;
    const auto water=build_render_water(game.area,game.terrain_render_revision);
    const auto fences=build_render_fences(game.area,game.terrain_render_revision,props.props->at(fence_model).color("post"),game.tuning.fence.pole_radius);
    const auto lamps=build_render_floodlights(game.area,game.terrain_render_revision,props.props->at(floodlight_model));
    const auto signs=build_render_hole_signs(game.area,hole_sign_texts(game),{},*text,palette.colors,
                                             props.props->at(hole_sign_model),game.terrain_render_revision);
    const render_meshes meshes{&course.terrain,&course.overlay,&signs,&fences,&water,&lamps,&course.grass};
    const auto initial=make_render_data(game,*text,content.skills,meshes,*props.props,60,true);
    sky_controls sky;sky.pinned_hour=night ? 24:12;
    // A winter date gives a true night on every Danish course.
    if(night) {sky.pinned_date=days_from_civil(1989,12,21);}
    const auto light=make_render_lighting(game,palette,sky,0);
    tree_shadow_cache tree_blobs;refresh_tree_shadows(tree_blobs,game,palette.shadows);
    blob_ground_memo blob_ground;
    weather_presentation weather;
    weather.started=true;
    weather.shown=shown_target(game.weather);
    std::set<float> levels;
    for(const auto& vertex:water.triangles) {levels.insert(vertex.position.y);}
    const std::string suffix=std::string(night ? "night":"noon")+(isolate_components ? "_isolate":"");
    std::ofstream info(out/("benchmark_"+suffix+"_info.txt"));
    info<<reinterpret_cast<const char*>(glGetString(GL_RENDERER))<<"\n1920 x 1080 window, "<<picture->scene_lines.front()
        <<" scene lines\n"<<warmup_frames<<" warmup + "<<measured_frames<<" measured frames per view/variant\n"
        <<"Each frame rebuilds render data, renders, swaps with vsync off, then glFinish.\n"
        <<"Serialized renderer cost; excludes simulation, network, audio and loading.\n"
        <<"Submission includes driver and current-frame visibility-query waits. GPU stages are delayed.\n"
        <<"trees "<<(initial.trees ? initial.trees->size():0)<<", water levels "<<levels.size()<<", grass field bytes "
        <<grass_field_memory_bytes(course.grass)<<"\n";
    std::ofstream csv(out/("benchmark_"+suffix+".csv"));
    std::ofstream frames(out/("benchmark_"+suffix+"_frames.csv"));
    std::ofstream stages(out/("benchmark_"+suffix+"_gpu.csv"));
    csv<<"view,variant,mean_ms,p50_ms,p95_ms,p99_ms,max_ms,prep_ms,submit_ms,draw_calls,upload_bytes,reallocations,uniform_lookups,measurement\n";
    frames<<"view,variant,frame,frame_ms,prep_ms,submit_ms,draw_calls,upload_bytes,swap_ms,finish_ms,measurement\n";
    stages<<"view,variant,stage,gpu_ms,samples,measurement\n";
    std::printf("serialized renderer probe: GPU %s, 1920x1080, scene %d lines; %s, %zu trees, %zu water levels, %.1f MiB grass field\n",
                reinterpret_cast<const char*>(glGetString(GL_RENDERER)),picture->scene_lines.front(),night?"night":"noon",
                (initial.trees ? initial.trees->size():0),levels.size(),grass_field_memory_bytes(course.grass)/1048576.0);
    const glm::vec3 start=initial.camera_position,target=initial.camera_target;
    {
        // Cached geometry (placed models, sign and fence casters, tee boxes, cups) is sent once per course
        // revision and per look, never because the light or the clock moved. Prints what it checked.
        const auto probe=[&](const std::function<void(render_data&)>& adjust,const char* label,const bool expect_upload) {
            auto data=make_render_data(game,*text,content.skills,meshes,*props.props,60,true);
            data.camera_position=start;data.camera_target=target;
            data.lighting=light;
            add_frame_lights(data,game,palette,tree_blobs,blob_ground);
            data.weather=make_frame_weather(weather,game,palette,data);
            adjust(data);
            frame_profile profile;
            draw.begin_profile(&profile);
            draw.render(data,*text);
            SDL_GL_SwapWindow(window);
            glFinish();
            draw.finish_profile([](const frame_profile&) {});
            const bool uploaded=profile.work.static_uploads>0;
            const bool ok=uploaded==expect_upload;
            std::printf("  static cache %-34s %llu uploads, %s%s\n",label,static_cast<unsigned long long>(profile.work.static_uploads),
                        uploaded ? "sent":"none sent",ok ? "":"  FAIL");
            warm_clean=warm_clean && ok;
        };
        const std::uint64_t revision=initial.trees_revision;
        probe([](render_data&) {},"first frame of the course",true);
        probe([](render_data&) {},"same course again",false);
        probe([](render_data& data) {data.glow.window=0.9f;data.glow.cup=0.4f;data.glow.pin_pole=0.3f;},"night glow raised",false);
        probe([revision](render_data& data) {data.trees_revision=revision+1;},"entering the course again",true);
        probe([revision](render_data& data) {data.trees_revision=revision+1;},"same entry again",false);
        look reloaded;reloaded.lighting=palette;reloaded.picture=*picture;
        look_changes changes;changes.shadows=true;
        if(!draw.set_look(root,reloaded,changes)) {std::printf("  FAIL: look reload\n");warm_clean=false;}
        probe([revision](render_data& data) {data.trees_revision=revision+1;},"palette reloaded",true);
        probe([revision](render_data& data) {data.trees_revision=revision+1;},"after the reload",false);
    }
    const std::vector<std::string> variants=isolate_components
        ? std::vector<std::string>{"full","no_water","no_trees","no_grass"}:std::vector<std::string>{"full"};
    for(const std::string& view:{std::string("play"),std::string("pond"),std::string("pan")}) {
        if(view=="pond" && water.triangles.empty()) {continue;}
        for(const std::string& variant:variants) {
            gpu_view_times gpu{};
            double gpu_frame = 0;
            int gpu_samples = 0;
            const frame_profile_sink collect = [&](const frame_profile& sample) {
                if (sample.frame_number < warmup_frames || !sample.gpu_timers_available) { return; }
                ++gpu_samples;
                gpu_frame += sample.gpu_frame_ms;
                for (std::size_t group = 0; group < gpu.size(); ++group) {
                    for (std::size_t stage = 0; stage < gpu[group].size(); ++stage) {
                        gpu[group][stage] += sample.gpu_ms[group][stage];
                    }
                }
            };
            std::vector<double> durations;
            double prep_total=0,submit_total=0,draws=0,uploads=0,reallocations=0,lookups=0,readbacks=0;
            glFinish();
            for(int frame=0;frame<warmup_frames+measured_frames;++frame) {
                const auto began=clock_type::now();
                auto data=make_render_data(game,*text,content.skills,meshes,*props.props,60,true);
                data.camera_position=start;data.camera_target=target;
                if(view=="pond") {
                    const auto& point=water.triangles.front().position;
                    data.camera_position=point+glm::vec3(0,1.7f,-15);
                    data.camera_target=point+glm::vec3(0,0,25);
                } else if(view=="pan") {
                    const float angle=6.2831853f*static_cast<float>(frame-warmup_frames)/measured_frames;
                    const auto direction=glm::mat3(glm::rotate(glm::mat4(1),angle,glm::vec3(0,1,0)))*(target-start);
                    data.camera_target=start+direction;
                }
                data.clock_seconds=3.0f+static_cast<float>(frame)/60.0f;
                data.lighting=light;
                add_frame_lights(data,game,palette,tree_blobs,blob_ground);
                data.weather=make_frame_weather(weather,game,palette,data);
                // Diagnostic subtraction changes no shipped setting or production path.
                if(variant=="no_water") {data.water=nullptr;}
                if(variant=="no_trees") {data.trees=nullptr;data.tree_shadows=nullptr;}
                if(variant=="no_grass") {data.grass=nullptr;}
                const double prep=milliseconds(began);
                frame_profile profile;
                const auto submitting=clock_type::now();
                profile.frame_number = frame;
                draw.begin_profile(&profile);
                draw.render(data,*text);
                const double submit=milliseconds(submitting);
                const auto swapping = clock_type::now();
                SDL_GL_SwapWindow(window);
                const double swap = milliseconds(swapping);
                const auto finishing = clock_type::now();
                glFinish();
                const double finish = milliseconds(finishing);
                const double duration=milliseconds(began);
                draw.finish_profile(collect);
                if(frame<warmup_frames) {continue;}
                durations.push_back(duration);prep_total+=prep;submit_total+=submit;
                draws+=profile.draw_calls;uploads+=static_cast<double>(profile.buffer_upload_bytes);
                reallocations+=profile.buffer_reallocations;lookups+=profile.uniform_location_queries;readbacks+=static_cast<double>(profile.work.state_readbacks);
                frames<<view<<','<<variant<<','<<frame-warmup_frames<<','<<duration<<','<<prep<<','<<submit<<','
                      <<profile.draw_calls<<','<<profile.buffer_upload_bytes<<','<<swap<<','<<finish<<",serialized\n";
            }
            draw.flush_profiles(collect);
            // A warmed stationary view (main, and the mirrored one at a pond) repeats no uniform lookup,
            // reallocates no buffer and sends at most 256 KiB a frame (the rendering plan's section 1).
            if(view!="pan" && variant=="full") {
                const double sent_per_frame=uploads/measured_frames;
                if(lookups!=0) {
                    std::printf("  FAIL %s: %.0f uniform lookups after warmup\n",view.c_str(),lookups);
                    warm_clean=false;
                }
                if(readbacks!=0) {
                    std::printf("  FAIL %s: %.0f GL state read-backs after warmup\n",view.c_str(),readbacks);
                    warm_clean=false;
                }
                if(reallocations!=0) {
                    std::printf("  FAIL %s: %.0f buffer reallocations after warmup\n",view.c_str(),reallocations);
                    warm_clean=false;
                }
                if(sent_per_frame>256.0*1024.0) {
                    std::printf("  FAIL %s: %.0f KiB sent a frame\n",view.c_str(),sent_per_frame/1024.0);
                    warm_clean=false;
                }
            }
            const double count=measured_frames;
            const double mean=std::accumulate(durations.begin(),durations.end(),0.0)/count;
            const double p50=percentile(durations,0.5),p95=percentile(durations,0.95),p99=percentile(durations,0.99);
            csv<<view<<','<<variant<<','<<mean<<','<<p50<<','<<p95<<','<<p99<<','<<*std::max_element(durations.begin(),durations.end())
               <<','<<prep_total/count<<','<<submit_total/count<<','<<draws/count<<','<<uploads/count<<','<<reallocations/count
               <<','<<lookups/count<<",serialized\n";
            std::printf("  %-4s %-8s mean %.2f ms, p95 %.2f, p99 %.2f; submit %.2f, %.0f draws, %.2f MiB/frame\n",
                        view.c_str(),variant.c_str(),mean,p95,p99,submit_total/count,draws/count,uploads/count/1048576.0);
            const auto write_stage = [&](const std::string& name, const double total) {
                stages << view << ',' << variant << ',' << name << ',';
                if (gpu_samples) { stages << total / gpu_samples; }
                stages << ',' << gpu_samples << ",serialized\n";
            };
            write_stage("FRAME", gpu_frame);
            constexpr std::array<const char*, 4> prefixes{"", "REF/", "ZOOM/", "ZOOM/REF/"};
            static_assert(prefixes.size() == std::tuple_size<gpu_view_times>::value);
            for (std::size_t group = 0; group < gpu.size(); ++group) {
                for (std::size_t stage = 0; stage < gpu[group].size(); ++stage) {
                    if (group == 0 || gpu[group][stage] > 0) {
                        const std::string label = gpu_profile_stage_label(static_cast<gpu_profile_stage>(stage));
                        write_stage(prefixes[group] + label, gpu[group][stage]);
                    }
                }
            }
            if (gpu_samples) {
                std::printf("       GPU frame %.2f ms (%d/%d timestamp samples)\n",
                            gpu_frame / gpu_samples, gpu_samples, measured_frames);
            } else {
                std::printf("       GPU timestamps unavailable (0/%d samples)\n", measured_frames);
            }
            csv.flush();frames.flush();stages.flush();
        }
    }
    draw.shutdown();
    return warm_clean && csv.good() && frames.good() && stages.good() && info.good();
}
