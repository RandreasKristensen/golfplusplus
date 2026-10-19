#include "renderer/shader.h"

#include <SDL.h>

#include <filesystem>
#include <optional>
#include <string>

#include "game/content_files.h"
#include "renderer/gl_loader.h"
#include "renderer/lighting_uniforms.h"
#include "renderer/shader_source.h"

namespace {
unsigned int compile_stage(unsigned int type, const char* source, const char* label) {
    unsigned int shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    int success = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char info_log[1024];
        glGetShaderInfoLog(shader, sizeof(info_log), nullptr, info_log);
        SDL_Log("Shader compile failed (%s): %s", label, info_log);
        glDeleteShader(shader);
        return 0;
    }

    return shader;
}

// The file's source with its includes, read from the shader's own folder.
std::optional<std::string> read_shader_source(const std::string& path) {
    const std::optional<std::string> source = read_text_file(path);
    if (!source) {
        return std::nullopt;
    }
    const std::filesystem::path folder = std::filesystem::path(path).parent_path();
    return expand_shader_includes(*source, [&folder](const std::string& name) { return read_text_file(folder / name); });
}

bool link_program(unsigned int program) {
    glLinkProgram(program);

    int success = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char info_log[1024];
        glGetProgramInfoLog(program, sizeof(info_log), nullptr, info_log);
        SDL_Log("Shader link failed: %s", info_log);
        return false;
    }

    return true;
}
}

bool shader_program::load_from_files(const std::string& vertex_path, const std::string& fragment_path) {
    shutdown();

    const std::optional<std::string> vertex_source = read_shader_source(vertex_path);
    const std::optional<std::string> fragment_source = read_shader_source(fragment_path);
    if (!vertex_source || !fragment_source) {
        SDL_Log("Failed to read shader: %s", (!vertex_source ? vertex_path : fragment_path).c_str());
        return false;
    }

    const unsigned int vertex_shader = compile_stage(GL_VERTEX_SHADER, vertex_source->c_str(), vertex_path.c_str());
    if (!vertex_shader) {
        return false;
    }

    const unsigned int fragment_shader = compile_stage(GL_FRAGMENT_SHADER, fragment_source->c_str(), fragment_path.c_str());
    if (!fragment_shader) {
        glDeleteShader(vertex_shader);
        return false;
    }

    program_ = glCreateProgram();
    glAttachShader(program_, vertex_shader);
    glAttachShader(program_, fragment_shader);

    if (!link_program(program_)) {
        glDeleteShader(vertex_shader);
        glDeleteShader(fragment_shader);
        shutdown();
        return false;
    }

    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);

    const unsigned int lighting_block = glGetUniformBlockIndex(program_, frame_lighting_block_name);
    if (lighting_block != GL_INVALID_INDEX) {
        glUniformBlockBinding(program_, lighting_block, frame_lighting_binding);
    }
    return true;
}

void shader_program::shutdown() {
    // Locations belong to the program object; any relink invalidates them.
    uniform_locations_.clear();
    if (program_ != 0) {
        glDeleteProgram(program_);
        program_ = 0;
    }
}

int shader_program::uniform_location(const char* name) const {
    if (program_ == 0 || name == nullptr) {
        return -1;
    }

    return uniform_locations_.find_or_query(name, [this](const char* uniform_name) {
        record_uniform_location_query(profile_);
        return static_cast<int>(glGetUniformLocation(program_, uniform_name));
    });
}

void shader_program::use() const {
    if (program_ != 0) {
        glUseProgram(program_);
    }
}

void shader_program::set_mat4(const char* name, const glm::mat4& value) const {
    const int location = uniform_location(name);
    if (location >= 0) {
        glUniformMatrix4fv(location, 1, GL_FALSE, &value[0][0]);
        record_uniform_set(profile_);
    }
}

void shader_program::set_vec3(const char* name, const glm::vec3& value) const {
    const int location = uniform_location(name);
    if (location >= 0) {
        glUniform3fv(location, 1, &value[0]);
        record_uniform_set(profile_);
    }
}

void shader_program::set_vec2(const char* name, const glm::vec2& value) const {
    const int location = uniform_location(name);
    if (location >= 0) {
        glUniform2fv(location, 1, &value[0]);
        record_uniform_set(profile_);
    }
}

void shader_program::set_vec4(const char* name, const glm::vec4& value) const {
    const int location = uniform_location(name);
    if (location >= 0) {
        glUniform4fv(location, 1, &value[0]);
        record_uniform_set(profile_);
    }
}

void shader_program::set_float(const char* name, const float value) const {
    const int location = uniform_location(name);
    if (location >= 0) {
        glUniform1f(location, value);
        record_uniform_set(profile_);
    }
}

void shader_program::set_int(const char* name, int value) const {
    const int location = uniform_location(name);
    if (location >= 0) {
        glUniform1i(location, value);
        record_uniform_set(profile_);
    }
}
