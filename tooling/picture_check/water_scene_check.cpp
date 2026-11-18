#include "water_scene_check.h"
#include "core/sky_frame.h"
#include "renderer/gl_loader.h"
#include "renderer/reflection_pass.h"
#include "renderer/tree_renderer.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <glm/gtc/matrix_transform.hpp>

bool check_water_scene(const check_course& course, const lighting_palette& palette, const std::string& root,
                       const std::filesystem::path& out, backdrop_pass& backdrop, water_renderer& water,
                       lighting_uniforms& lighting, framebuffer& scene, gl_timer_pool& timers) {
    tree_renderer tree;
    if (!tree.init(check_shader_path(root,"tree.vert"),check_shader_path(root,"tree.frag"),palette.trees)) { return false; }
    tree.set_seasons({{palette.trees.front().name,1,0,0}});
    tree_body body; body.base={0,10,30}; body.shape={0.4f,5,4,9};
    const std::vector<tree_body> trees{body};
    const glm::vec3 eye(0,1.7f,0);
    const auto view=glm::lookAt(eye,eye+glm::vec3(0,-std::tan(glm::radians(15.0f)),1),glm::vec3(0,1,0));
    const auto proj=glm::perspective(glm::radians(50.0f),4.0f/3.0f,0.1f,1000.0f);
    const auto frustum=make_view_frustum(proj*view);
    // The tree stands on a high bank outside the view's top edge.
    const bool off_screen=build_tree_batch(trees,&frustum).instances.empty();
    sky_controls sky; sky.pinned_hour=12;
    const auto light=make_render_lighting(course.game,palette,sky,0);
    render_water pond; pond.revision=917;
    for (const glm::vec2 p : {glm::vec2(-100,1),glm::vec2(100,1),glm::vec2(100,100),
                             glm::vec2(-100,1),glm::vec2(100,100),glm::vec2(-100,100)}) {
        render_water_vertex v; v.position={p.x,0,p.y}; pond.triangles.push_back(v);
    }
    reflection_pass reflections;
    std::vector<glm::vec4> baseline;
    std::vector<glm::vec4> reflected_tree;
    int reflected_draws=0;
    for (const bool with_tree : {false,true}) {
        scene.bind(); glViewport(0,0,320,240); glClearColor(0,0,0,1);
        glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        auto haze=backdrop.prepare(&course.game.place.backdrop,eye,course.game.area.extent,light.haze_tint);
        lighting.upload(light,haze,{},nullptr);
        backdrop.draw(view,proj,3,{});
        reflections.draw(&pond,view,proj,eye,timers,[&](const water_reflection_view& mirror) {
            auto mirrored_haze=backdrop.prepare(&course.game.place.backdrop,mirror.eye,course.game.area.extent,light.haze_tint);
            mirrored_haze.reflection_level=mirror.level;
            lighting.upload(light,mirrored_haze,{},nullptr);
            backdrop.draw(mirror.view,proj,3,{},true);
            if (with_tree && tree.draw(trees,mirror.projection*mirror.view,make_view_frustum(mirror.projection*mirror.view),
                                       3,glm::vec2(0),nullptr)) { ++reflected_draws; }
        });
        lighting.upload(light,haze,{},nullptr);
        water_style calm=palette.water; calm.ripple_slope=0; calm.fine_slope=0; calm.glint_strength=0;
        water.draw_surfaces(&pond,proj*view,calm,{},reflections.surfaces(),3,timers,nullptr);
        if (with_tree) { reflected_tree=read_colors(scene,320,240); }
        else { baseline=read_colors(scene,320,240); }
    }
    int changed=0;
    std::vector<std::array<std::uint8_t,3>> rgb;
    for (std::size_t i=0;i<baseline.size();++i) {
        const auto delta=glm::abs(glm::vec3(reflected_tree[i]-baseline[i]));
        changed+=std::max({delta.x,delta.y,delta.z})>0.004f ? 1:0;
        std::array<std::uint8_t,3> pixel{};
        for(int c=0;c<3;++c) { pixel[c]=static_cast<std::uint8_t>(255*std::pow(std::clamp(reflected_tree[i][c],0.0f,1.0f),1.0f/2.2f)); }
        rgb.push_back(pixel);
    }
    write_check_bmp(out/"water_offscreen_tree.bmp",320,240,rgb);
    scene.bind();glDepthMask(GL_TRUE);glClearDepth(1);glClear(GL_DEPTH_BUFFER_BIT);
    const auto exposed=water.visible_levels(&pond,proj*view,nullptr);
    glClearDepth(0);glClear(GL_DEPTH_BUFFER_BIT);
    const auto hidden=water.visible_levels(&pond,proj*view,nullptr);
    glClearDepth(1);
    const bool visibility_ok=exposed.size()==1 && hidden.empty();
    const bool ok=off_screen && reflected_draws==1 && changed>20 && visibility_ok;
    std::printf("  water off-screen tree: %d reflected pixels, %s\n",changed,ok ? "ok":"FAILED");
    reflections.shutdown(); tree.shutdown();
    return ok;
}
