#include "module_cache.h"

#include "embedded_content.h"

#include <zlib.h>

#include <map>
#include <optional>
#include <string>
#include <utility>

namespace {
struct module_cache {
    bool loaded = false;
    std::string bundle;  // the inflated embedded content; `files` views into it
    content_files files;
    server_content_load_result content;
    std::map<std::string, std::optional<server_course>> courses;
};

module_cache cache;

// Empty unless the gzip stream inflates to exactly `size` bytes.
std::string inflate_gzip(const unsigned char* data, const std::size_t data_size, const std::size_t size) {
    std::string out(size, '\0');
    z_stream stream{};
    if (inflateInit2(&stream, 16 + MAX_WBITS) != Z_OK) {
        return {};
    }
    stream.next_in = const_cast<unsigned char*>(data);
    stream.avail_in = static_cast<uInt>(data_size);
    stream.next_out = reinterpret_cast<unsigned char*>(out.data());
    stream.avail_out = static_cast<uInt>(size);
    const int result = inflate(&stream, Z_FINISH);
    const bool whole = result == Z_STREAM_END && stream.total_out == size;
    inflateEnd(&stream);
    return whole ? out : std::string();
}

module_cache& loaded_cache() {
    if (!cache.loaded) {
        cache.loaded = true;
        cache.bundle = inflate_gzip(embedded_gzip, embedded_gzip_size, embedded_content_size);
        if (cache.bundle.size() != embedded_content_size) {
            cache.content.error = "the module's embedded content does not inflate";
            return cache;
        }
        for (std::size_t i = 0; i < embedded_file_count; ++i) {
            cache.files.emplace(embedded_files[i].path,
                                std::string_view(cache.bundle).substr(embedded_files[i].offset, embedded_files[i].size));
        }
        cache.content = parse_server_content(cache.files);
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
    if (!loaded.content.content || (find_server_course(*loaded.content.content, course_id) == nullptr &&
        (!loaded.content.content->hub || loaded.content.content->hub->id != course_id))) {
        return nullptr;
    }
    auto found = loaded.courses.find(course_id);
    if (found == loaded.courses.end()) {
        found = loaded.courses.emplace(course_id, build_server_course(loaded.files, *loaded.content.content, course_id)).first;
    }
    return found->second ? &*found->second : nullptr;
}
