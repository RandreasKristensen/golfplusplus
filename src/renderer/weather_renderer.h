#pragma once

// Draws the weather's moving things (render_weather): rain streaks and the
// wind's specks, each one draw of lines from a buffer of random places
// filled once at init (assets/picture/shaders/weather_streak.vert wraps them round
// the camera). Blended over the scene, depth-tested but never written.

#include "profiling/profiling.h"
#include "renderer/render_data.h"
#include "renderer/shader.h"

#include <string>

#include <glm/mat4x4.hpp>

class weather_renderer {
public:
    // `max_streaks` is the most either draw ever shows.
    bool init(const std::string& vertex_path, const std::string& fragment_path, int max_streaks);
    void shutdown();
    void draw(const render_weather& weather, const glm::mat4& view_proj, frame_profile* profile);

private:
    void draw_streaks(const render_streaks& streaks);

    shader_program shader_;
    unsigned int vao_ = 0;
    unsigned int vbo_ = 0;
    int max_streaks_ = 0;
};
