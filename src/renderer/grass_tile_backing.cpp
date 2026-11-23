#include "renderer/grass_tile_backing.h"

void grass_memory_backing::put(const std::size_t slot, const std::vector<std::uint8_t>& bytes) {
    if (slot >= span_.size() || bytes.empty()) { return; }
    span_[slot] = {bytes_.size(), static_cast<std::uint32_t>(bytes.size())};
    bytes_.insert(bytes_.end(), bytes.begin(), bytes.end());
}

bool grass_memory_backing::read(const std::size_t slot, std::vector<std::uint8_t>& out) const {
    if (slot >= span_.size() || span_[slot].size == 0) { return false; }
    const auto first = bytes_.begin() + static_cast<std::ptrdiff_t>(span_[slot].first);
    out.assign(first, first + span_[slot].size);
    return true;
}
