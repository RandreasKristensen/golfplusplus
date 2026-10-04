#include "module_cache.h"

#include "embedded_content.h"

#include <map>
#include <optional>
#include <utility>

namespace {
struct module_cache {
    bool loaded = false;
    content_files files;
    server_content_load_result content;
    std::map<std::string, std::optional<server_course>> courses;
};

module_cache cache;

module_cache& loaded_cache() {
    if (!cache.loaded) {
        for (std::size_t i = 0; i < embedded_file_count; ++i) {
            cache.files.emplace(embedded_files[i].path, embedded_files[i].text);
        }
        cache.content = parse_server_content(cache.files);
        cache.loaded = true;
    }
    return cache;
}
}

const server_content* cached_content() {
    const module_cache& loaded = loaded_cache();
    return loaded.content.content ? &*loaded.content.content : nullptr;
}

const std::string& content_error() {
    return loaded_cache().content.error;
}

const server_course* cached_course(const std::string& course_id) {
    module_cache& loaded = loaded_cache();
    // Only known ids are memoised, so made-up ones cannot grow the cache.
    if (!loaded.content.content || find_server_course(*loaded.content.content, course_id) == nullptr) {
        return nullptr;
    }
    auto found = loaded.courses.find(course_id);
    if (found == loaded.courses.end()) {
        found = loaded.courses.emplace(course_id, build_server_course(loaded.files, *loaded.content.content, course_id)).first;
    }
    return found->second ? &*found->second : nullptr;
}
