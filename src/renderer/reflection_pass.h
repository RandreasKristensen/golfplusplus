#pragma once

// Draws the actual scene at quarter scene resolution for each water level the
// view may show (water_candidate). A candidate with a query is drawn under
// conditional rendering on it, so the GPU skips a level whose water is hidden
// this frame and draws whenever the result is not yet known. The callback is
// the caller's ordinary scene path, without water (no recursion); it receives
// the mirrored, surface-clipped view, cropped to the candidate's coverage
// rectangle (renderer/water_crop.h): drawn into a viewport of the crop's size
// at the corner of a target as large as the whole quarter-scale picture, so
// targets never reallocate as the crop moves. Targets are pooled: the pool
// grows to the most levels captured in one frame and is trimmed after it
// stays larger than needed for `pool_idle_frames` frames. `parent` is the
// view being drawn: its framebuffer, viewport and scissor are restored after.
#include "renderer/gl_storage.h"
#include <functional>
#include <vector>
#include "renderer/framebuffer.h"
#include "profiling/profiling.h"
#include "renderer/water_reflection.h"

class reflection_pass {
public:
    explicit reflection_pass(gl_storage storage = {})
        : storage_(storage) {}

    using draw_scene = std::function<void(const water_reflection_view&)>;
    bool draw(const glm::mat4& view, const glm::mat4& projection, const glm::vec3& eye, const draw_target& parent,
              const std::vector<water_candidate>& candidates, const draw_scene& draw, frame_profile* profile = nullptr);
    void shutdown();
    const std::vector<water_reflection>& surfaces() const { return surfaces_; }
    std::size_t pool_size() const { return targets_.size(); }
    static constexpr int pool_idle_frames = 240;
private:
    void trim_pool();
    gl_storage storage_;
    std::vector<framebuffer> targets_;
    std::vector<water_reflection> surfaces_;
    int idle_frames_ = 0;
};
