#include "renderer/grass_field.h"

#include <algorithm>
#include <array>
#include <utility>
#include <cmath>
#include <glm/common.hpp>
#include <glm/geometric.hpp>

namespace {
float cross(const glm::vec2& a, const glm::vec2& b) { return a.x * b.y - a.y * b.x; }

float edge_distance_squared(const glm::vec2& p, const glm::vec2& a, const glm::vec2& b) {
    const glm::vec2 ab = b - a;
    const float t = std::clamp(glm::dot(p - a, ab) / std::max(glm::dot(ab, ab), 1.0e-12f), 0.0f, 1.0f);
    const glm::vec2 delta = p - a - t * ab;
    return glm::dot(delta, delta);
}

// Nearest exact bare edge, indexed once for the course. Interior nodes are
// marked by rasterization; outside a union its nearest triangle edge is on
// that union's boundary, so shared and overlapping edges are harmless.
class bare_distance {
    struct edge { glm::vec2 a,b; };
    struct node {
        glm::vec2 low{0},high{0};
        int first=0,count=0,left=0,right=0;
    };
public:
    void add(const glm::vec2& a,const glm::vec2& b) {
        if(a==b) {return;}
        edges_.push_back(a.x<b.x || (a.x==b.x && a.y<b.y) ? edge{a,b}:edge{b,a});
    }
    void build() {
        const auto key=[](const edge& e){return std::array<float,4>{e.a.x,e.a.y,e.b.x,e.b.y};};
        std::sort(edges_.begin(),edges_.end(),[&](const edge& a,const edge& b){return key(a)<key(b);});
        edges_.erase(std::unique(edges_.begin(),edges_.end(),[](const edge& a,const edge& b){return a.a==b.a && a.b==b.b;}),edges_.end());
        nodes_.reserve(edges_.size()*2);
        if(!edges_.empty()) {branch(0,static_cast<int>(edges_.size()));}
    }
    float squared(const glm::vec2& p) const {
        const float cap=grass_field_distance_cap*grass_field_distance_cap;
        return nodes_.empty() ? cap:nearest(0,p,cap);
    }
private:
    static float box_distance(const node& n,const glm::vec2& p) {
        const auto d=glm::max(glm::max(n.low-p,p-n.high),glm::vec2(0));
        return glm::dot(d,d);
    }
    int branch(const int first,const int count) {
        node n;
        n.low=glm::min(edges_[first].a,edges_[first].b);
        n.high=glm::max(edges_[first].a,edges_[first].b);
        for(int i=first+1;i<first+count;++i) {
            n.low=glm::min(n.low,glm::min(edges_[i].a,edges_[i].b));
            n.high=glm::max(n.high,glm::max(edges_[i].a,edges_[i].b));
        }
        const int id=static_cast<int>(nodes_.size());
        nodes_.push_back(n);
        if(count<=8) {n.first=first;n.count=count;}
        else {
            const int axis=n.high.x-n.low.x>n.high.y-n.low.y ? 0:1;
            const int half=count/2;
            std::nth_element(edges_.begin()+first,edges_.begin()+first+half,edges_.begin()+first+count,
                [axis](const edge& a,const edge& b){return a.a[axis]+a.b[axis]<b.a[axis]+b.b[axis];});
            n.left=branch(first,half);n.right=branch(first+half,count-half);
        }
        nodes_[id]=n;
        return id;
    }
    float nearest(const int id,const glm::vec2& p,float best) const {
        const auto& n=nodes_[id];
        if(box_distance(n,p)>=best) {return best;}
        if(n.count) {
            for(int i=n.first;i<n.first+n.count;++i) {best=std::min(best,edge_distance_squared(p,edges_[i].a,edges_[i].b));}
            return best;
        }
        const bool left_first=box_distance(nodes_[n.left],p)<box_distance(nodes_[n.right],p);
        best=nearest(left_first ? n.left:n.right,p,best);
        return nearest(left_first ? n.right:n.left,p,best);
    }
    std::vector<edge> edges_;
    std::vector<node> nodes_;
};

// Rasterize world geometry at the field's nodes, never at camera texels.
// Mark bare interiors; the edge index keeps sub-cell slivers too, so a
// thin path cannot disappear between samples.
void triangle(grass_field& field, const render_terrain_vertex& a, const render_terrain_vertex& b,
              const render_terrain_vertex& c, const bool heights, const bool kind_only = false) {
    const bool bare = a.blades < 0.5f && b.blades < 0.5f && c.blades < 0.5f;
    if (!heights && !bare) { return; }
    const glm::vec2 pa(a.position.x, a.position.z), pb(b.position.x, b.position.z), pc(c.position.x, c.position.z);
    const float area = cross(pb - pa, pc - pa);
    if (std::abs(area) < 1.0e-10f) { return; }
    const float margin = 0.0f;
    const glm::vec2 low = (glm::min(pa, glm::min(pb, pc)) - glm::vec2(margin) - field.origin) / grass_field_spacing;
    const glm::vec2 high = (glm::max(pa, glm::max(pb, pc)) + glm::vec2(margin) - field.origin) / grass_field_spacing;
    const int x0 = std::max(0, static_cast<int>(std::ceil(low.x)));
    const int z0 = std::max(0, static_cast<int>(std::ceil(low.y)));
    const int x1 = std::min(field.columns - 1, static_cast<int>(std::floor(high.x)));
    const int z1 = std::min(field.rows - 1, static_cast<int>(std::floor(high.y)));
    for (int z = z0; z <= z1; ++z) {
        for (int x = x0; x <= x1; ++x) {
            const glm::vec2 p = field.origin + glm::vec2(x, z) * grass_field_spacing;
            const float wb = cross(p - pa, pc - pa) / area;
            const float wc = cross(pb - pa, p - pa) / area;
            const float wa = 1.0f - wb - wc;
            const bool inside = std::min(wa, std::min(wb, wc)) >= -1.0e-5f;
            glm::vec3& sample = field.samples[static_cast<std::size_t>(z) * field.columns + x];
            if (heights) {
                if (inside) {
                    if (!kind_only) { sample.x = wa * a.position.y + wb * b.position.y + wc * c.position.y; }
                    sample.z = std::max(1.0f, wa * a.blades + wb * b.blades + wc * c.blades);
                }
            } else if (inside) {
                sample.y = 0.0f;
            }
        }
    }
}

void mesh(grass_field& field, const render_static_mesh& source, const bool heights, bare_distance* distance = nullptr) {
    for (std::size_t i = 0; i + 2 < source.indices.size(); i += 3) {
        const auto& a=source.vertices[source.indices[i]];
        const auto& b=source.vertices[source.indices[i+1]];
        const auto& c=source.vertices[source.indices[i+2]];
        triangle(field,a,b,c,heights);
        if(distance && a.blades<0.5f && b.blades<0.5f && c.blades<0.5f) {
            const glm::vec2 pa(a.position.x,a.position.z),pb(b.position.x,b.position.z),pc(c.position.x,c.position.z);
            distance->add(pa,pb);distance->add(pb,pc);distance->add(pc,pa);
        }
    }
}
}

grass_field bake_grass_field(const render_static_mesh& ground, const render_static_mesh& overlay,
                              const std::vector<tee_box>& tees) {
    grass_field field;
    field.revision = ground.revision;
    if (!ground.bounds.valid || ground.indices.empty()) { return field; }
    field.origin = glm::floor(glm::vec2(ground.bounds.min.x, ground.bounds.min.z) / grass_field_spacing) * grass_field_spacing;
    const glm::vec2 end(ground.bounds.max.x, ground.bounds.max.z);
    const glm::ivec2 size = glm::ivec2(glm::ceil((end - field.origin) / grass_field_spacing)) + 1;
    field.columns = size.x;
    field.rows = size.y;
    field.samples.resize(static_cast<std::size_t>(size.x) * size.y, glm::vec3(-1000.0f, grass_field_distance_cap * grass_field_distance_cap, 0.0f));
    mesh(field, ground, true);
    // Grass kinds from rough overlays; they do not alter the ground height.
    for (std::size_t i = 0; i + 2 < overlay.indices.size(); i += 3) {
        const auto& a = overlay.vertices[overlay.indices[i]];
        const auto& b = overlay.vertices[overlay.indices[i + 1]];
        const auto& c = overlay.vertices[overlay.indices[i + 2]];
        if (a.blades >= 1.5f && b.blades >= 1.5f && c.blades >= 1.5f) { triangle(field, a, b, c, true, true); }
    }
    bare_distance distance;
    mesh(field, ground, false, &distance);
    mesh(field, overlay, false, &distance);
    for (const tee_box& tee : tees) {
        const glm::vec3 along = tee.down_hole * tee.half_length;
        const glm::vec3 across(-tee.down_hole.z * tee.half_width, 0.0f, tee.down_hole.x * tee.half_width);
        render_terrain_vertex corners[4];
        corners[0].position = tee.center - along - across;
        corners[1].position = tee.center + along - across;
        corners[2].position = tee.center + along + across;
        corners[3].position = tee.center - along + across;
        for(int i=0;i<4;++i) {
            const auto& a=corners[i].position;
            const auto& b=corners[(i+1)%4].position;
            distance.add({a.x,a.z},{b.x,b.z});
        }
        triangle(field, corners[0], corners[1], corners[2], false);
        triangle(field, corners[0], corners[2], corners[3], false);
    }
    distance.build();
    // No blades outside the ground; keep its border conservative too.
    for (int z = 0; z < field.rows; ++z) {
        for (int x = 0; x < field.columns; ++x) {
            auto& sample = field.samples[static_cast<std::size_t>(z) * field.columns + x];
            const float border = grass_field_spacing * static_cast<float>(std::min({x,z,field.columns-1-x,field.rows-1-z}));
            if(sample.z==0.0f) {sample.y=0.0f;}
            else if(sample.y>0.0f) {
                const auto point=field.origin+glm::vec2(x,z)*grass_field_spacing;
                sample.y=std::sqrt(std::min(distance.squared(point),border*border));
            }
        }
    }
    return field;
}

glm::vec3 sample_grass_field(const grass_field& field, const glm::vec2& root) {
    if (field.columns < 2 || field.rows < 2) { return glm::vec3(0.0f); }
    const glm::vec2 at = (root - field.origin) / grass_field_spacing;
    if (at.x < 0 || at.y < 0 || at.x >= field.columns - 1 || at.y >= field.rows - 1) { return glm::vec3(0.0f); }
    const glm::ivec2 low = glm::ivec2(glm::floor(at));
    const glm::vec2 f = at - glm::vec2(low);
    const std::size_t i = static_cast<std::size_t>(low.y) * field.columns + low.x;
    return glm::mix(glm::mix(field.samples[i], field.samples[i + 1], f.x),
                    glm::mix(field.samples[i + field.columns], field.samples[i + field.columns + 1], f.x), f.y);
}
