#pragma once

#include "renderer/gl_storage.h"
#include <array>
#include <cstddef>
#include <cstring>
#include <string>

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include "profiling/profiling.h"

// Fixed-size, allocation-free map from uniform name to location for one linked
// program. Locations are stable from a successful link until the program is
// deleted or relinked, so each name only needs one glGetUniformLocation call.
//
// Keys are compared by content (strcmp against an inline copy), never by
// pointer: identical literals may live at different addresses across TUs, and
// a non-literal name's address can be reused for different text later.
// Negative locations (uniform optimised out) are cached like any other.
//
// Kept free of GL so it can be unit tested; the actual query is passed in.
class uniform_location_cache {
public:
    // Above the most names any one program sets (backdrop.frag sets 35, lit
    // scene programs a few dozen with their lamp arrays).
    static constexpr std::size_t capacity = 128;
    static constexpr std::size_t max_name_length = 47;

    // Returns the cached location for `name`, calling `query(name)` (which
    // must return an int location) on a miss. Names too long to store, or
    // arriving after the table is full, are queried every call — still
    // correct, just uncached.
    template <typename query_fn>
    int find_or_query(const char* name, query_fn&& query) {
        // Setters tend to run in the same order every draw, so start scanning
        // just after the previous hit: the common case is a single strcmp.
        for (std::size_t step = 0; step < count_; ++step) {
            std::size_t index = next_ + step;
            if (index >= count_) {
                index -= count_;
            }
            const entry& candidate = entries_[index];
            if (std::strcmp(candidate.name, name) == 0) {
                next_ = index + 1 < count_ ? index + 1 : 0;
                return candidate.location;
            }
        }

        const int location = query(name);
        const std::size_t length = std::strlen(name);
        if (count_ < capacity && length <= max_name_length) {
            entry& slot = entries_[count_];
            std::memcpy(slot.name, name, length + 1);
            slot.location = location;
            ++count_;
            next_ = 0;
        }
        return location;
    }

    void clear() {
        count_ = 0;
        next_ = 0;
    }

    std::size_t size() const { return count_; }

private:
    struct entry {
        char name[max_name_length + 1] = {};
        int location = -1;
    };

    std::array<entry, capacity> entries_{};
    std::size_t count_ = 0;
    std::size_t next_ = 0;
};

class shader_program {
public:
    explicit shader_program(gl_storage storage = {})
        : storage_(storage) {}

    // `defines` (whole #define lines) go into both stages after #version.
    bool load_from_files(const std::string& vertex_path, const std::string& fragment_path,
                         const std::string& defines = std::string());
    void shutdown();
    void use() const;

    void set_mat4(const char* name, const glm::mat4& value) const;
    void set_vec3(const char* name, const glm::vec3& value) const;
    void set_vec2(const char* name, const glm::vec2& value) const;
    void set_vec4(const char* name, const glm::vec4& value) const;
    void set_vec4_array(const char* name, const glm::vec4* values, int count) const;
    void set_float(const char* name, float value) const;
    void set_int(const char* name, int value) const;

    unsigned int id() const { return program_; }

    // Standalone passes can supply a frame directly. The game's passes share the renderer's ledger.
    void set_profile(frame_profile* profile) { profile_ = profile; }
    frame_profile* profile() const { return storage_.profile(profile_); }

private:
    gl_storage storage_;
    int uniform_location(const char* name) const;

    unsigned int program_ = 0;
    frame_profile* profile_ = nullptr;
    // Filled lazily by the const setters; cleared on shutdown/reload.
    mutable uniform_location_cache uniform_locations_;
};
