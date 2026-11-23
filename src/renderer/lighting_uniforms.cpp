#include "renderer/lighting_uniforms.h"

#include "renderer/gl_loader.h"

bool lighting_uniforms::init() {
    shutdown();
    glGenBuffers(1, &buffer_);
    glBindBuffer(GL_UNIFORM_BUFFER, buffer_);
    storage_.buffer(buffer_, GL_UNIFORM_BUFFER, sizeof(frame_lighting_block), nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, frame_lighting_binding, buffer_);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
    return buffer_ != 0;
}

void lighting_uniforms::shutdown() {
    storage_.delete_buffer(buffer_);
}

void lighting_uniforms::upload(const render_lighting& lighting, const scene_haze& haze,
                               const std::vector<render_light>& lights, frame_profile* profile) {
    lights_ = nearest_scene_lights(lights, haze.eye);
    block_ = make_frame_lighting_block(lighting, haze, lights_);
    glBindBuffer(GL_UNIFORM_BUFFER, buffer_);
    storage_.write(GL_UNIFORM_BUFFER, 0, sizeof(block_), &block_, upload_kind::dynamic, profile);
    glBindBufferBase(GL_UNIFORM_BUFFER, frame_lighting_binding, buffer_);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
}
