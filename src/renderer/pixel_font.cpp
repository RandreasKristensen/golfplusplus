#include "renderer/pixel_font.h"

#include <algorithm>
#include <cctype>
#include <cstring>

// Glyph table moved verbatim from renderer.cpp. Rows are 7 tall; '1' is a lit
// pixel. Unknown characters fall back to 'I'.
const std::array<const char*, 7>& glyph_rows(const char value) {
    static const std::array<const char*, 7> glyph_a = {
        "0110",
        "1001",
        "1001",
        "1111",
        "1001",
        "1001",
        "1001"
    };
    static const std::array<const char*, 7> glyph_b = {
        "1110",
        "1001",
        "1001",
        "1110",
        "1001",
        "1001",
        "1110"
    };
    static const std::array<const char*, 7> glyph_g = {
        "0111",
        "1000",
        "1000",
        "1011",
        "1001",
        "1001",
        "0111"
    };
    static const std::array<const char*, 7> glyph_h = {
        "1001",
        "1001",
        "1001",
        "1111",
        "1001",
        "1001",
        "1001"
    };
    static const std::array<const char*, 7> glyph_k = {
        "1001",
        "1010",
        "1100",
        "1100",
        "1010",
        "1001",
        "1001"
    };
    static const std::array<const char*, 7> glyph_l = {
        "1000",
        "1000",
        "1000",
        "1000",
        "1000",
        "1000",
        "1111"
    };
    static const std::array<const char*, 7> glyph_n = {
        "1001",
        "1101",
        "1101",
        "1011",
        "1011",
        "1001",
        "1001"
    };
    static const std::array<const char*, 7> glyph_p = {
        "1110",
        "1001",
        "1001",
        "1110",
        "1000",
        "1000",
        "1000"
    };
    static const std::array<const char*, 7> glyph_q = {
        "0110",
        "1001",
        "1001",
        "1001",
        "1011",
        "1001",
        "0111"
    };
    static const std::array<const char*, 7> glyph_o = {
        "0110",
        "1001",
        "1001",
        "1001",
        "1001",
        "1001",
        "0110"
    };
    static const std::array<const char*, 7> glyph_f = {
        "1111",
        "1000",
        "1000",
        "1110",
        "1000",
        "1000",
        "1000"
    };
    static const std::array<const char*, 7> glyph_w = {
        "10001",
        "10001",
        "10001",
        "10101",
        "10101",
        "10101",
        "01010"
    };
    static const std::array<const char*, 7> glyph_7 = {
        "1111",
        "0001",
        "0010",
        "0010",
        "0100",
        "0100",
        "0100"
    };
    static const std::array<const char*, 7> glyph_i = {
        "111",
        "010",
        "010",
        "010",
        "010",
        "010",
        "111"
    };
    static const std::array<const char*, 7> glyph_j = {
        "1111",
        "0001",
        "0001",
        "0001",
        "1001",
        "1001",
        "0110"
    };
    static const std::array<const char*, 7> glyph_e = {
        "1111",
        "1000",
        "1000",
        "1110",
        "1000",
        "1000",
        "1111"
    };
    static const std::array<const char*, 7> glyph_r = {
        "1110",
        "1001",
        "1001",
        "1110",
        "1010",
        "1001",
        "1001"
    };
    static const std::array<const char*, 7> glyph_t = {
        "11111",
        "00100",
        "00100",
        "00100",
        "00100",
        "00100",
        "00100"
    };
    static const std::array<const char*, 7> glyph_u = {
        "1001",
        "1001",
        "1001",
        "1001",
        "1001",
        "1001",
        "0110"
    };
    static const std::array<const char*, 7> glyph_v = {
        "1001",
        "1001",
        "1001",
        "1001",
        "1001",
        "0110",
        "0110"
    };
    static const std::array<const char*, 7> glyph_y = {
        "1001",
        "1001",
        "0110",
        "0010",
        "0010",
        "0010",
        "0010"
    };
    static const std::array<const char*, 7> glyph_x = {
        "1001",
        "1001",
        "0110",
        "0110",
        "0110",
        "1001",
        "1001"
    };
    static const std::array<const char*, 7> glyph_s = {
        "1110",
        "1000",
        "1000",
        "1110",
        "0001",
        "0001",
        "1110"
    };
    static const std::array<const char*, 7> glyph_plus = {
        "00100",
        "00100",
        "11111",
        "00100",
        "00100",
        "00000",
        "00000"
    };
    static const std::array<const char*, 7> glyph_dash = {
        "0000",
        "0000",
        "0000",
        "1111",
        "0000",
        "0000",
        "0000"
    };
    static const std::array<const char*, 7> glyph_slash = {
        "0001",
        "0001",
        "0010",
        "0010",
        "0100",
        "0100",
        "1000"
    };
    static const std::array<const char*, 7> glyph_colon = {
        "000",
        "010",
        "010",
        "000",
        "010",
        "010",
        "000"
    };
    static const std::array<const char*, 7> glyph_c = {
        "0111",
        "1000",
        "1000",
        "1000",
        "1000",
        "1000",
        "0111"
    };
    static const std::array<const char*, 7> glyph_d = {
        "1110",
        "1001",
        "1001",
        "1001",
        "1001",
        "1001",
        "1110"
    };
    static const std::array<const char*, 7> glyph_m = {
        "10001",
        "11011",
        "10101",
        "10101",
        "10001",
        "10001",
        "10001"
    };
    static const std::array<const char*, 7> glyph_0 = {
        "0110",
        "1001",
        "1001",
        "1001",
        "1001",
        "1001",
        "0110"
    };
    static const std::array<const char*, 7> glyph_1 = {
        "010",
        "110",
        "010",
        "010",
        "010",
        "010",
        "111"
    };
    static const std::array<const char*, 7> glyph_2 = {
        "1110",
        "0001",
        "0001",
        "0110",
        "1000",
        "1000",
        "1111"
    };
    static const std::array<const char*, 7> glyph_3 = {
        "1110",
        "0001",
        "0001",
        "0110",
        "0001",
        "0001",
        "1110"
    };
    static const std::array<const char*, 7> glyph_4 = {
        "1001",
        "1001",
        "1001",
        "1111",
        "0001",
        "0001",
        "0001"
    };
    static const std::array<const char*, 7> glyph_5 = {
        "1111",
        "1000",
        "1000",
        "1110",
        "0001",
        "0001",
        "1110"
    };
    static const std::array<const char*, 7> glyph_6 = {
        "0111",
        "1000",
        "1000",
        "1110",
        "1001",
        "1001",
        "0110"
    };
    static const std::array<const char*, 7> glyph_8 = {
        "0110",
        "1001",
        "1001",
        "0110",
        "1001",
        "1001",
        "0110"
    };
    static const std::array<const char*, 7> glyph_9 = {
        "0110",
        "1001",
        "1001",
        "0111",
        "0001",
        "0001",
        "1110"
    };

    switch (value) {
    case '0':
        return glyph_0;
    case '1':
        return glyph_1;
    case '2':
        return glyph_2;
    case '3':
        return glyph_3;
    case '4':
        return glyph_4;
    case '5':
        return glyph_5;
    case '6':
        return glyph_6;
    case '7':
        return glyph_7;
    case '8':
        return glyph_8;
    case '9':
        return glyph_9;
    case 'A':
        return glyph_a;
    case 'B':
        return glyph_b;
    case 'G':
        return glyph_g;
    case 'H':
        return glyph_h;
    case 'K':
        return glyph_k;
    case 'L':
        return glyph_l;
    case 'N':
        return glyph_n;
    case 'P':
        return glyph_p;
    case 'Q':
        return glyph_q;
    case 'O':
        return glyph_o;
    case 'F':
        return glyph_f;
    case 'W':
        return glyph_w;
    case 'E':
        return glyph_e;
    case 'R':
        return glyph_r;
    case 'T':
        return glyph_t;
    case 'U':
        return glyph_u;
    case 'V':
        return glyph_v;
    case 'Y':
        return glyph_y;
    case 'S':
        return glyph_s;
    case '+':
        return glyph_plus;
    case 'C':
        return glyph_c;
    case 'D':
        return glyph_d;
    case 'M':
    case 'm':
        return glyph_m;
    case 'I':
        return glyph_i;
    case 'J':
        return glyph_j;
    case 'X':
        return glyph_x;
    case '-':
        return glyph_dash;
    case '/':
        return glyph_slash;
    case ':':
        return glyph_colon;
    default:
        return glyph_i;
    }
}

int glyph_width(const char value) {
    if (value == ' ') {
        return 3;
    }
    const char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(value)));
    return static_cast<int>(std::strlen(glyph_rows(upper)[0]));
}

int glyph_lit_pixel_count(const char value) {
    if (value == ' ') {
        return 0;
    }

    const char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(value)));
    int count = 0;
    for (const char* row : glyph_rows(upper)) {
        for (int x = 0; row[x] != '\0'; ++x) {
            if (row[x] == '1') {
                ++count;
            }
        }
    }
    return count;
}

float pixel_text_width(const std::string& label, const float pixel_size) {
    float width = 0.0f;
    for (const char c : label) {
        width += static_cast<float>(glyph_width(c) + 1) * pixel_size;
    }
    return std::max(0.0f, width - pixel_size);
}

float fit_pixel_size(const std::string& label,
                     const glm::vec2& max_half_size,
                     const float max_pixel_size,
                     const float min_pixel_size,
                     const float padding) {
    if (label.empty()) {
        return min_pixel_size;
    }

    const float max_width = max_half_size.x * 2.0f * padding;
    const float max_height = max_half_size.y * 2.0f * padding;
    const float base_width = std::max(1.0f, pixel_text_width(label, 1.0f));
    const float width_scale = max_width / base_width;
    const float height_scale = max_height / 7.0f;
    const float target = std::min(width_scale, height_scale);
    return std::clamp(target, min_pixel_size, max_pixel_size);
}

void draw_pixel_glyph(overlay_batch& batch,
                      const char value,
                      const glm::vec2 top_left,
                      const float pixel_size,
                      const glm::vec3 color) {
    if (value == ' ') {
        return;
    }

    const char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(value)));
    const std::array<const char*, 7>& rows = glyph_rows(upper);
    for (int y = 0; y < static_cast<int>(rows.size()); ++y) {
        const char* row = rows[y];
        for (int x = 0; row[x] != '\0'; ++x) {
            if (row[x] != '1') {
                continue;
            }

            draw_overlay_quad(batch,
                              top_left + glm::vec2((static_cast<float>(x) + 0.5f) * pixel_size,
                                                   -(static_cast<float>(y) + 0.5f) * pixel_size),
                              glm::vec2(pixel_size * pixel_glyph_half_size_ratio),
                              color);
        }
    }
}

void draw_pixel_text_centered(overlay_batch& batch,
                              const std::string& label,
                              const glm::vec2 center,
                              const float pixel_size,
                              const glm::vec3 color) {
    const float width = pixel_text_width(label, pixel_size);
    glm::vec2 cursor(center.x - width * 0.5f, center.y + 3.5f * pixel_size);
    for (const char c : label) {
        draw_pixel_glyph(batch, c, cursor, pixel_size, color);
        cursor.x += static_cast<float>(glyph_width(c) + 1) * pixel_size;
    }
}

void draw_pixel_text_left(overlay_batch& batch,
                          const std::string& label,
                          const glm::vec2 top_left,
                          const float pixel_size,
                          const glm::vec3 color) {
    glm::vec2 cursor = top_left;
    for (const char c : label) {
        draw_pixel_glyph(batch, c, cursor, pixel_size, color);
        cursor.x += static_cast<float>(glyph_width(c) + 1) * pixel_size;
    }
}
