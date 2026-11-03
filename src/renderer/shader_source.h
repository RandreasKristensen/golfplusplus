#pragma once

// A shader's source with its includes pulled in: each line that is only
// `#include "name"` is replaced by the text of `name`, read next to the
// shader. One level deep: included files include nothing. A program's
// variant's #defines go in after its #version line. GL-free.

#include <functional>
#include <optional>
#include <string>

using shader_include_reader = std::function<std::optional<std::string>(const std::string& name)>;

// nullopt when an included file cannot be read, or itself includes one.
std::optional<std::string> expand_shader_includes(const std::string& source, const shader_include_reader& read_include);

// `source` with `defines` (whole lines, each ending in a newline) after its
// `#version` line, which must come first; before everything without one.
std::string with_defines(const std::string& source, const std::string& defines);
