#include "renderer/bmp_image.h"

#include "game/content_files.h"

#include <cstddef>
#include <cstdlib>

namespace {
// BITMAPFILEHEADER (14 bytes) then at least a BITMAPINFOHEADER (40 bytes).
constexpr std::size_t file_header_size = 14;
constexpr std::size_t info_header_size = 40;
// Refuses pictures whose pixel count could not be a real asset.
constexpr int max_bmp_side = 8192;
constexpr std::uint32_t bmp_uncompressed = 0;   // BI_RGB
constexpr std::uint32_t bmp_bitfields = 3;      // BI_BITFIELDS, standard masks only
// Where BI_BITFIELDS keeps its masks (red, green, blue, then alpha in headers
// of at least alpha_mask_header_size bytes), and the only ones read.
constexpr std::size_t red_mask_offset = file_header_size + info_header_size;
constexpr std::size_t alpha_mask_offset = red_mask_offset + 12;
constexpr std::size_t alpha_mask_header_size = 56;
constexpr std::uint32_t standard_masks[] = {0x00FF0000U, 0x0000FF00U, 0x000000FFU};
constexpr std::uint32_t standard_alpha_mask = 0xFF000000U;
constexpr std::uint8_t opaque = 255;

std::uint32_t read_u32(const std::string& bytes, const std::size_t offset) {
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        value |= static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset + i])) << (8U * i);
    }
    return value;
}

std::uint16_t read_u16(const std::string& bytes, const std::size_t offset) {
    return static_cast<std::uint16_t>(static_cast<unsigned char>(bytes[offset])
        | (static_cast<unsigned char>(bytes[offset + 1]) << 8U));
}
}

std::optional<rgba_image> parse_bmp(const std::string& bytes) {
    if (bytes.size() < file_header_size + info_header_size || bytes[0] != 'B' || bytes[1] != 'M') {
        return std::nullopt;
    }
    const std::size_t data_offset = read_u32(bytes, 10);
    const std::int32_t width = static_cast<std::int32_t>(read_u32(bytes, 18));
    const std::int32_t signed_height = static_cast<std::int32_t>(read_u32(bytes, 22));
    const std::uint16_t bits = read_u16(bytes, 28);
    const std::uint32_t compression = read_u32(bytes, 30);
    // A negative height stores the rows from the top down.
    const bool top_down = signed_height < 0;
    const int height = std::abs(signed_height);
    const bool known_compression = compression == bmp_uncompressed || (compression == bmp_bitfields && bits == 32);
    if (width <= 0 || height <= 0 || width > max_bmp_side || height > max_bmp_side || (bits != 24 && bits != 32) ||
        !known_compression) {
        return std::nullopt;
    }
    bool has_alpha = false;
    if (compression == bmp_bitfields) {
        if (bytes.size() < alpha_mask_offset + 4) {
            return std::nullopt;
        }
        for (std::size_t i = 0; i < 3; ++i) {
            if (read_u32(bytes, red_mask_offset + i * 4) != standard_masks[i]) {
                return std::nullopt;
            }
        }
        const std::size_t header_size = read_u32(bytes, file_header_size);
        has_alpha = header_size >= alpha_mask_header_size && read_u32(bytes, alpha_mask_offset) == standard_alpha_mask;
    }

    const std::size_t pixel_bytes = bits / 8U;
    const std::size_t stride = (static_cast<std::size_t>(width) * pixel_bytes + 3U) & ~std::size_t{3};
    if (data_offset > bytes.size() || bytes.size() - data_offset < stride * static_cast<std::size_t>(height)) {
        return std::nullopt;
    }

    rgba_image image;
    image.width = width;
    image.height = height;
    image.pixels.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U);
    for (int row = 0; row < height; ++row) {
        const int stored_row = top_down ? height - 1 - row : row;
        const std::size_t source = data_offset + static_cast<std::size_t>(stored_row) * stride;
        for (int column = 0; column < width; ++column) {
            const std::size_t from = source + static_cast<std::size_t>(column) * pixel_bytes;
            const std::size_t to = (static_cast<std::size_t>(row) * static_cast<std::size_t>(width) + static_cast<std::size_t>(column)) * 4U;
            // Stored blue, green, red (, alpha).
            image.pixels[to] = static_cast<std::uint8_t>(bytes[from + 2]);
            image.pixels[to + 1] = static_cast<std::uint8_t>(bytes[from + 1]);
            image.pixels[to + 2] = static_cast<std::uint8_t>(bytes[from]);
            image.pixels[to + 3] = has_alpha ? static_cast<std::uint8_t>(bytes[from + 3]) : opaque;
        }
    }
    return image;
}

std::optional<rgba_image> load_bmp_file(const std::filesystem::path& path) {
    const std::optional<std::string> bytes = read_text_file(path);
    return bytes ? parse_bmp(*bytes) : std::nullopt;
}
