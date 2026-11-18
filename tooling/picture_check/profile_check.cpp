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
#include "renderer/renderer.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
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
    weather_presentation weather;
    weather.started=true;
    weather.shown=shown_target(game.weather);
    std::set<float> levels;
    for(const auto& vertex:water.triangles) {levels.insert(vertex.position.y);}
    const std::string suffix=std::string(night ? "night":"noon")+(isolate_components ? "_isolate":"");
    std::ofstream info(out/("benchmark_"+suffix+"_info.txt"));
    info<<reinterpret_cast<const char*>(glGetString(GL_RENDERER))<<"\n1920 x 1080 window, "<<picture->target_lines
        <<" scene lines\n"<<warmup_frames<<" warmup + "<<measured_frames<<" measured frames per view/variant\n"
        <<"Each frame rebuilds render data, renders, swaps with vsync off, then glFinish.\n"
        <<"Serialized renderer throughput; excludes simulation, network, audio and loading.\n"
        <<"Submission includes driver and current-frame visibility-query waits. GPU stages are delayed.\n"
        <<"trees "<<(initial.trees ? initial.trees->size():0)<<", water levels "<<levels.size()<<", grass field bytes "
        <<course.grass.samples.size()*sizeof(glm::vec3)<<"\n";
    std::ofstream csv(out/("benchmark_"+suffix+".csv"));
    std::ofstream frames(out/("benchmark_"+suffix+"_frames.csv"));
    std::ofstream stages(out/("benchmark_"+suffix+"_gpu.csv"));
    csv<<"view,variant,mean_ms,p50_ms,p95_ms,p99_ms,max_ms,prep_ms,submit_ms,draw_calls,upload_bytes,reallocations,uniform_lookups\n";
    frames<<"view,variant,frame,frame_ms,prep_ms,submit_ms,draw_calls,upload_bytes\n";
    stages<<"view,variant,stage,gpu_ms\n";
    std::printf("GPU %s, 1920x1080, scene %d lines; %s, %zu trees, %zu water levels, %.1f MiB grass field\n",
                reinterpret_cast<const char*>(glGetString(GL_RENDERER)),picture->target_lines,night?"night":"noon",
                (initial.trees ? initial.trees->size():0),levels.size(),course.grass.samples.size()*sizeof(glm::vec3)/1048576.0);
    const glm::vec3 start=initial.camera_position,target=initial.camera_target;
    const std::vector<std::string> variants=isolate_components
        ? std::vector<std::string>{"full","no_water","no_trees","no_grass"}:std::vector<std::string>{"full"};
    for(const std::string& view:{std::string("play"),std::string("pond"),std::string("pan")}) {
        if(view=="pond" && water.triangles.empty()) {continue;}
        for(const std::string& variant:variants) {
            std::array<double,gpu_profile_stage_count> gpu{};
            std::vector<double> durations;
            double prep_total=0,submit_total=0,draws=0,uploads=0,reallocations=0,lookups=0;
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
                add_frame_lights(data,game,palette,tree_blobs);
                data.weather=make_frame_weather(weather,game,palette,data);
                // Diagnostic subtraction changes no shipped setting or production path.
                if(variant=="no_water") {data.water=nullptr;}
                if(variant=="no_trees") {data.trees=nullptr;data.tree_shadows=nullptr;}
                if(variant=="no_grass") {data.grass=nullptr;}
                const double prep=milliseconds(began);
                frame_profile profile;
                const auto submitting=clock_type::now();
                draw.render(data,*text,&profile);
                const double submit=milliseconds(submitting);
                SDL_GL_SwapWindow(window);
                glFinish();
                const double duration=milliseconds(began);
                if(frame<warmup_frames) {continue;}
                durations.push_back(duration);prep_total+=prep;submit_total+=submit;
                draws+=profile.draw_calls;uploads+=static_cast<double>(profile.buffer_upload_bytes);
                reallocations+=profile.buffer_reallocations;lookups+=profile.uniform_location_queries;
                for(std::size_t stage=0;stage<gpu_profile_stage_count;++stage) {gpu[stage]+=profile.gpu_stage_ms[stage];}
                frames<<view<<','<<variant<<','<<frame-warmup_frames<<','<<duration<<','<<prep<<','<<submit<<','
                      <<profile.draw_calls<<','<<profile.buffer_upload_bytes<<'\n';
            }
            const double count=measured_frames;
            const double mean=std::accumulate(durations.begin(),durations.end(),0.0)/count;
            const double p50=percentile(durations,0.5),p95=percentile(durations,0.95),p99=percentile(durations,0.99);
            csv<<view<<','<<variant<<','<<mean<<','<<p50<<','<<p95<<','<<p99<<','<<*std::max_element(durations.begin(),durations.end())
               <<','<<prep_total/count<<','<<submit_total/count<<','<<draws/count<<','<<uploads/count<<','<<reallocations/count
               <<','<<lookups/count<<'\n';
            std::printf("  %-4s %-8s mean %.2f ms, p95 %.2f, p99 %.2f; submit %.2f, %.0f draws, %.2f MiB/frame\n",
                        view.c_str(),variant.c_str(),mean,p95,p99,submit_total/count,draws/count,uploads/count/1048576.0);
            for(std::size_t stage=0;stage<gpu_profile_stage_count;++stage) {
                stages<<view<<','<<variant<<','<<gpu_profile_stage_label(static_cast<gpu_profile_stage>(stage))<<','<<gpu[stage]/count<<'\n';
            }
            csv.flush();frames.flush();stages.flush();
        }
    }
    draw.shutdown();
    return csv.good() && frames.good() && stages.good() && info.good();
}
