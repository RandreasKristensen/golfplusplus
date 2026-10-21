#include "renderer/weather_renderer.h"

#include "renderer/gl_loader.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

namespace {
constexpr GLuint seed_location = 0;
constexpr GLuint end_location = 1;

struct streak_vertex {
    float seed[4];
    float end;
};

// The same places every run (splitmix64), each in [0, 1).
class seed_stream {
public:
    float next() {
        std::uint64_t x = (state_ += 0x9E3779B97F4A7C15ULL);
        x = (x ^ (x >> 30U)) * 0xBF58476D1CE4E5B9ULL;
        x = (x ^ (x >> 27U)) * 0x94D049BB133111EBULL;
        x ^= x >> 31U;
        return static_cast<float>(x >> 40U) / 16777216.0f;
    }

private:
    std::uint64_t state_ = 0;
};

std::vector<streak_vertex> make_streak_vertices(const int count) {
    std::vector<streak_vertex> vertices;
    vertices.reserve(static_cast<std::size_t>(count) * 2U);
    seed_stream random;
    for (int i = 0; i < count; ++i) {
        const streak_vertex head{{random.next(), random.next(), random.next(), random.next()}, 0.0f};
        streak_vertex tail = head;
        tail.end = 1.0f;
        vertices.push_back(head);
        vertices.push_back(tail);
    }
    return vertices;
}
}

bool weather_renderer::init(const std::string& vertex_path, const std::string& fragment_path, const int max_streaks) {
    shutdown();
    if (!shader_.load_from_files(vertex_path, fragment_path)) {
        return false;
    }
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    if (vao_ == 0 || vbo_ == 0) {
        shutdown();
        return false;
    }
    max_streaks_ = std::max(0, max_streaks);
    const std::vector<streak_vertex> vertices = make_streak_vertices(max_streaks_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(streak_vertex)), vertices.data(),
                 GL_STATIC_DRAW);
    glEnableVertexAttribArray(seed_location);
    glVertexAttribPointer(seed_location, 4, GL_FLOAT, GL_FALSE, sizeof(streak_vertex), nullptr);
    glEnableVertexAttribArray(end_location);
    glVertexAttribPointer(end_location, 1, GL_FLOAT, GL_FALSE, sizeof(streak_vertex),
                          reinterpret_cast<void*>(offsetof(streak_vertex, end)));
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    return true;
}

void weather_renderer::shutdown() {
    if (vbo_ != 0) {
        glDeleteBuffers(1, &vbo_);
        vbo_ = 0;
    }
    if (vao_ != 0) {
        glDeleteVertexArrays(1, &vao_);
        vao_ = 0;
    }
    shader_.shutdown();
    max_streaks_ = 0;
}

void weather_renderer::draw(const render_weather& weather, const glm::mat4& view_proj, frame_profile* profile) {
    if (shader_.id() == 0 || (weather.rain.count <= 0 && weather.specks.count <= 0)) {
        return;
    }
    shader_.set_profile(profile);
    shader_.use();
    shader_.set_mat4("u_view_proj", view_proj);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glBindVertexArray(vao_);
    draw_streaks(weather.specks);
    draw_streaks(weather.rain);
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

void weather_renderer::draw_streaks(const render_streaks& streaks) {
    const int count = std::min(streaks.count, max_streaks_);
    if (count <= 0) {
        return;
    }
    shader_.set_vec3("u_center", streaks.center);
    shader_.set_vec3("u_size", streaks.size);
    shader_.set_vec3("u_offset", streaks.offset);
    shader_.set_vec3("u_velocity", streaks.velocity);
    shader_.set_vec3("u_color", streaks.color);
    shader_.set_float("u_water", streaks.water ? 1.0f : 0.0f);
    shader_.set_vec3("u_streak", glm::vec3(streaks.streak_seconds, streaks.alpha, streaks.near_fade));
    shader_.set_vec2("u_tumble", glm::vec2(streaks.tumble, streaks.tumble_phase));
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(count) * 2);
    record_draw_call(shader_.profile());
}
