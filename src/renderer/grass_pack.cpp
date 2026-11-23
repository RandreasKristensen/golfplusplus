#include "renderer/grass_pack.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace {
constexpr std::size_t node_count = static_cast<std::size_t>(grass_tile_nodes) * grass_tile_nodes;
constexpr std::uint8_t float_heights = 1;
constexpr std::uint8_t one_clearance = 2;
constexpr std::uint8_t one_kind = 4;
constexpr std::uint16_t no_ground_offset = 0xffff;

struct header {
    std::uint8_t flags = 0;
    std::int8_t kind = 0;
    std::uint16_t clearance = 0;
    std::int32_t base = 0;
};
static_assert(sizeof(header) == 8, "the stored header is packed");

float decode_height(const std::int32_t whole) { return static_cast<float>(static_cast<double>(whole) * static_cast<double>(grass_height_step)); }

template <typename value>
void append(std::vector<std::uint8_t>& out, const value* items, const std::size_t count) {
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(items);
    out.insert(out.end(), bytes, bytes + count * sizeof(value));
}

template <typename value>
const std::uint8_t* take(const std::uint8_t* at, std::vector<value>& out, const std::size_t count) {
    out.resize(count);
    std::memcpy(out.data(), at, count * sizeof(value));
    return at + count * sizeof(value);
}

bool constant(const std::vector<std::uint16_t>& values) { return std::all_of(values.begin(), values.end(), [&](const std::uint16_t v) { return v == values[0]; }); }
bool constant(const std::vector<std::int8_t>& values) { return std::all_of(values.begin(), values.end(), [&](const std::int8_t v) { return v == values[0]; }); }
}

std::vector<std::uint8_t> pack_grass_tile(const grass_exact_tile& nodes, grass_tile_info& info) {
    info = grass_tile_info{};
    std::vector<std::uint16_t> clearance(node_count);
    std::vector<std::int8_t> kind(node_count);
    std::uint16_t most = 0;
    for (std::size_t i = 0; i < node_count; ++i) {
        clearance[i] = encode_grass_clearance(nodes.clearance[i]);
        kind[i] = encode_grass_kind(nodes.kind[i]);
        most = std::max(most, clearance[i]);
    }
    // No style's blade needs less than the field's error beyond its reach.
    if (decode_grass_clearance(most) < grass_field_error) { return {}; }

    std::int64_t lowest = std::numeric_limits<std::int64_t>::max();
    std::int64_t highest = std::numeric_limits<std::int64_t>::min();
    std::vector<std::int64_t> whole(node_count, 0);
    bool grounded = false;
    for (std::size_t i = 0; i < node_count; ++i) {
        if (nodes.kind[i] == 0.0f) { continue; }
        whole[i] = std::llround(static_cast<double>(nodes.height[i]) / static_cast<double>(grass_height_step));
        lowest = std::min(lowest, whole[i]);
        highest = std::max(highest, whole[i]);
        grounded = true;
    }
    header head;
    const bool fits = grounded && highest - lowest < no_ground_offset &&
                      std::max(std::abs(lowest), std::abs(highest)) < (std::int64_t{1} << 30);
    if (!fits) { head.flags |= float_heights; }
    if (constant(clearance)) { head.flags |= one_clearance; head.clearance = clearance[0]; }
    if (constant(kind)) { head.flags |= one_kind; head.kind = kind[0]; }
    head.base = fits ? static_cast<std::int32_t>(lowest) : 0;

    std::vector<std::uint8_t> out;
    append(out, &head, 1);
    if (fits) {
        std::vector<std::uint16_t> offsets(node_count, no_ground_offset);
        for (std::size_t i = 0; i < node_count; ++i) {
            if (nodes.kind[i] != 0.0f) { offsets[i] = static_cast<std::uint16_t>(whole[i] - lowest); }
        }
        append(out, offsets.data(), node_count);
    } else {
        append(out, nodes.height.data(), node_count);
    }
    if (!(head.flags & one_clearance)) { append(out, clearance.data(), node_count); }
    if (!(head.flags & one_kind)) { append(out, kind.data(), node_count); }

    info.max_clearance = decode_grass_clearance(most);
    info.low = std::numeric_limits<float>::max();
    info.high = std::numeric_limits<float>::lowest();
    for (std::size_t i = 0; i < node_count; ++i) {
        if (nodes.kind[i] == 0.0f) { continue; }
        const float height = fits ? decode_height(static_cast<std::int32_t>(whole[i])) : nodes.height[i];
        info.low = std::min(info.low, height);
        info.high = std::max(info.high, height);
    }
    if (info.low > info.high) { info.low = info.high = 0.0f; }
    return out;
}

bool unpack_grass_tile(const std::uint8_t* bytes, const std::size_t size, grass_tile_texels& out) {
    if (size < sizeof(header)) { return false; }
    header head;
    std::memcpy(&head, bytes, sizeof(header));
    const bool floats = (head.flags & float_heights) != 0;
    const std::size_t expected = sizeof(header) + node_count * (floats ? sizeof(float) : sizeof(std::uint16_t)) +
                                 ((head.flags & one_clearance) ? 0 : node_count * sizeof(std::uint16_t)) +
                                 ((head.flags & one_kind) ? 0 : node_count);
    if (size != expected || (head.flags & ~(float_heights | one_clearance | one_kind)) != 0) { return false; }
    const std::uint8_t* at = bytes + sizeof(header);
    if (floats) {
        at = take(at, out.height, node_count);
    } else {
        out.height.resize(node_count);
        for (std::size_t i = 0; i < node_count; ++i, at += sizeof(std::uint16_t)) {
            std::uint16_t offset = 0;
            std::memcpy(&offset, at, sizeof(offset));
            out.height[i] = offset == no_ground_offset ? grass_no_ground_height : decode_height(head.base + offset);
        }
    }
    if (head.flags & one_clearance) { out.clearance.assign(node_count, head.clearance); }
    else { at = take(at, out.clearance, node_count); }
    if (head.flags & one_kind) { out.kind.assign(node_count, head.kind); }
    else { take(at, out.kind, node_count); }
    return true;
}
