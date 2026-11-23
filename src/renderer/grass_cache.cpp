#include "renderer/grass_cache.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

#include "renderer/grass_pack.h"

namespace {
namespace fs = std::filesystem;

constexpr char file_magic[8] = {'G', 'P', 'P', 'G', 'R', 'A', 'S', 'S'};
constexpr char end_magic[8] = {'G', 'P', 'P', 'E', 'N', 'D', '0', '1'};

struct header {
    char magic[8];
    std::uint32_t version;
    std::uint32_t tile_nodes;
    std::uint64_t key;
    float origin[2];
    std::int32_t columns;
    std::int32_t rows;
    std::int32_t first_tile[2];
    std::int32_t counts[2];
    std::uint64_t slots;
};
static_assert(sizeof(header) == 64, "the file header is fixed");

struct footer {
    std::uint64_t index_offset;
    std::uint64_t index_checksum;
    std::uint64_t slots;
    char magic[8];
};
static_assert(sizeof(footer) == 32, "the file footer is fixed");

struct entry {
    float low;
    float high;
    float max_clearance;
    std::uint32_t size;
    std::uint64_t offset;
    std::uint32_t checksum;
    std::uint32_t reserved;
};
static_assert(sizeof(entry) == 32, "an index entry is fixed");

std::uint64_t rotate_left(const std::uint64_t value, const int bits) { return (value << bits) | (value >> (64 - bits)); }

// A fast 64-bit hash of bytes, eight at a time; it detects damage and change, nothing more.
std::uint64_t hash_bytes(const void* data, const std::size_t size, std::uint64_t seed) {
    constexpr std::uint64_t first = 0x9e3779b185ebca87ull;
    constexpr std::uint64_t second = 0xc2b2ae3d27d4eb4full;
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    std::uint64_t h = seed ^ (size * first);
    std::size_t at = 0;
    for (; at + 8 <= size; at += 8) {
        std::uint64_t word = 0;
        std::memcpy(&word, bytes + at, 8);
        h = rotate_left(h ^ (word * first), 31) * second;
    }
    std::uint64_t tail = 0;
    std::memcpy(&tail, bytes + at, size - at);
    h = rotate_left(h ^ (tail * first), 31) * second;
    h ^= h >> 33;
    h *= first;
    return h ^ (h >> 29);
}

template <typename value>
std::uint64_t hash_value(const value& item, const std::uint64_t seed) { return hash_bytes(&item, sizeof(value), seed); }

std::uint32_t checksum_of(const std::vector<std::uint8_t>& bytes) {
    const std::uint64_t h = hash_bytes(bytes.data(), bytes.size(), 0x51ed270b);
    return static_cast<std::uint32_t>(h ^ (h >> 32));
}

std::string file_name(const std::uint64_t key) {
    char name[40];
    std::snprintf(name, sizeof(name), "grass-%016llx.cache", static_cast<unsigned long long>(key));
    return name;
}

header header_of(const grass_cache_shape& shape) {
    header head{};
    std::memcpy(head.magic, file_magic, 8);
    head.version = grass_cache_version;
    head.tile_nodes = grass_tile_nodes;
    head.key = shape.key;
    head.origin[0] = shape.lattice.origin.x;
    head.origin[1] = shape.lattice.origin.y;
    head.columns = shape.lattice.columns;
    head.rows = shape.lattice.rows;
    head.first_tile[0] = shape.extent.first_tile.x;
    head.first_tile[1] = shape.extent.first_tile.y;
    head.counts[0] = shape.extent.counts.x;
    head.counts[1] = shape.extent.counts.y;
    head.slots = static_cast<std::uint64_t>(shape.extent.counts.x) * static_cast<std::uint64_t>(shape.extent.counts.y);
    return head;
}

class file_backing : public grass_tile_backing {
public:
    file_backing(std::ifstream&& stream, std::vector<entry>&& entries, const std::uint64_t bytes)
        : stream_(std::move(stream)), entries_(std::move(entries)), bytes_(bytes) {}
    bool read(const std::size_t slot, std::vector<std::uint8_t>& out) const override {
        if (slot >= entries_.size() || entries_[slot].size == 0) { return false; }
        const entry& item = entries_[slot];
        out.resize(item.size);
        stream_.clear();
        stream_.seekg(static_cast<std::streamoff>(item.offset));
        stream_.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(item.size));
        return stream_.good() && checksum_of(out) == item.checksum;
    }
    std::uint64_t memory_bytes() const override { return entries_.capacity() * sizeof(entry); }
    std::uint64_t file_bytes() const override { return bytes_; }

private:
    mutable std::ifstream stream_;
    std::vector<entry> entries_;
    std::uint64_t bytes_;
};

// Removes the oldest cache files beyond the budget, and temporary files left by a
// write that never finished; never `keep`.
void prune(const fs::path& directory, const fs::path& keep) {
    struct found {
        fs::path path;
        std::uint64_t size;
        fs::file_time_type time;
    };
    std::vector<found> files;
    std::error_code error;
    const auto now = fs::file_time_type::clock::now();
    for (fs::directory_iterator at(directory, error), end; !error && at != end; at.increment(error)) {
        std::error_code item_error;
        const fs::path path = at->path();
        const std::string name = path.filename().string();
        if (name.rfind("grass-", 0) != 0 || !at->is_regular_file(item_error)) { continue; }
        const auto time = fs::last_write_time(path, item_error);
        const auto size = fs::file_size(path, item_error);
        if (item_error) { continue; }
        if (path.extension() == ".tmp") {
            if (now - time > std::chrono::hours(24)) { fs::remove(path, item_error); }
        } else if (path.extension() == ".cache") {
            files.push_back({path, size, time});
        }
    }
    std::sort(files.begin(), files.end(), [](const found& a, const found& b) { return a.time > b.time; });
    std::uint64_t total = 0;
    for (const found& file : files) {
        total += file.size;
        if (total > grass_cache_budget_bytes && file.path != keep) {
            std::error_code remove_error;
            fs::remove(file.path, remove_error);
        }
    }
}
}

std::uint64_t grass_content_key(const render_static_mesh& ground, const render_static_mesh& overlay, const std::vector<tee_box>& tees) {
    std::uint64_t h = hash_value(grass_cache_version, 0x67726173);
    // Everything the bake and the packing read besides the meshes.
    const std::array<float, 8> constants{grass_field_spacing, grass_field_error, grass_field_distance_cap, grass_height_step,
                                        grass_no_ground_height, static_cast<float>(grass_tile_cells), static_cast<float>(grass_tile_nodes),
                                        static_cast<float>(sizeof(grass_tile_info))};
    h = hash_value(constants, h);
    std::vector<float> chunk;
    for (const render_static_mesh* mesh : {&ground, &overlay}) {
        h = hash_value(mesh->vertices.size(), h);
        h = hash_value(mesh->bounds.valid, h);
        h = hash_value(mesh->bounds.min, h);
        h = hash_value(mesh->bounds.max, h);
        chunk.clear();
        for (const render_terrain_vertex& v : mesh->vertices) {
            chunk.insert(chunk.end(), {v.position.x, v.position.y, v.position.z, v.blades()});
            if (chunk.size() >= 4096) {
                h = hash_bytes(chunk.data(), chunk.size() * sizeof(float), h);
                chunk.clear();
            }
        }
        h = hash_bytes(chunk.data(), chunk.size() * sizeof(float), h);
        h = hash_value(mesh->indices.size(), h);
        h = hash_bytes(mesh->indices.data(), mesh->indices.size() * sizeof(mesh->indices[0]), h);
    }
    h = hash_value(tees.size(), h);
    for (const tee_box& tee : tees) {
        const std::array<float, 8> corners{tee.center.x, tee.center.y, tee.center.z, tee.down_hole.x, tee.down_hole.z, tee.half_width,
                                          tee.half_length, tee.down_hole.y};
        h = hash_value(corners, h);
    }
    return h;
}

std::shared_ptr<const grass_tile_backing> open_grass_cache(const fs::path& directory, const grass_cache_shape& shape,
                                                           std::vector<grass_tile_info>& infos) {
    const fs::path path = directory / file_name(shape.key);
    std::error_code error;
    if (!fs::is_regular_file(path, error)) { return nullptr; }
    const std::uint64_t size = fs::file_size(path, error);
    const auto damaged = [&]() -> std::shared_ptr<const grass_tile_backing> {
        std::error_code remove_error;
        fs::remove(path, remove_error);
        return nullptr;
    };
    if (error) { return nullptr; }
    std::ifstream stream(path, std::ios::binary);
    const header want = header_of(shape);
    header head{};
    footer tail{};
    if (size < sizeof(header) + sizeof(footer) || !stream.read(reinterpret_cast<char*>(&head), sizeof(head))) { return damaged(); }
    if (std::memcmp(&head, &want, sizeof(header)) != 0) { return damaged(); }
    stream.seekg(static_cast<std::streamoff>(size - sizeof(footer)));
    if (!stream.read(reinterpret_cast<char*>(&tail), sizeof(tail)) || std::memcmp(tail.magic, end_magic, 8) != 0 || tail.slots != want.slots ||
        tail.index_offset < sizeof(header) || tail.index_offset + want.slots * sizeof(entry) + sizeof(footer) != size) {
        return damaged();
    }
    std::vector<entry> entries(static_cast<std::size_t>(want.slots));
    stream.seekg(static_cast<std::streamoff>(tail.index_offset));
    if (!stream.read(reinterpret_cast<char*>(entries.data()), static_cast<std::streamsize>(entries.size() * sizeof(entry))) ||
        hash_bytes(entries.data(), entries.size() * sizeof(entry), 0) != tail.index_checksum) {
        return damaged();
    }
    infos.assign(entries.size(), grass_tile_info{});
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (entries[i].size != 0 && (entries[i].offset < sizeof(header) || entries[i].offset + entries[i].size > tail.index_offset)) { return damaged(); }
        infos[i] = {entries[i].low, entries[i].high, entries[i].max_clearance};
    }
    // Recent use keeps a file from being pruned as old.
    fs::last_write_time(path, fs::file_time_type::clock::now(), error);
    return std::make_shared<file_backing>(std::move(stream), std::move(entries), size);
}

class grass_cache_writer::state {
public:
    state(const fs::path& directory, const grass_cache_shape& shape) : directory_(directory), shape_(shape) {
        std::error_code error;
        fs::create_directories(directory, error);
        if (error || !fs::is_directory(directory, error)) { return; }
        const auto stamp = static_cast<unsigned long long>(std::chrono::steady_clock::now().time_since_epoch().count());
        char suffix[40];
        std::snprintf(suffix, sizeof(suffix), ".%llx.tmp", stamp ^ reinterpret_cast<std::uintptr_t>(this));
        path_ = directory / file_name(shape.key);
        temporary_ = path_;
        temporary_ += suffix;
        out_.open(temporary_, std::ios::binary | std::ios::trunc);
        const header head = header_of(shape);
        out_.write(reinterpret_cast<const char*>(&head), sizeof(head));
        written_ = sizeof(head);
        entries_.assign(static_cast<std::size_t>(head.slots), entry{});
        ok_ = out_.good();
    }
    ~state() {
        out_.close();
        if (!temporary_.empty() && !finished_) {
            std::error_code error;
            fs::remove(temporary_, error);
        }
    }
    bool ok() const { return ok_; }
    void put(const std::size_t slot, const std::vector<std::uint8_t>& bytes, const grass_tile_info& info) {
        if (!ok_ || slot >= entries_.size()) { return; }
        entry& item = entries_[slot];
        item.low = info.low;
        item.high = info.high;
        item.max_clearance = info.max_clearance;
        if (bytes.empty()) { return; }
        item.size = static_cast<std::uint32_t>(bytes.size());
        item.offset = written_;
        item.checksum = checksum_of(bytes);
        out_.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        written_ += bytes.size();
        ok_ = out_.good();
    }
    std::shared_ptr<const grass_tile_backing> finish() {
        if (!ok_) { return nullptr; }
        footer tail{};
        tail.index_offset = written_;
        tail.index_checksum = hash_bytes(entries_.data(), entries_.size() * sizeof(entry), 0);
        tail.slots = entries_.size();
        std::memcpy(tail.magic, end_magic, 8);
        out_.write(reinterpret_cast<const char*>(entries_.data()), static_cast<std::streamsize>(entries_.size() * sizeof(entry)));
        out_.write(reinterpret_cast<const char*>(&tail), sizeof(tail));
        out_.flush();
        out_.close();
        if (out_.fail()) { return nullptr; }
        std::error_code error;
        fs::rename(temporary_, path_, error);
        if (error) {
            // Windows will not rename over a file; one already there is another copy of this one.
            fs::remove(path_, error);
            fs::rename(temporary_, path_, error);
        }
        if (error) { return nullptr; }
        finished_ = true;
        std::vector<grass_tile_info> infos;
        auto backing = open_grass_cache(directory_, shape_, infos);
        if (backing) { prune(directory_, path_); }
        return backing;
    }

private:
    fs::path directory_;
    fs::path path_;
    fs::path temporary_;
    grass_cache_shape shape_;
    std::ofstream out_;
    std::vector<entry> entries_;
    std::uint64_t written_ = 0;
    bool ok_ = false;
    bool finished_ = false;
};

grass_cache_writer::grass_cache_writer(const fs::path& directory, const grass_cache_shape& shape)
    : state_(std::make_unique<state>(directory, shape)) {}
grass_cache_writer::~grass_cache_writer() = default;
bool grass_cache_writer::ok() const { return state_->ok(); }
void grass_cache_writer::put(const std::size_t slot, const std::vector<std::uint8_t>& bytes, const grass_tile_info& info) {
    state_->put(slot, bytes, info);
}
std::shared_ptr<const grass_tile_backing> grass_cache_writer::finish() { return state_->finish(); }
