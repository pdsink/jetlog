#pragma once

#include "codecs.hpp"

#include <etl/type_traits.h>

namespace jetlog {

// Codecs to add to the core. The core is integers up to 32 bits and strings;
// anything else - Flt, Dbl, I64, U64 or your own - goes here.
template <typename... Ts>
struct Codecs {};


namespace detail {

template <typename T> struct is_codecs : etl::false_type {};
template <typename... Ts> struct is_codecs<Codecs<Ts...>> : etl::true_type {};

template <typename T> inline constexpr bool is_codecs_v = is_codecs<T>::value;

// Picks the codec list, or keeps the default.
template <typename... Opts>
struct pick_codecs { using type = Codecs<>; };

template <typename... Ts, typename... Rest>
struct pick_codecs<Codecs<Ts...>, Rest...> { using type = Codecs<Ts...>; };

template <typename Other, typename... Rest>
struct pick_codecs<Other, Rest...> : pick_codecs<Rest...> {};

// Core codecs are always there and are not named in the list.
template <typename List> struct make_codec_list;

template <typename... Ts>
struct make_codec_list<Codecs<Ts...>> {
    using type = CodecList<I8, U8, I16, U16, I32, U32, StrRef, StrCopy, Ts...>;
};

} // namespace detail


template <typename... Opts>
struct Config {
    static_assert(
        (detail::is_codecs_v<Opts> && ...),
        "Unknown jetlog config option");

    using codecs = typename detail::make_codec_list<
        typename detail::pick_codecs<Opts...>::type>::type;
};

} // namespace jetlog
