#include "game/json_edit.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
constexpr int max_decimals = 9;
constexpr std::size_t max_index_digits = 9;

bool is_space(const char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

// A character of a number, true, false or null.
bool is_scalar(const char c) {
    return c != '\0' && std::strchr("+-0123456789.eEtruefalsn", c) != nullptr;
}

// Walks JSON text without building it, to find where a value is written.
class json_scanner {
public:
    explicit json_scanner(const std::string& text) : text_(text) {}

    // The span of the value at `path` from here, or nullopt.
    std::optional<text_span> find(const json_path& path, std::size_t depth) {
        skip_space();
        if (depth == path.size()) {
            const std::size_t begin = at_;
            if (!skip_value()) {
                return std::nullopt;
            }
            return text_span{begin, at_};
        }
        if (peek() == '{') {
            return find_member(path, depth);
        }
        if (peek() == '[') {
            return find_element(path, depth);
        }
        return std::nullopt;
    }

    bool at_end() {
        skip_space();
        return at_ == text_.size();
    }

    bool skip_value() {
        skip_space();
        const char c = peek();
        if (c == '{' || c == '[') {
            return skip_container(c == '{' ? '}' : ']', c == '{');
        }
        if (c == '"') {
            return skip_string();
        }
        const std::size_t begin = at_;
        while (at_ < text_.size() && is_scalar(text_[at_])) {
            ++at_;
        }
        return at_ > begin;
    }

private:
    char peek() const { return at_ < text_.size() ? text_[at_] : '\0'; }

    void skip_space() {
        while (at_ < text_.size() && is_space(text_[at_])) {
            ++at_;
        }
    }

    bool skip_string() {
        ++at_;  // the opening quote
        while (at_ < text_.size()) {
            const char c = text_[at_++];
            if (c == '\\') {
                ++at_;
            } else if (c == '"') {
                return true;
            }
        }
        return false;
    }

    // A member's key, as written between its quotes.
    std::optional<std::string> read_key() {
        skip_space();
        if (peek() != '"') {
            return std::nullopt;
        }
        const std::size_t begin = at_ + 1;
        if (!skip_string()) {
            return std::nullopt;
        }
        return text_.substr(begin, at_ - 1 - begin);
    }

    bool expect(const char c) {
        skip_space();
        if (peek() != c) {
            return false;
        }
        ++at_;
        return true;
    }

    // After a member or element: true at the next one, false at `close` or a fault.
    bool next_item(const char close, bool& closed) {
        skip_space();
        if (peek() == ',') {
            ++at_;
            return true;
        }
        closed = peek() == close;
        if (closed) {
            ++at_;
        }
        return false;
    }

    bool skip_container(const char close, const bool object) {
        ++at_;
        skip_space();
        if (peek() == close) {
            ++at_;
            return true;
        }
        bool closed = false;
        do {
            if (object && (!read_key() || !expect(':'))) {
                return false;
            }
            if (!skip_value()) {
                return false;
            }
        } while (next_item(close, closed));
        return closed;
    }

    std::optional<text_span> find_member(const json_path& path, const std::size_t depth) {
        ++at_;
        skip_space();
        if (peek() == '}') {
            return std::nullopt;
        }
        bool closed = false;
        do {
            const std::optional<std::string> key = read_key();
            if (!key || !expect(':')) {
                return std::nullopt;
            }
            if (*key == path[depth]) {
                return find(path, depth + 1);
            }
            if (!skip_value()) {
                return std::nullopt;
            }
        } while (next_item('}', closed));
        return std::nullopt;
    }

    std::optional<text_span> find_element(const json_path& path, const std::size_t depth) {
        const std::string& segment = path[depth];
        const auto digit = [](const char c) { return c >= '0' && c <= '9'; };
        if (segment.empty() || segment.size() > max_index_digits || !std::all_of(segment.begin(), segment.end(), digit)) {
            return std::nullopt;
        }
        const std::size_t wanted = static_cast<std::size_t>(std::stoul(segment));
        ++at_;
        skip_space();
        if (peek() == ']') {
            return std::nullopt;
        }
        bool closed = false;
        std::size_t index = 0;
        do {
            if (index == wanted) {
                return find(path, depth + 1);
            }
            if (!skip_value()) {
                return std::nullopt;
            }
            ++index;
        } while (next_item(']', closed));
        return std::nullopt;
    }

    const std::string& text_;
    std::size_t at_ = 0;
};
}

std::optional<json_path> parse_json_path(const std::string& text) {
    json_path path;
    std::size_t begin = 0;
    while (begin <= text.size()) {
        const std::size_t slash = std::min(text.find('/', begin), text.size());
        if (slash == begin) {
            return std::nullopt;
        }
        path.push_back(text.substr(begin, slash - begin));
        begin = slash + 1;
    }
    return path;
}

std::optional<text_span> json_value_span(const std::string& text, const json_path& path) {
    // The whole text must be one value, so a span is never inside a fragment.
    json_scanner whole(text);
    if (!whole.skip_value() || !whole.at_end()) {
        return std::nullopt;
    }
    return json_scanner(text).find(path, 0);
}

std::optional<std::string> replace_json_value(const std::string& text, const json_path& path, const std::string& value) {
    const std::optional<text_span> span = json_value_span(text, path);
    if (!span) {
        return std::nullopt;
    }
    return text.substr(0, span->begin) + value + text.substr(span->end);
}

std::string json_number_text(const double value, const int decimals, const bool keep_point) {
    const int places = std::clamp(decimals, 0, max_decimals);
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.*f", places, value);
    std::string text = buffer;
    if (text.find('.') != std::string::npos) {
        text.erase(text.find_last_not_of('0') + 1);
        if (text.back() == '.') {
            text.pop_back();
        }
    }
    if (text == "-0") {
        text = "0";
    }
    if (keep_point && text.find('.') == std::string::npos) {
        text += ".0";
    }
    return text;
}

int json_number_decimals(const std::string& number) {
    const std::size_t point = number.find('.');
    if (point == std::string::npos) {
        return 0;
    }
    const std::size_t end = number.find_first_of("eE", point);
    return static_cast<int>((end == std::string::npos ? number.size() : end) - point - 1);
}
