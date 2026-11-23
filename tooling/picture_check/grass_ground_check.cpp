#include "grass_ground_check.h"
#include "core/sky_frame.h"
#include "renderer/gl_loader.h"
#include "renderer/grass_renderer.h"
#include "renderer/lighting_uniforms.h"
#include "renderer/shadow_map_pass.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>

grass_check_result check_grass_ground_views(const check_course& course,const lighting_palette& palette,
                                             const std::string& root,const std::filesystem::path& out) {
    grass_check_result result;
    grass_renderer diagnostic,roots,blades;
    lighting_uniforms lighting;
    shadow_map_pass shadows;
    framebuffer picture;
    gl_timer_pool timers;timers.init();
    const auto path=[&](const char* name){return check_shader_path(root,name);};
    if(!diagnostic.init(path("grass_blade.vert"),path("grass_blade.frag"),"#define GRASS_DEBUG_RINGS\n") ||
       !roots.init(path("grass_blade.vert"),path("grass_blade.frag"),"#define GRASS_DEBUG_ROOTS\n") ||
       !blades.init(path("grass_blade.vert"),path("grass_blade.frag")) ||
       !lighting.init() || !shadows.init(path("shadow_map.vert"),path("shadow_map.frag"),palette.shadows) ||
       !picture.init(900,600,{framebuffer_color::exact,framebuffer_depth::drawn,false,false})) { return result; }
    sky_controls sky;sky.pinned_hour=12;
    const auto light=make_render_lighting(course.game,palette,sky,0);
    const auto& area=course.game.area;
    int fairways=0;
    for(const auto& zone:area.zones) {
        if(zone.type!=material_zone_type::fairway || zone.outline.size()<3) {continue;}
        if(fairways++==3) {break;}
        const auto& edge=zone.outline.front();
        const glm::vec2 centre(zone.center.x,zone.center.z);
        const glm::vec2 way=glm::normalize(centre-edge+glm::vec2(0.00001f));
        for(const float distance:{30.0f,60.0f,90.0f}) {
            const glm::vec2 at=centre-way*distance;
            const auto eye=anchor_on_terrain(area,{at.x,0,at.y})+glm::vec3(0,1.7f,0);
            const auto target=anchor_on_terrain(area,{centre.x,0,centre.y});
            const auto vp=glm::perspective(glm::radians(60.0f),1.5f,0.1f,300.0f)*glm::lookAt(eye,target,glm::vec3(0,1,0));
            scene_haze haze;haze.eye=eye;haze.full_distance=100000;
            lighting.upload(light,haze,{},nullptr);
            const grass_frame frame{&course.grass,vp,eye,palette.colors.ground.rough,{},7,600,{}};
            timers.begin_frame(nullptr);picture.bind();glViewport(0,0,900,600);glEnable(GL_DEPTH_TEST);glDisable(GL_CULL_FACE);
            glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
            roots.draw(frame,palette.grass,shadows,4,timers,nullptr);
            const auto selected=read_colors(picture,900,600);
            for(const auto& pixel:selected) {
                if(pixel.a<0.5f) {continue;}
                const auto material=sample_area(area,glm::vec3(pixel)).material;
                if(material!=terrain_material::rough && material!=terrain_material::heavy_rough) {
                    if(result.wrong_pixels<10) {
                        std::printf("  bare root %.6f %.6f %.6f material %d, field %.6f\n",pixel.x,pixel.y,pixel.z,
                                    static_cast<int>(material),sample_grass_field(course.grass,{pixel.x,pixel.z}).y);
                    }
                    ++result.wrong_pixels;
                }
                ++result.blade_pixels;
            }
            picture.bind();glClearColor(0.1f,0.1f,0.1f,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
            diagnostic.draw(frame,palette.grass,shadows,4,timers,nullptr);
            const auto pixels=read_colors(picture,900,600);
            std::vector<std::array<std::uint8_t,3>> rgb;
            for(const auto& pixel:pixels) {rgb.push_back({static_cast<std::uint8_t>(255*pixel.r),static_cast<std::uint8_t>(255*pixel.g),
                                                       static_cast<std::uint8_t>(255*pixel.b)});}
            write_check_bmp(out/("grass_rings_"+std::to_string(fairways)+"_"+std::to_string(static_cast<int>(distance))+".bmp"),900,600,rgb);
            ++result.pictures;
        }
    }
    // A fixed rough patch through both sides of each density boundary:
    // roots and heights stay put, and projected coverage changes smoothly.
    glm::vec2 at(0.0f);float ground=0.0f;
    const bool found=for_each_grass_node(course.grass,137,[&](const glm::vec2& place,const glm::vec3& node) {
        if(node.y<5) {return false;}
        at=place;ground=node.x;return true;});
    if(found) {
        const auto vp=glm::ortho(-2.0f,2.0f,-2.0f,2.0f,0.1f,200.0f)*
            glm::lookAt(glm::vec3(at.x,ground+100,at.y),glm::vec3(at.x,ground,at.y),glm::vec3(0,0,1));
        for(int level=0;level<static_cast<int>(std::round(palette.grass.rings));++level) {
            std::vector<glm::vec4> before;
            const float boundary=std::ldexp(palette.grass.reach_meters,level);
            for(const float delta:{-0.01f,0.01f}) {
                const glm::vec3 eye(at.x,ground+1.7f,at.y-boundary-delta);
                scene_haze haze;haze.eye=eye;haze.full_distance=100000;
                lighting.upload(light,haze,{},nullptr);
                picture.bind();glViewport(0,0,512,512);glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
                blades.draw({&course.grass,vp,eye,palette.colors.ground.rough,{12,0},7,512,{}},
                             palette.grass,shadows,4,timers,nullptr);
                auto image=read_colors(picture,512,512);
                if(before.empty()) {before=std::move(image);continue;}
                int changed=0,covered=0;
                for(std::size_t pixel=0;pixel<image.size();++pixel) {
                    const bool a=before[pixel].a>0.5f,b=image[pixel].a>0.5f;
                    changed+=a!=b ? 1:0;covered+=a||b ? 1:0;
                }
                const bool ok=changed<=8+covered/50;
                result.wrong_pixels+=ok ? 0:1;
                std::printf("  grass %.0f m boundary: %d / %d coverage changed, %s\n",boundary,changed,covered,ok?"ok":"FAILED");
                result.pictures+=2;
            }
        }
    }
    std::printf("  ground views: %lld root pixels on bare ground\n",result.wrong_pixels);
    diagnostic.shutdown();roots.shutdown();blades.shutdown();lighting.shutdown();shadows.shutdown();picture.shutdown();timers.shutdown();
    result.ran=true;
    return result;
}
