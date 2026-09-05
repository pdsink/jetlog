#pragma once

/*

// Simple placeholder without format
{}

// Hex format (lowercase/uppercase)
{:x}  // ff
{:X}  // FF

// Hex with base prefix
{:#x} // 0xff
{:#X} // 0XFF

// Zero-padded hex
{:04x} // 00ff

// Binary format
{:b}   // 101010
{:#b}  // 0b101010

// Decimal
{:d}   // 42

*/

#include <etl/format_spec.h>
#include <etl/string_view.h>

namespace jetlog {

class FormatParser {
public:
    static auto get_placeholder_length(etl::string_view str, size_t pos) -> size_t {
        const size_t start{pos};
        const auto max = str.length();

        if (pos >= max || str[pos] != '{') { return 0; }
        if (pos + 1 >= max) { return 0; }

        // Empty placeholder {}.
        if (str[pos + 1] == '}') { return 2; }

        // A format specifier starts with ':'.
        if (str[pos + 1] != ':') { return 0; }
        if (pos + 2 >= max) { return 0; }
        pos += 2;

        // Optional base prefix.
        if (pos < max && str[pos] == '#') {
            pos++;
            if (pos >= max) { return 0; }
        }

        // A leading zero selects padding and must be followed by a width.
        if (pos < max && str[pos] == '0') {
            pos++;
            if (pos >= max || !is_digit(str[pos])) { return 0; }
            while (pos < max && is_digit(str[pos])) { pos++; }
            if (pos >= max) { return 0; }
        }
        // Check just width
        else if (pos < max && is_digit(str[pos])) {
            while (pos < max && is_digit(str[pos])) { pos++; }
            if (pos >= max) { return 0; }
        }

        // Check type and final }
        if (pos + 1 >= max) { return 0; }

        switch(str[pos]) {
            case 'x': case 'X': case 'b': case 'd': break;
            default: return 0;
        }
        pos++;

        if (str[pos] != '}') { return 0; }
        return pos + 1 - start;
    }

    static auto parse_format(etl::string_view str, size_t pos, etl::format_spec& spec) -> void {
        const auto max = str.length();
        if (pos >= max || str[pos] != '{') {
            reset_spec(spec);
            return;
        }
        if (pos + 1 >= max) {
            reset_spec(spec);
            return;
        }

        // {} keeps the caller's format defaults.
        if (str[pos + 1] == '}') { return; }

        // A format specifier starts with ':'.
        if (str[pos + 1] != ':') {
            reset_spec(spec);
            return;
        }
        if (pos + 2 >= max) {
            reset_spec(spec);
            return;
        }
        pos += 2;

        // Optional base prefix.
        if (pos < max && str[pos] == '#') {
            spec.show_base(true);
            pos++;
            if (pos >= max) {
                reset_spec(spec);
                return;
            }
        }

        // A leading zero selects padding and must be followed by a width.
        if (pos < max && str[pos] == '0') {
            spec.fill('0');
            pos++;
            if (pos >= max || !is_digit(str[pos])) {
                reset_spec(spec);
                return;
            }
            int width = 0;
            while (pos < max && is_digit(str[pos])) {
                width = width * 10 + (str[pos] - '0');
                pos++;
            }
            spec.width(width);
            if (pos >= max) {
                reset_spec(spec);
                return;
            }
        }
        // Check just width
        else if (pos < max && is_digit(str[pos])) {
            int width = 0;
            while (pos < max && is_digit(str[pos])) {
                width = width * 10 + (str[pos] - '0');
                pos++;
            }
            spec.width(width);
            if (pos >= max) {
                reset_spec(spec);
                return;
            }
        }

        // Must be a type specifier
        switch(str[pos]) {
            case 'x': spec.base(16); break;
            case 'X': spec.base(16).upper_case(true); break;
            case 'b': spec.base(2); break;
            case 'd': spec.base(10); break;
            default: reset_spec(spec); return;
        }
        pos++;

        // The placeholder must end with '}'.
        if (str[pos] != '}') {
            reset_spec(spec);
            return;
        }
    }

private:
    static auto reset_spec(etl::format_spec& spec) -> void {
        spec.fill(' ');
        spec.width(0);
        spec.base(10);
        spec.show_base(false);
        spec.upper_case(false);
    }

    static constexpr auto is_digit(int ch) noexcept -> bool {
        return ch >= '0' && ch <= '9';
    }
};

} // namespace jetlog
