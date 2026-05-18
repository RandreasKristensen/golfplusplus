#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

struct shader_program {
    bool load_from_files(const char* vertex_path, const char* fragment_path);
    void shutdown();
    void use() const;

    void set_mat4(const char* name, const glm::mat4& value) const;
    void set_vec3(const char* name, const glm::vec3& value) const;
    void set_vec2(const char* name, const glm::vec2& value) const;
    void set_int(const char* name, int value) const;

    unsigned int id() const { return program_; }

private:
    unsigned int program_ = 0;
};
