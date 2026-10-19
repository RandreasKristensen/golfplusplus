#pragma once

// A shader's source with its includes pulled in: each line that is only
// `#include "name"` is replaced by the text of `name`, read next to the
// shader. One level deep: included files include nothing. GL-free.

#include <functional>
#include <optional>
#include <string>

using shader_include_reader = std::function<std::optional<std::string>(const std::string& name)>;

// nullopt when an included file cannot be read, or itself includes one.
std::optional<std::string> expand_shader_includes(const std::string& source, const shader_include_reader& read_include);
