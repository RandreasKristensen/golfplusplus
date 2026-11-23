#include "renderer/grass_tile_store.h"

#include <algorithm>
#include <array>
#include <glm/common.hpp>

#include "renderer/gl_loader.h"

namespace {
// Rows of 65 texels are 65, 130 and 260 bytes: the default four-byte row
// alignment would shear two of the three, so the unpack state is set, not
// assumed, and put back to GL's defaults (nothing else here sets it) without
// reading it, which would make the app wait for the driver.
class unpack_state {
public:
    unpack_state() { set(1); }
    ~unpack_state() { set(4); }
    unpack_state(const unpack_state&) = delete;
    unpack_state& operator=(const unpack_state&) = delete;

private:
    static void set(const GLint alignment) {
        glPixelStorei(GL_UNPACK_ALIGNMENT, alignment);
        for (const GLenum pname : {GL_UNPACK_ROW_LENGTH, GL_UNPACK_SKIP_PIXELS, GL_UNPACK_SKIP_ROWS, GL_UNPACK_SKIP_IMAGES,
                                   GL_UNPACK_IMAGE_HEIGHT}) {
            glPixelStorei(pname, 0);
        }
    }
};

void parameters(const unsigned target, const unsigned filter) {
    glTexParameteri(target, GL_TEXTURE_MIN_FILTER, static_cast<GLint>(filter));
    glTexParameteri(target, GL_TEXTURE_MAG_FILTER, static_cast<GLint>(filter));
    glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void bind_unit(const int unit, const unsigned target, const unsigned texture) {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(target, texture);
}
}

bool grass_tile_store::init(const int first_unit, const int layer_limit) {
    shutdown();
    first_unit_ = first_unit;
    GLint layers = 0;
    glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &layers);
    GLint size = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &size);
    max_texture_size_ = size;
    const int capacity = std::min(grass_tile_layers(layers), layer_limit);
    if (capacity < 1 || size < grass_tile_nodes) { return false; }
    unsigned int ids[4] = {0, 0, 0, 0};
    glGenTextures(4, ids);
    height_ = ids[0];
    clearance_ = ids[1];
    kind_ = ids[2];
    table_ = ids[3];
    if (!height_ || !clearance_ || !kind_ || !table_) { shutdown(); return false; }
    const unpack_state unpack;
    bind_unit(first_unit_, GL_TEXTURE_2D_ARRAY, height_);
    storage_.texture_array(height_, GL_R32F, grass_tile_nodes, grass_tile_nodes, capacity, GL_RED, GL_FLOAT);
    parameters(GL_TEXTURE_2D_ARRAY, GL_LINEAR);
    bind_unit(first_unit_, GL_TEXTURE_2D_ARRAY, clearance_);
    storage_.texture_array(clearance_, GL_R16, grass_tile_nodes, grass_tile_nodes, capacity, GL_RED, GL_UNSIGNED_SHORT);
    parameters(GL_TEXTURE_2D_ARRAY, GL_NEAREST);
    bind_unit(first_unit_, GL_TEXTURE_2D_ARRAY, kind_);
    storage_.texture_array(kind_, GL_R8_SNORM, grass_tile_nodes, grass_tile_nodes, capacity, GL_RED, GL_BYTE);
    parameters(GL_TEXTURE_2D_ARRAY, GL_LINEAR);
    bind_unit(first_unit_, GL_TEXTURE_2D, table_);
    const std::uint16_t none = 0;
    storage_.texture_image(table_, GL_R16UI, 1, 1, GL_RED_INTEGER, GL_UNSIGNED_SHORT, &none);
    parameters(GL_TEXTURE_2D, GL_NEAREST);
    glActiveTexture(GL_TEXTURE0);
    cache_.reset(capacity);
    layer_tile_.assign(static_cast<std::size_t>(capacity), glm::ivec2(0));
    have_field_ = false;
    usable_ = false;
    return true;
}

void grass_tile_store::shutdown() {
    storage_.delete_texture(height_);
    storage_.delete_texture(clearance_);
    storage_.delete_texture(kind_);
    storage_.delete_texture(table_);
    cache_.reset(0);
    layer_tile_.clear();
    have_field_ = false;
    usable_ = false;
}

std::uint64_t grass_tile_store::logical_bytes() const {
    if (!storage_.ledger) { return 0; }
    const auto& textures = storage_.ledger->textures;
    return textures.bytes(height_) + textures.bytes(clearance_) + textures.bytes(kind_) + textures.bytes(table_);
}

bool grass_tile_store::use(const grass_field& field) {
    const identity now{field.revision, field.origin, field.columns, field.rows};
    if (have_field_ && field_ == now) { return usable_; }
    field_ = now;
    have_field_ = true;
    ++fields_used_;
    cache_.reset(cache_.capacity());
    std::fill(layer_tile_.begin(), layer_tile_.end(), glm::ivec2(0));
    refused_.clear();
    first_tile_ = field.first_tile;
    table_size_ = glm::max(field.tile_counts, glm::ivec2(1));
    usable_ = !field.tiles.empty() && table_size_.x <= max_texture_size_ && table_size_.y <= max_texture_size_;
    table_data_.assign(static_cast<std::size_t>(table_size_.x) * table_size_.y, 0);
    table_stale_ = true;
    if (usable_) {
        const unpack_state unpack;
        bind_unit(first_unit_, GL_TEXTURE_2D, table_);
        storage_.texture_image(table_, GL_R16UI, table_size_.x, table_size_.y, GL_RED_INTEGER, GL_UNSIGNED_SHORT,
                               table_data_.data());
        glActiveTexture(GL_TEXTURE0);
    }
    return usable_;
}

std::uint64_t grass_tile_store::key_of(const glm::ivec2& tile) {
    return static_cast<std::uint64_t>(static_cast<std::uint32_t>(tile.x)) << 32 | static_cast<std::uint32_t>(tile.y);
}

std::uint64_t grass_tile_store::decoded_bytes() const {
    return scratch_.height.capacity() * sizeof(float) + scratch_.clearance.capacity() * sizeof(std::uint16_t) + scratch_.kind.capacity() +
           static_cast<std::uint64_t>(grass_tile_nodes) * grass_tile_nodes * (sizeof(float) + sizeof(std::uint16_t) + 1);
}

bool grass_tile_store::upload_tile(const grass_field& field, const glm::ivec2& tile, const int layer) {
    if (!fill_grass_tile(field, tile, scratch_)) { return false; }
    const int n = grass_tile_nodes;
    const std::size_t texels = static_cast<std::size_t>(n) * n;
    bind_unit(first_unit_, GL_TEXTURE_2D_ARRAY, height_);
    storage_.texture_layer(GL_RED, GL_FLOAT, n, n, layer, scratch_.height.data(), texels * sizeof(float));
    bind_unit(first_unit_ + 1, GL_TEXTURE_2D_ARRAY, clearance_);
    storage_.texture_layer(GL_RED, GL_UNSIGNED_SHORT, n, n, layer, scratch_.clearance.data(), texels * sizeof(std::uint16_t));
    bind_unit(first_unit_ + 2, GL_TEXTURE_2D_ARRAY, kind_);
    storage_.texture_layer(GL_RED, GL_BYTE, n, n, layer, scratch_.kind.data(), texels * sizeof(std::int8_t));
    layer_tile_[static_cast<std::size_t>(layer)] = tile;
    ++tiles_uploaded_;
    return true;
}

// Uploads what the cache decided; a tile that cannot be read gives its layer back.
void grass_tile_store::apply(const grass_field& field, const grass_tile_cache::change& change) {
    for (const grass_tile_cache::upload& upload : change.uploads) {
        if (upload_tile(field, upload.tile, upload.layer)) { continue; }
        cache_.forget(upload.tile);
        refused_[key_of(upload.tile)] = draws_ + grass_tile_retry_draws;
        ++tiles_unreadable_;
    }
    table_stale_ = table_stale_ || !change.uploads.empty() || !change.evicted.empty();
}

void grass_tile_store::write_table(const std::vector<glm::ivec2>& only, const bool only_these) {
    std::fill(table_data_.begin(), table_data_.end(), 0);
    const auto set = [&](const glm::ivec2& tile, const int layer) {
        const glm::ivec2 at = tile - first_tile_;
        if (layer >= 0 && at.x >= 0 && at.y >= 0 && at.x < table_size_.x && at.y < table_size_.y) {
            table_data_[static_cast<std::size_t>(at.y) * table_size_.x + at.x] = static_cast<std::uint16_t>(layer + 1);
        }
    };
    if (only_these) {
        for (const glm::ivec2& tile : only) { set(tile, cache_.layer_of(tile)); }
    } else {
        for (int layer = 0; layer < cache_.capacity(); ++layer) {
            const glm::ivec2 tile = layer_tile_[static_cast<std::size_t>(layer)];
            if (cache_.layer_of(tile) == layer) { set(tile, layer); }
        }
    }
    bind_unit(first_unit_ + 3, GL_TEXTURE_2D, table_);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, table_size_.x, table_size_.y, GL_RED_INTEGER, GL_UNSIGNED_SHORT, table_data_.data());
}

void grass_tile_store::make_resident(const grass_field& field, const std::vector<glm::ivec2>& tiles, const bool only_these) {
    if (!usable_) { return; }
    const unpack_state unpack;
    ++draws_;
    std::vector<glm::ivec2> asked;
    asked.reserve(tiles.size());
    for (const glm::ivec2& tile : tiles) {
        const auto refused = refused_.find(key_of(tile));
        if (refused == refused_.end()) { asked.push_back(tile); }
        else if (refused->second <= draws_) { refused_.erase(refused); asked.push_back(tile); }
    }
    apply(field, cache_.make_resident(asked));
    table_stale_ = table_stale_ || only_these || table_only_;
    table_only_ = only_these;
    if (table_stale_) {
        write_table(tiles, only_these);
        table_stale_ = false;
    } else {
        bind_unit(first_unit_ + 3, GL_TEXTURE_2D, table_);
    }
    bind_unit(first_unit_, GL_TEXTURE_2D_ARRAY, height_);
    bind_unit(first_unit_ + 1, GL_TEXTURE_2D_ARRAY, clearance_);
    bind_unit(first_unit_ + 2, GL_TEXTURE_2D_ARRAY, kind_);
    glActiveTexture(GL_TEXTURE0);
}

void grass_tile_store::fill_ahead(const grass_field& field, const std::vector<glm::ivec2>& ahead, const int budget) {
    if (!usable_ || budget <= 0) { return; }
    const unpack_state unpack;
    std::vector<glm::ivec2> asked;
    asked.reserve(ahead.size());
    for (const glm::ivec2& tile : ahead) {
        if (refused_.find(key_of(tile)) == refused_.end()) { asked.push_back(tile); }
    }
    const grass_tile_cache::change change = cache_.fill_ahead(asked, budget);
    tiles_ahead_ += change.uploads.size();
    apply(field, change);
    glActiveTexture(GL_TEXTURE0);
}
