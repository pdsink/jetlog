#pragma once

#include "types.hpp"

namespace jetlog {

template<bool... Bs>
struct bool_or : etl::false_type {};

template<bool B, bool... Bs>
struct bool_or<B, Bs...> : etl::conditional<B, etl::true_type, bool_or<Bs...>>::type {};


// Helper to call Encoder ONLY if matched (because only in this case write()
// will accept desired type)
template<template<typename> class E, typename T, typename TOUT>
typename etl::enable_if<E<T>::matchType>::type
call_encoder(const T& v, TOUT& out) {
    E<T>::write(v, out);
}

template<template<typename> class E, typename T, typename TOUT>
typename etl::enable_if<!E<T>::matchType>::type
call_encoder(const T&, TOUT&) {
    // do nothing if not matched
}


template<template<typename> class... Es>
struct EncoderList {
    template<typename T>
    struct has_matching_trait {
        static constexpr bool value = bool_or<Es<T>::matchType...>::value;
    };

    template<typename T, typename TOUT>
    static void write(const T& value, TOUT& out) {
        static_assert(has_matching_trait<T>::value, "No matching encoder found");

        int dummy[] = { (call_encoder<Es, T, TOUT>(value, out), 0)... };
        (void)dummy;
    }
};


template<typename... Ds>
struct DecoderList {
    static bool format(const etl::ivector<uint8_t>& data, size_t offset, etl::istring& output, etl::string_view fmt = {}) {
        if (!IDecoder::isAvailableAt(data, offset)) { return false; }

        bool decoded = false;

        int dummy[] = {
            (Ds::matchTypeTag(IDecoder::readHeader(data, offset).typeId)
                 ? (Ds(data, offset).format(output, fmt), decoded = true, 0)
                 : 0)...
        };
        (void)dummy;

        if (!decoded) { DecoderUnknown(data, offset).format(output, fmt); }
        return true;
    }
};


//
// Several pre-defined list variants for quick-choose
//

using ParamEncoders_32_No_Float = EncoderList<
    EncoderI8, EncoderU8, EncoderI16, EncoderU16, EncoderI32, EncoderU32,
    EncoderStdString, EncoderCString
>;

using ParamDecoders_32_No_Float = DecoderList<
    DecoderI8, DecoderU8, DecoderI16, DecoderU16, DecoderI32, DecoderU32,
    DecoderStr
>;

using ParamEncoders_32_And_Float = EncoderList<
    EncoderI8, EncoderU8, EncoderI16, EncoderU16, EncoderI32, EncoderU32,
    EncoderStdString, EncoderCString,
    EncoderFlt
>;

using ParamDecoders_32_And_Float = DecoderList<
    DecoderI8, DecoderU8, DecoderI16, DecoderU16, DecoderI32, DecoderU32,
    DecoderStr,
    DecoderFlt
>;

using ParamEncoders_64_And_Double = EncoderList<
    EncoderI8, EncoderU8, EncoderI16, EncoderU16, EncoderI32, EncoderU32,
    EncoderStdString, EncoderCString,
    EncoderI64, EncoderU64,
    EncoderFlt,
    EncoderDbl
>;

using ParamDecoders_64_And_Double = DecoderList<
    DecoderI8, DecoderU8, DecoderI16, DecoderU16, DecoderI32, DecoderU32,
    DecoderStr,
    DecoderI64, DecoderU64,
    DecoderFlt,
    DecoderDbl
>;

} // namespace jetlog
