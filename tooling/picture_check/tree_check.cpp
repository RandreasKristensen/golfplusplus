#include "tree_check.h"
#include "renderer/gl_loader.h"
#include "renderer/lighting_uniforms.h"
#include "renderer/tree_renderer.h"
#include <algorithm>
#include <cstdio>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/common.hpp>
#include <glm/vector_relational.hpp>

bool check_trees(const lighting_palette& palette, const std::string& root) {
    tree_renderer trees;
    framebuffer target;
    lighting_uniforms lighting;
    shader_program caster;
    if (!trees.init(check_shader_path(root,"tree.vert"),check_shader_path(root,"tree.frag"),palette.trees,palette.tree_impostor) ||
        !caster.load_from_files(check_shader_path(root,"shadow_map.vert"),check_shader_path(root,"shadow_map.frag"),"#define TREE_CASTER\n") ||
        !target.init(512,512,{framebuffer_color::signed_hdr,framebuffer_depth::drawn,false,false}) || !lighting.init()) { return false; }
    render_lighting light; light.ambient_sky=glm::vec3(1); light.ambient_ground=glm::vec3(1);
    bool passed=true;
    int largest=0;
    std::uint64_t revision=0;
    for (const auto& look:palette.trees) {
      // In leaf, and bare with a winter's withered leaves and twigs.
      for (const render_tree_species& season:{render_tree_species{look.name,1,0,0},render_tree_species{look.name,0,1,0,0.25f}}) {
        trees.set_seasons({season});
        tree_body tree; tree.shape={0.4f,5,4,9};
        trees.set_trees({tree},++revision,nullptr);
        std::vector<glm::vec4> before;
        for (const float distance:{69.99f,70.01f}) {
            const glm::vec3 eye(0,7,-distance);
            const auto vp=glm::perspective(glm::radians(20.0f),1.0f,0.1f,200.0f)*glm::lookAt(eye,glm::vec3(0,7,0),glm::vec3(0,1,0));
            scene_haze haze; haze.eye=eye; haze.full_distance=100000;
            lighting.upload(light,haze,{},nullptr);
            target.bind();glViewport(0,0,512,512);glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
            glEnable(GL_DEPTH_TEST);glDisable(GL_CULL_FACE);
            trees.draw(vp,make_view_frustum(vp),3,glm::vec2(4,1),nullptr);
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
            caster.use();trees.draw_casters(caster,vp,nullptr);
            std::vector<float> depth(512*512);
            glReadPixels(0,0,512,512,GL_DEPTH_COMPONENT,GL_FLOAT,depth.data());
            int shadow_changed=0;
            for(std::size_t i=0;i<depth.size();++i) {shadow_changed+=(depth[i]<1)!=(picture[i].a>0.5f) ? 1:0;}
            passed=passed && shadow_changed==0;
            largest=std::max(largest,changed);
            passed=passed && covered>100 && changed<covered/50;
        }
      }
    }
    {
        // Entering again: the index and its buffer are replaced only by a new revision of the trees,
        // a new palette regrows the models; the clock and wind are uniforms.
        trees.set_seasons({{palette.trees.front().name,1,0,0}});
        tree_body here; here.shape={0.4f,5,4,9};
        tree_body away=here; away.base={500,0,500};
        const glm::vec3 eye(0,7,-40);
        const auto vp=glm::perspective(glm::radians(20.0f),1.0f,0.1f,200.0f)*glm::lookAt(eye,glm::vec3(0,7,0),glm::vec3(0,1,0));
        scene_haze haze; haze.eye=eye; haze.full_distance=100000;
        lighting.upload(light,haze,{},nullptr);
        const auto covered=[&](const float seconds) {
            target.bind();glViewport(0,0,512,512);glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
            glEnable(GL_DEPTH_TEST);glDisable(GL_CULL_FACE);
            trees.draw(vp,make_view_frustum(vp),seconds,glm::vec2(0),nullptr);
            int count=0;
            for(const auto& pixel:read_colors(target,512,512)) { count+=pixel.a>0.5f ? 1:0; }
            return count;
        };
        frame_profile uploads;
        trees.set_trees({here},++revision,&uploads);
        const int first=covered(1);
        const bool one_upload=uploads.work.static_uploads==1;
        trees.set_trees({here},revision,&uploads);
        const bool kept=uploads.work.static_uploads==1 && covered(1)==first;
        trees.set_trees({away},++revision,nullptr);
        const bool left=covered(1)==0;
        trees.set_trees({here},++revision,nullptr);
        const bool back=covered(1)==first;
        const bool regrown=trees.set_looks(palette.trees,palette.tree_impostor);
        trees.set_seasons({{palette.trees.front().name,1,0,0}});
        const bool relooked=regrown && covered(1)==first;
        const bool again=one_upload && kept && left && back && relooked && first>100;
        std::printf("trees: course re-entry, new looks and a still index: %s\n",again?"ok":"FAILED");
        passed=passed && again;
    }
    {
        // Crowns drawn depth first (the casters' shader, then lit where the depth is equal) must give exactly the
        // picture of the plain draw, leaf holes and all, but for ties.
        trees.set_seasons({{palette.trees.front().name,1,0,0}});
        std::vector<tree_body> grove;
        for (int i=0;i<12;++i) {
            tree_body one; one.shape={0.4f,5,4,9};
            one.base={static_cast<float>(i%4)*2.5f-4.0f,0,static_cast<float>(i/4)*2.5f};
            grove.push_back(one);
        }
        trees.set_trees(grove,++revision,nullptr);
        const glm::vec3 eye(0,5,-22);
        const auto vp=glm::perspective(glm::radians(40.0f),1.0f,0.1f,200.0f)*glm::lookAt(eye,glm::vec3(0,5,2),glm::vec3(0,1,0));
        scene_haze haze; haze.eye=eye; haze.full_distance=100000;
        lighting.upload(light,haze,{},nullptr);
        const auto drawn=[&](const float viewport_pixels) {
            target.bind();glViewport(0,0,512,512);glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
            glEnable(GL_DEPTH_TEST);glDisable(GL_CULL_FACE);
            // A lens that makes every crown near, so each is drawn whole: depth first when the picture is given a size.
            trees.draw(vp,make_view_frustum(vp),2,glm::vec2(5,0),nullptr,tree_eye{eye,1000.0f,viewport_pixels});
            return read_colors(target,512,512);
        };
        const auto plain=drawn(0.0f);
        const auto depth_first=drawn(512.0f*512.0f);
        int covered=0,different=0;
        for(std::size_t i=0;i<plain.size();++i) {
            covered+=plain[i].a>0.5f ? 1:0;
            different+=(plain[i].a>0.5f)!=(depth_first[i].a>0.5f) || glm::any(glm::greaterThan(glm::abs(plain[i]-depth_first[i]),glm::vec4(1.0e-4f))) ? 1:0;
        }
        // Two cards in one plane tie in depth, and the depth-first draw lets the later win: a handful of pixels.
        const bool same=covered>1000 && different<=covered/2000;
        std::printf("trees: depth first and plain draws of a grove (%d pixels), %d differ: %s\n",covered,different,same?"ok":"FAILED");
        passed=passed && same;
    }
    std::printf("trees: all species across 70 m, largest changed coverage %d pixels: %s\n",largest,passed?"ok":"FAILED");
    caster.shutdown();trees.shutdown();target.shutdown();lighting.shutdown();
    return passed;
}
