#include "renderer/shader.h"

#include <SDL.h>

#include <fstream>
#include <sstream>
#include <string>

#include "core/gl_loader.h"

namespace {
std::string read_text_file(const char* path, bool* ok) {
    std::ifstream file(path, std::ios::in);
    if (!file.is_open()) {
        if (ok) {
            *ok = false;
        }
        return {};
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();

    if (ok) {
        *ok = true;
    }
    return buffer.str();
}

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

bool shader_program::load_from_files(const char* vertex_path, const char* fragment_path) {
    shutdown();

    bool ok = false;
    const std::string vertex_source = read_text_file(vertex_path, &ok);
    if (!ok) {
        SDL_Log("Failed to read vertex shader: %s", vertex_path);
        return false;
    }

    const std::string fragment_source = read_text_file(fragment_path, &ok);
    if (!ok) {
        SDL_Log("Failed to read fragment shader: %s", fragment_path);
        return false;
    }

    unsigned int vertex_shader = compile_stage(GL_VERTEX_SHADER, vertex_source.c_str(), vertex_path);
    if (!vertex_shader) {
        return false;
    }

    unsigned int fragment_shader = compile_stage(GL_FRAGMENT_SHADER, fragment_source.c_str(), fragment_path);
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
    return true;
}

void shader_program::shutdown() {
    if (program_ != 0) {
        glDeleteProgram(program_);
        program_ = 0;
    }
}

void shader_program::use() const {
    if (program_ != 0) {
        glUseProgram(program_);
    }
}

void shader_program::set_mat4(const char* name, const glm::mat4& value) const {
    const int location = glGetUniformLocation(program_, name);
    if (location >= 0) {
        glUniformMatrix4fv(location, 1, GL_FALSE, &value[0][0]);
    }
}

void shader_program::set_vec3(const char* name, const glm::vec3& value) const {
    const int location = glGetUniformLocation(program_, name);
    if (location >= 0) {
        glUniform3fv(location, 1, &value[0]);
    }
}

void shader_program::set_vec2(const char* name, const glm::vec2& value) const {
    const int location = glGetUniformLocation(program_, name);
    if (location >= 0) {
        glUniform2fv(location, 1, &value[0]);
    }
}

void shader_program::set_int(const char* name, int value) const {
    const int location = glGetUniformLocation(program_, name);
    if (location >= 0) {
        glUniform1i(location, value);
    }
}
