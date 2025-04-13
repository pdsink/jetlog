#pragma once

#include "format_parser.hpp"
#include <etl/iterator.h>

namespace jetlog {

class StringTokenizer {
public:
    struct Token {
        etl::string_view text;
        bool is_placeholder;

        Token(etl::string_view t, bool ph) : text(t), is_placeholder(ph) {}
    };

    class iterator {
    public:
        using iterator_category = etl::forward_iterator_tag;
        using value_type = Token;
        using difference_type = int32_t;
        using pointer = Token*;
        using reference = Token&;

        explicit iterator(etl::string_view str, size_t pos = 0) : source(str), current_pos(pos) {
            if (current_pos < source.length()) {
                find_next_token();
            }
        }

        auto operator++() -> iterator&  {
            if (current_pos < source.length()) {
                current_pos += current_token.text.length();
                find_next_token();
            }
            return *this;
        }

        auto operator++(int) -> iterator {
            iterator tmp = *this;
            ++(*this);
            return tmp;
        }

        auto operator==(const iterator& other) const -> bool {
            return current_pos == other.current_pos;
        }

        auto operator!=(const iterator& other) const -> bool {
            return !(*this == other);
        }

        auto operator*() const -> const Token&{
            return current_token;
        }

    private:
        etl::string_view source;
        size_t current_pos;
        Token current_token{"", false};

        void find_next_token() {
            if (current_pos >= source.length()) {
                return;
            }

            if (source[current_pos] == '{') {
                size_t len{FormatParser::get_placeholder_length(source, current_pos)};
                if (len > 0) {
                    current_token = Token{source.substr(current_pos, len), true};
                } else {
                    current_token = Token{source.substr(current_pos, 1), false};
                }
            } else {
                size_t next_pos{source.find('{', current_pos)};
                if (next_pos == etl::string_view::npos) {
                    current_token = Token{source.substr(current_pos), false};
                } else {
                    current_token = Token{source.substr(current_pos, next_pos - current_pos), false};
                }
            }
        }
    };

    explicit StringTokenizer(etl::string_view input) : source(input) {}

    auto begin() -> iterator { return iterator(source); }
    auto end() -> iterator { return iterator(source, source.length()); }

private:
    etl::string_view source;
};

} // namespace jetlog
