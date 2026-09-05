#pragma once

#include "codecs.hpp"

#include <etl/type_traits.h>
#include <string.h>

namespace jetlog {

namespace detail {

template <typename T>
struct always_false : etl::false_type {};

// For tag and format, const char[N] is treated as a literal.
// Local const arrays must still outlive their records.
template <typename T>
struct char_array : etl::false_type {
    static constexpr size_t size = 0;
    static constexpr bool is_literal = false;
};

template <size_t N>
struct char_array<char[N]> : etl::true_type {
    static constexpr size_t size = N - 1;
    static constexpr bool is_literal = false;
};

template <size_t N>
struct char_array<const char[N]> : etl::true_type {
    static constexpr size_t size = N - 1;
    static constexpr bool is_literal = true;
};

template <typename T>
inline constexpr bool is_c_string_v =
    etl::is_same_v<T, const char*> || etl::is_same_v<T, char*>;

// Anything with data(), size() and a value_type of char: std::string,
// etl::string, views, classes of your own. A container of uint8_t is a buffer,
// not a string, and must not land here.
template <typename T, typename = void>
struct is_string_like : etl::false_type {};

template <typename T>
struct is_string_like<T, etl::void_t<
        typename T::value_type,
        decltype(etl::declval<const T&>().data()),
        decltype(etl::declval<const T&>().size())>>
    : etl::bool_constant<etl::is_same_v<typename T::value_type, char>> {};

template <typename T>
inline constexpr bool is_string_like_v = is_string_like<T>::value;

template <typename T>
using bare = typename etl::remove_cv<typename etl::remove_reference<T>::type>::type;

// Tag and format are stored by reference and must outlive their records.
template <typename T>
auto as_static(T&& text) -> static_str {
    using U = typename etl::remove_reference<T>::type;

    if constexpr (char_array<U>::is_literal) {
        return static_str(&text[0], char_array<U>::size);
    } else if constexpr (etl::is_same_v<bare<T>, static_str>) {
        return text;
    } else {
        static_assert(always_false<T>::value,
            "jetlog: tag and format must be a literal or jetlog::static_str");
        return {};
    }
}

// Message strings are copied unless explicitly marked static_str.
template <typename T>
decltype(auto) as_arg(const T& value) {
    if constexpr (char_array<T>::value || is_c_string_v<bare<T>>) {
        const char* text = value;
        return copy_str(etl::string_view(text, text != nullptr ? strlen(text) : 0));
    } else if constexpr (is_string_like_v<bare<T>> && !etl::is_same_v<bare<T>, static_str>) {
        return copy_str(etl::string_view(value.data(), value.size()));
    } else {
        return (value);
    }
}

} // namespace detail

} // namespace jetlog
