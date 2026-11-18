#include "tree_check.h"
#include "renderer/gl_loader.h"
#include "renderer/lighting_uniforms.h"
#include "renderer/tree_renderer.h"
#include <algorithm>
#include <cstdio>
#include <glm/gtc/matrix_transform.hpp>

bool check_trees(const lighting_palette& palette, const std::string& root) {
    tree_renderer trees;
    framebuffer target;
    lighting_uniforms lighting;
    shader_program caster;
    if (!trees.init(check_shader_path(root,"tree.vert"),check_shader_path(root,"tree.frag"),palette.trees) ||
        !caster.load_from_files(check_shader_path(root,"shadow_map.vert"),check_shader_path(root,"shadow_map.frag"),"#define TREE_CASTER\n") ||
        !target.init(512,512,{framebuffer_color::signed_hdr,framebuffer_depth::drawn,false,false}) || !lighting.init()) { return false; }
    render_lighting light; light.ambient_sky=glm::vec3(1); light.ambient_ground=glm::vec3(1);
    bool passed=true;
    int largest=0;
    for (const auto& look:palette.trees) {
        trees.set_seasons({{look.name,1,0,0}});
        tree_body tree; tree.shape={0.4f,5,4,9};
        std::vector<glm::vec4> before;
        for (const float distance:{69.99f,70.01f}) {
            const glm::vec3 eye(0,7,-distance);
            const auto vp=glm::perspective(glm::radians(20.0f),1.0f,0.1f,200.0f)*glm::lookAt(eye,glm::vec3(0,7,0),glm::vec3(0,1,0));
            scene_haze haze; haze.eye=eye; haze.full_distance=100000;
            lighting.upload(light,haze,{},nullptr);
            target.bind();glViewport(0,0,512,512);glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
            glEnable(GL_DEPTH_TEST);glDisable(GL_CULL_FACE);
            trees.draw({tree},vp,make_view_frustum(vp),3,glm::vec2(4,1),nullptr);
            auto picture=read_colors(target,512,512);
            if(before.empty()) { before=std::move(picture);continue; }
            int changed=0,covered=0;
            for(std::size_t i=0;i<before.size();++i) {
                const bool a=before[i].a>0.5f,b=picture[i].a>0.5f;
                changed+=a!=b ? 1:0; covered+=a||b ? 1:0;
            }
            // The caster and picture must cover exactly the same pixels,
            // including seasonal leaf holes and the current wind pose.
            target.bind();glClear(GL_DEPTH_BUFFER_BIT);
            caster.use();trees.draw_casters({tree},1,caster,vp,nullptr);
            std::vector<float> depth(512*512);
            glReadPixels(0,0,512,512,GL_DEPTH_COMPONENT,GL_FLOAT,depth.data());
            int shadow_changed=0;
            for(std::size_t i=0;i<depth.size();++i) {shadow_changed+=(depth[i]<1)!=(picture[i].a>0.5f) ? 1:0;}
            passed=passed && shadow_changed==0;
            largest=std::max(largest,changed);
            passed=passed && covered>100 && changed<covered/50;
        }
    }
    std::printf("trees: all species across 70 m, largest changed coverage %d pixels: %s\n",largest,passed?"ok":"FAILED");
    caster.shutdown();trees.shutdown();target.shutdown();lighting.shutdown();
    return passed;
}
