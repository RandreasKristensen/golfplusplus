#include "renderer/lighting_uniforms.h"

#include "renderer/gl_loader.h"

bool lighting_uniforms::init() {
    shutdown();
    glGenBuffers(1, &buffer_);
    glBindBuffer(GL_UNIFORM_BUFFER, buffer_);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(frame_lighting_block), nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, frame_lighting_binding, buffer_);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
    return buffer_ != 0;
}

void lighting_uniforms::shutdown() {
    if (buffer_ != 0) {
        glDeleteBuffers(1, &buffer_);
        buffer_ = 0;
    }
}

void lighting_uniforms::upload(const render_lighting& lighting, const scene_haze& haze,
                               const std::vector<render_light>& lights, frame_profile* profile) {
    const frame_lighting_block block = make_frame_lighting_block(lighting, haze, nearest_scene_lights(lights, haze.eye));
    glBindBuffer(GL_UNIFORM_BUFFER, buffer_);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(block), &block);
    glBindBufferBase(GL_UNIFORM_BUFFER, frame_lighting_binding, buffer_);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
    record_buffer_write(profile, sizeof(block));
}
