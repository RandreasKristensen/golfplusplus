#pragma once

// Where a field's packed tiles (grass_pack.h) are kept, by their slot in the
// field's tile list. Tiles are written once, as the baker makes them, and
// never change. A backing is in memory or in a disposable cache file
// (grass_cache.h); reading one tile never touches another. GL-free.

#include <cstddef>
#include <cstdint>
#include <vector>

class grass_tile_backing {
public:
    virtual ~grass_tile_backing() = default;
    // Copies slot's bytes to `out`; false when it holds none or they cannot be read.
    virtual bool read(std::size_t slot, std::vector<std::uint8_t>& out) const = 0;
    // Bytes held in memory.
    virtual std::uint64_t memory_bytes() const = 0;
    // Bytes held in a file.
    virtual std::uint64_t file_bytes() const { return 0; }
};

class grass_memory_backing : public grass_tile_backing {
public:
    explicit grass_memory_backing(const std::size_t slots) : span_(slots) {}
    // Keeps `bytes` as slot's tile; each slot is put once.
    void put(std::size_t slot, const std::vector<std::uint8_t>& bytes);
    bool read(std::size_t slot, std::vector<std::uint8_t>& out) const override;
    std::uint64_t memory_bytes() const override { return bytes_.capacity() + span_.capacity() * sizeof(span); }

private:
    struct span {
        std::uint64_t first = 0;
        std::uint32_t size = 0;
    };
    std::vector<std::uint8_t> bytes_;
    std::vector<span> span_;
};
