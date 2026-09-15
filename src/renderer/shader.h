#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include "profiling/profiling.h"

struct shader_program {
    bool load_from_files(const char* vertex_path, const char* fragment_path);
    void shutdown();
    void use() const;

    void set_mat4(const char* name, const glm::mat4& value) const;
    void set_vec3(const char* name, const glm::vec3& value) const;
    void set_vec2(const char* name, const glm::vec2& value) const;
    void set_float(const char* name, float value) const;
    void set_int(const char* name, int value) const;

    unsigned int id() const { return program_; }

    // Optional, non-owning profiling sink. The renderer points this at the
    // frame profile it is recording into (null when profiling is off) so
    // uniform sets are counted without any global state. Drawing helpers read
    // it back through profile() to count draw calls from the same sink.
    void set_profile(frame_profile* profile) { profile_ = profile; }
    frame_profile* profile() const { return profile_; }

private:
    unsigned int program_ = 0;
    frame_profile* profile_ = nullptr;
};
