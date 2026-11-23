#include "water_scene_check.h"
#include "core/sky_frame.h"
#include "renderer/gl_loader.h"
#include "renderer/reflection_pass.h"
#include "renderer/tree_renderer.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>
#include <vector>
#include <glm/gtc/matrix_transform.hpp>

bool check_water_queries(water_renderer& water, framebuffer& scene);

bool check_water_scene(const check_course& course, const lighting_palette& palette, const std::string& root,
                       const std::filesystem::path& out, backdrop_pass& backdrop, water_renderer& water,
                       lighting_uniforms& lighting, framebuffer& scene, gl_timer_pool& timers) {
    tree_renderer tree;
    if (!tree.init(check_shader_path(root,"tree.vert"),check_shader_path(root,"tree.frag"),palette.trees,palette.tree_impostor)) { return false; }
    tree.set_seasons({{palette.trees.front().name,1,0,0}});
    tree_body body; body.base={0,10,30}; body.shape={0.4f,5,4,9};
    const std::vector<tree_body> trees{body};
    tree.set_trees(trees,1,nullptr);
    const glm::vec3 eye(0,1.7f,0);
    const auto view=glm::lookAt(eye,eye+glm::vec3(0,-std::tan(glm::radians(15.0f)),1),glm::vec3(0,1,0));
    const auto proj=glm::perspective(glm::radians(50.0f),4.0f/3.0f,0.1f,1000.0f);
    const auto frustum=make_view_frustum(proj*view);
    // The tree stands on a high bank outside the view's top edge.
    tree_index index; index.build(trees);
    std::vector<render_tree_bucket> seen,far_seen; index.select(&frustum,nullptr,seen,far_seen);
    const bool off_screen=seen.empty();
    sky_controls sky; sky.pinned_hour=12;
    const auto light=make_render_lighting(course.game,palette,sky,0);
    render_water pond; pond.revision=917;
    for (const glm::vec2 p : {glm::vec2(-100,1),glm::vec2(100,1),glm::vec2(100,100),
                             glm::vec2(-100,1),glm::vec2(100,100),glm::vec2(-100,100)}) {
        render_water_vertex v; v.position={p.x,0,p.y}; pond.triangles.push_back(v);
    }
    index_render_water(pond);
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
        const auto candidates=water.candidates(&pond,proj*view,eye,palette.water,{},nullptr);
        const draw_target target{scene.id(),320,240,false};
        reflections.draw(view,proj,eye,target,candidates,[&](const water_reflection_view& mirror) {
            auto mirrored_haze=backdrop.prepare(&course.game.place.backdrop,mirror.eye,course.game.area.extent,light.haze_tint);
            mirrored_haze.reflection_level=mirror.level;
            lighting.upload(light,mirrored_haze,{},nullptr);
            backdrop.draw(mirror.view,mirror.sky_projection,3,{},true);
            if (with_tree && tree.draw(mirror.projection*mirror.view,make_view_frustum(mirror.projection*mirror.view),
                                       3,glm::vec2(0),nullptr)) { ++reflected_draws; }
        });
        lighting.upload(light,haze,{},nullptr);
        water_style calm=palette.water; calm.ripple_slope=0; calm.fine_slope=0; calm.glint_strength=0;
        water.draw_surfaces(&pond,candidates,proj*view,target,calm,{},reflections.surfaces(),3,timers,nullptr);
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
    const auto exposed=water.candidates(&pond,proj*view,eye,palette.water,{},nullptr);
    const bool queried=exposed.size()==1 && exposed[0].query!=0;
    const bool ok=off_screen && reflected_draws==1 && changed>20 && queried;
    std::printf("  water off-screen tree: %d reflected pixels, %s\n",changed,ok ? "ok":"FAILED");
    reflections.shutdown(); tree.shutdown();
    return check_water_queries(water,scene) && ok;
}

namespace {
struct gl_state {
    GLint framebuffer=0,viewport[4]={},front_face=0,depth_func=0,scissor_box[4]={};
    GLboolean depth_mask=0,color_mask[4]={},scissor=0;
    bool operator==(const gl_state& o) const {
        return framebuffer==o.framebuffer && front_face==o.front_face && depth_func==o.depth_func && depth_mask==o.depth_mask &&
            scissor==o.scissor && std::equal(viewport,viewport+4,o.viewport) && std::equal(color_mask,color_mask+4,o.color_mask) &&
            std::equal(scissor_box,scissor_box+4,o.scissor_box);
    }
};
gl_state read_gl_state() {
    gl_state s;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&s.framebuffer);glGetIntegerv(GL_VIEWPORT,s.viewport);
    glGetIntegerv(GL_FRONT_FACE,&s.front_face);glGetIntegerv(GL_DEPTH_FUNC,&s.depth_func);
    glGetIntegerv(GL_SCISSOR_BOX,s.scissor_box);glGetBooleanv(GL_DEPTH_WRITEMASK,&s.depth_mask);
    glGetBooleanv(GL_COLOR_WRITEMASK,s.color_mask);s.scissor=glIsEnabled(GL_SCISSOR_TEST);
    return s;
}
// The red of a mirror's middle texel.
float middle_red(const unsigned int texture) {
    glBindTexture(GL_TEXTURE_2D,texture);
    GLint width=0,height=0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_WIDTH,&width);glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_HEIGHT,&height);
    std::vector<float> texels(static_cast<std::size_t>(width)*static_cast<std::size_t>(height)*3);
    glGetTexImage(GL_TEXTURE_2D,0,GL_RGB,GL_FLOAT,texels.data());
    glBindTexture(GL_TEXTURE_2D,0);
    return texels[(static_cast<std::size_t>(height/2)*static_cast<std::size_t>(width)+static_cast<std::size_t>(width/2))*3];
}
}

// Two ponds at different heights, mirrored under occlusion queries: a mirror
// is drawn while its water shows (result pending or known), skipped once its
// result is known hidden, redrawn the frame the water is exposed again, and
// each level keeps its own picture. The callback paints its colour in place of the scene.
bool check_water_queries(water_renderer& water, framebuffer& scene) {
    render_water ponds; ponds.revision=923;
    for (const auto& [level,x] : {std::pair{0.0f,-60.0f},std::pair{-0.5f,10.0f}}) {
        for (const glm::vec2 p : {glm::vec2(x,20),glm::vec2(x+50,20),glm::vec2(x+50,70),glm::vec2(x,20),glm::vec2(x+50,70),glm::vec2(x,70)}) {
            render_water_vertex v; v.position={p.x,level,p.y}; ponds.triangles.push_back(v);
        }
    }
    index_render_water(ponds);
    const glm::vec3 eye(0,1.7f,0);
    const auto view=glm::lookAt(eye,eye+glm::vec3(0,-0.1f,1),glm::vec3(0,1,0));
    const auto proj=glm::perspective(glm::radians(60.0f),4.0f/3.0f,0.1f,500.0f);
    reflection_pass reflections;
    const draw_target target{scene.id(),320,240,true};
    float paint=0.0f;
    bool viewport_is_crop=true,cropped=false;
    const auto draw=[&](const bool finish_queries) {
        const auto candidates=water.candidates(&ponds,proj*view,eye,water_style{},render_weather{},nullptr);
        if (finish_queries) { glFinish(); }
        reflections.draw(view,proj,eye,target,candidates,[&](const water_reflection_view& mirror) {
            GLint now[4]={};glGetIntegerv(GL_VIEWPORT,now);
            viewport_is_crop=viewport_is_crop && now[0]==0 && now[1]==0 && now[2]==mirror.crop.width && now[3]==mirror.crop.height;
            cropped=cropped || mirror.crop.width<mirror.crop.full_width || mirror.crop.height<mirror.crop.full_height;
            glClearColor(paint+(mirror.level<-0.25f ? 0.0f : 0.25f),0,0,1);glClear(GL_COLOR_BUFFER_BIT);
        });
        return candidates;
    };
    const auto frame=[&](const float depth) {
        scene.bind();glViewport(0,0,320,240);glDepthMask(GL_TRUE);glClearDepth(depth);glClear(GL_DEPTH_BUFFER_BIT);glClearDepth(1);
    };
    glEnable(GL_SCISSOR_TEST);glScissor(0,0,320,240);
    frame(1);
    const gl_state before=read_gl_state();
    bool ok=true;
    const auto expect=[&](const bool condition,const char* what) {
        if (!condition) { std::printf("  water queries: %s FAILED\n",what); ok=false; }
    };
    // Open water, results pending: both levels drawn, each its own picture.
    paint=0.5f;
    const auto first=draw(false);
    expect(first.size()==2 && first[0].query!=0 && first[1].query!=0 && first[0].query!=first[1].query,"two queried levels");
    expect(read_gl_state()==before,"state restored after a drawn frame");
    expect(viewport_is_crop && cropped,"each capture is drawn into a viewport of its crop's size, smaller than the picture");
    expect(reflections.surfaces().size()==2 && reflections.pool_size()==2,"one pooled target per visible level");
    if (reflections.surfaces().size()==2) {
        const auto& low=reflections.surfaces()[0];const auto& high=reflections.surfaces()[1];
        expect(low.level==-0.5f && high.level==0.0f && low.texture!=high.texture,"levels keep separate textures");
        expect(std::abs(middle_red(low.texture)-0.5f)<0.01f && std::abs(middle_red(high.texture)-0.75f)<0.01f,"each level shows its own picture");
    }
    // Hidden water, results known: nothing is drawn, the pictures stay.
    frame(0);paint=0.0f;
    draw(true);
    expect(read_gl_state()==before,"state restored after a skipped frame");
    if (reflections.surfaces().size()==2) {
        expect(std::abs(middle_red(reflections.surfaces()[1].texture)-0.75f)<0.01f,"hidden water drew nothing");
    }
    // Exposed again in the very next frame: drawn at once, never a frame late.
    frame(1);paint=0.125f;
    draw(false);
    if (reflections.surfaces().size()==2) {
        expect(std::abs(middle_red(reflections.surfaces()[0].texture)-0.125f)<0.01f &&
               std::abs(middle_red(reflections.surfaces()[1].texture)-0.375f)<0.01f,"newly exposed water draws this frame");
    }
    // Looking away: no candidates; the pool is trimmed after it idles.
    const auto away=glm::lookAt(eye,eye+glm::vec3(0,0,-1),glm::vec3(0,1,0));
    expect(water.candidates(&ponds,proj*away,eye,water_style{},render_weather{},nullptr).empty(),"water behind the camera is no candidate");
    for (int i=0;i<reflection_pass::pool_idle_frames+1;++i) { reflections.draw(view,proj,eye,target,{},[](const water_reflection_view&) {}); }
    expect(reflections.pool_size()==0 && reflections.surfaces().empty(),"idle pool trimmed");
    glDisable(GL_SCISSOR_TEST);
    reflections.shutdown();
    std::printf("  water queries: %s\n",ok ? "ok":"FAILED");
    return ok;
}
