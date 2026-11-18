#pragma once

// Draws the actual scene at quarter scene resolution for each visible water
// plane, after current-frame depth queries reject occluded surfaces. The callback is the caller's ordinary scene path, without
// water (no recursion); it receives the mirrored, surface-clipped view.
#include <functional>
#include <vector>
#include "renderer/framebuffer.h"
#include "renderer/gl_timer.h"
#include "renderer/water_reflection.h"

class reflection_pass {
public:
    using draw_scene = std::function<void(const water_reflection_view&)>;
    bool draw(const render_water* water, const glm::mat4& view, const glm::mat4& projection, const glm::vec3& eye,
              gl_timer_pool& timers, const draw_scene& draw, const std::vector<float>* visible = nullptr);
    void shutdown();
    const std::vector<water_reflection>& surfaces() const { return surfaces_; }
private:
    std::vector<framebuffer> targets_;
    std::vector<water_reflection> surfaces_;
};
