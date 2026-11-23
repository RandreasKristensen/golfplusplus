#pragma once

// Renderer-owned ledger of live GL storage requests and the transfers into them.
#include <cstddef>
#include <cstdint>
#include <unordered_map>

struct frame_profile;
enum class upload_kind { dynamic, cached };

class allocation_table {
public:
    bool replace(const unsigned id, const std::uint64_t bytes) {
        auto& previous = objects_[id];
        const bool grew = bytes > previous;
        bytes_ = bytes_ - previous + bytes;
        previous = bytes;
        return grew;
    }

    void erase(const unsigned id) {
        const auto found = objects_.find(id);
        if (found != objects_.end()) {
            bytes_ -= found->second;
            objects_.erase(found);
        }
    }
    std::uint64_t bytes() const { return bytes_; }
    std::uint64_t bytes(const unsigned id) const {
        const auto found = objects_.find(id);
        return found == objects_.end() ? 0 : found->second;
    }
private:
    std::unordered_map<unsigned, std::uint64_t> objects_;
    std::uint64_t bytes_ = 0;
};

struct gpu_allocations {
    allocation_table buffers;
    allocation_table textures;
    allocation_table renderbuffers;
    frame_profile* frame = nullptr;
};

// Copies refer to the same renderer. Standalone picture checks can omit the ledger.
struct gl_storage {
    gpu_allocations* ledger = nullptr;
    frame_profile* profile(frame_profile* standalone = nullptr) const { return ledger ? ledger->frame : standalone; }
    void buffer(unsigned id, unsigned target, std::size_t bytes, const void* data, unsigned usage,
                frame_profile* standalone = nullptr) const;
    void write(unsigned target, std::size_t offset, std::size_t bytes, const void* data, upload_kind kind,
               frame_profile* standalone = nullptr) const;
    void texture_image(unsigned id, unsigned internal_format, int width, int height, unsigned format, unsigned type,
                       const void* data, bool mipmaps = false) const;
    // A GL_TEXTURE_2D_ARRAY of `layers` layers, left undefined, with every mip level when
    // `mipmaps` (the caller fills level 0 and glGenerateMipmap). Bind it first.
    void texture_array(unsigned id, unsigned internal_format, int width, int height, int layers, unsigned format,
                       unsigned type, bool mipmaps = false) const;
    // Replaces one whole layer of the bound array; the caller sets the pixel-unpack state.
    void texture_layer(unsigned format, unsigned type, int width, int height, int layer, const void* data,
                       std::size_t bytes) const;
    void depth_stencil(unsigned id, int width, int height) const;
    void delete_texture(unsigned& id) const;
    void delete_renderbuffer(unsigned& id) const;
    void delete_buffer(unsigned& id) const;
};
