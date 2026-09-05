#pragma once

#include "format_parser.hpp"

#include <etl/memory.h>
#include <etl/span.h>
#include <etl/string_view.h>
#include <etl/to_string.h>
#include <etl/type_traits.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace jetlog {

// Chunk memory handed to a codec: writable when a record is built, read only
// when it is read back.
using ByteSpan = etl::span<uint8_t>;
using ConstByteSpan = etl::span<const uint8_t>;


//
// A promise that a string outlives its records.
//
class static_str : public etl::string_view {
public:
    constexpr static_str() = default;

    template <size_t N>
    constexpr explicit static_str(const char (&text)[N])
        : etl::string_view(text, N - 1) {}

    explicit static_str(const char* text)
        : etl::string_view(text, text != nullptr ? strlen(text) : 0) {}

    constexpr static_str(const char* text, size_t size)
        : etl::string_view(text, size) {}
};


namespace detail {

// A string the writer was told to copy into the record.
class copy_str : public etl::string_view {
public:
    constexpr explicit copy_str(etl::string_view text) : etl::string_view(text) {}
};

} // namespace detail


//
// Numeric payloads use little endian regardless of the target's byte order.
// Static strings store native pointers, so complete records remain tied to
// the address space where they were written.
//

template <typename T>
inline auto store_le(T value, uint8_t* out) -> void {
    static_assert(etl::is_integral<T>::value, "integral only");

    using U = typename etl::make_unsigned<T>::type;
    U bits = static_cast<U>(value);

    for (size_t i = 0; i < sizeof(T); i++) {
        out[i] = static_cast<uint8_t>(bits >> (8 * i));
    }
}

template <typename T>
inline auto load_le(const uint8_t* in) -> T {
    static_assert(etl::is_integral<T>::value, "integral only");

    using U = typename etl::make_unsigned<T>::type;
    U bits = 0;

    for (size_t i = 0; i < sizeof(T); i++) {
        bits = static_cast<U>(bits | (static_cast<U>(in[i]) << (8 * i)));
    }

    return static_cast<T>(bits);
}


//
// Encoding byte layout:
//
//   0b0xxxxxxx   plain encoding
//   0b1xxxxxxx   static string, bits 0..6 are its length
//
namespace encoding {

// 0x00 belongs to the record itself, see wire.hpp.
constexpr uint8_t I8  = 0x01;
constexpr uint8_t U8  = 0x02;
constexpr uint8_t I16 = 0x03;
constexpr uint8_t U16 = 0x04;
constexpr uint8_t I32 = 0x05;
constexpr uint8_t U32 = 0x06;
constexpr uint8_t I64 = 0x07;
constexpr uint8_t U64 = 0x08;
constexpr uint8_t Flt = 0x09;
constexpr uint8_t Dbl = 0x0A;

// Copied string fragment: length, then text bytes. The encoding marks the last
// fragment, so no total string length is stored.
constexpr uint8_t StrCopyPartLast = 0x0B;
constexpr uint8_t StrCopyPart     = 0x0C;

// String left where it is: a pointer, and a length that did not fit the
// encoding byte.
constexpr uint8_t StrRefLong = 0x0D;

// Static string, length in the low bits. Zero length carries no pointer.
constexpr uint8_t StrRefCompact = 0x80;
constexpr uint8_t StrRefLenMask = 0x7F;

// Longest static string that fits the encoding byte itself, and the longest
// fragment a length byte can measure.
constexpr size_t MaxStrRefCompactLen = StrRefLenMask;
constexpr size_t MaxPartLen = 0xFF;

} // namespace encoding


// Carried between calls by the caller, not by the codec.
struct EncoderState {
    size_t input_offset;
    size_t produced_bytes;
    bool done;
};

struct DecoderState {
    size_t consumed_bytes;
    bool done;
};


//
// One list of codecs serves both sides. A codec answers four questions:
//
//   log write, asked by C++ type:
//     can_encode<T>()                             - takes this type?
//     encode(out, src, src_offset)         -> EncoderState
//
//   log read, asked by the encoding taken from the record:
//     can_decode(value_encoding)   - owns this encoding?
//     decode(in, out, spec)        -> DecoderState
//
// The core owns 0x00..0x0D and the whole 0x80..0xFF half, so 0x0E..0x7F is left
// for your own types.
//

// A static_str stored by pointer, without copying its text.
struct StrRef {
    static constexpr size_t PtrSize = sizeof(const char*);

    template <typename T>
    static constexpr auto can_encode() -> bool {
        return etl::is_same_v<T, static_str>;
    }

    template <typename T>
    static auto encode(ByteSpan out, const T& src,
        size_t src_offset) -> EncoderState
    {
        auto size = src.size();

        // Empty strings need only the encoding byte; omit the pointer.
        if (size == 0) {
            if (out.size() < 1) { return {src_offset, 0, false}; }

            out[0] = encoding::StrRefCompact;
            return {size, 1, true};
        }

        const char* at = src.data();

        // Strings <= 127 bytes long keep size directly in encoding byte.
        if (size <= encoding::MaxStrRefCompactLen) {
            if (out.size() < 1 + PtrSize) { return {src_offset, 0, false}; }

            out[0] = static_cast<uint8_t>(encoding::StrRefCompact | size);
            etl::mem_copy(reinterpret_cast<const uint8_t*>(&at), PtrSize, out.data() + 1);

            return {size, 1 + PtrSize, true};
        }

        if (out.size() < 3 + PtrSize) { return {src_offset, 0, false}; }

        // Strings >= 128 bytes go with 2 byte length and pointer
        out[0] = encoding::StrRefLong;
        store_le(static_cast<uint16_t>(size), out.data() + 1);
        etl::mem_copy(reinterpret_cast<const uint8_t*>(&at), PtrSize, out.data() + 3);

        return {size, 3 + PtrSize, true};
    }

    static auto can_decode(uint8_t value_encoding) -> bool {
        return ((value_encoding & encoding::StrRefCompact) != 0)
            || value_encoding == encoding::StrRefLong;
    }

    // raw refers to the original text. consumed_bytes counts the encoding,
    // optional length and pointer in the record, not the text length.
    static auto decode_as_raw(ConstByteSpan in, ConstByteSpan& raw) -> DecoderState
    {
        const char* at = nullptr;

        if (in[0] == encoding::StrRefLong) {
            etl::mem_copy(in.data() + 3, PtrSize, reinterpret_cast<uint8_t*>(&at));
            raw = ConstByteSpan(reinterpret_cast<const uint8_t*>(at),
                                load_le<uint16_t>(in.data() + 1));

            return {3 + PtrSize, true};
        }

        size_t size = in[0] & encoding::StrRefLenMask;

        // An empty string is the encoding byte and nothing else.
        if (size == 0) {
            raw = {};
            return {1, true};
        }

        etl::mem_copy(in.data() + 1, PtrSize, reinterpret_cast<uint8_t*>(&at));
        raw = ConstByteSpan(reinterpret_cast<const uint8_t*>(at), size);

        return {1 + PtrSize, true};
    }

    static auto decode(ConstByteSpan in, etl::istring& out,
        const etl::string_view&) -> DecoderState
    {
        ConstByteSpan raw{};
        auto state = decode_as_raw(in, raw);

        if (!raw.empty()) {
            const char* at = reinterpret_cast<const char*>(raw.data());
            out.append(at, at + raw.size());
        }

        return state;
    }
};


// A string copied into the record, in fragments of what the chunks allow.
struct StrCopy {
    template <typename T>
    static constexpr auto can_encode() -> bool {
        return etl::is_same_v<T, detail::copy_str>;
    }

    template <typename T>
    static auto encode(ByteSpan out, const T& src,
        size_t src_offset) -> EncoderState
    {
        const uint8_t* data = reinterpret_cast<const uint8_t*>(src.data());
        auto size = src.size();
        size_t rest = size - src_offset;

        // Reserve encoding and length bytes; nonempty input also needs room
        // for at least one text byte to make progress.
        if (out.size() < 2 || (rest > 0 && out.size() < 3)) {
            return {src_offset, 0, false};
        }

        size_t take = rest;
        if (take > out.size() - 2) { take = out.size() - 2; }
        if (take > encoding::MaxPartLen) { take = encoding::MaxPartLen; }

        bool last = src_offset + take == size;

        out[0] = last ? encoding::StrCopyPartLast : encoding::StrCopyPart;
        out[1] = static_cast<uint8_t>(take);
        etl::mem_copy(data + src_offset, take, out.data() + 2);

        return {src_offset + take, 2 + take, last};
    }

    static auto can_decode(uint8_t value_encoding) -> bool {
        return value_encoding == encoding::StrCopyPartLast
            || value_encoding == encoding::StrCopyPart;
    }

    static auto decode(ConstByteSpan in, etl::istring& out,
        const etl::string_view&) -> DecoderState
    {
        size_t size = in[1];
        const char* at = reinterpret_cast<const char*>(in.data() + 2);
        out.append(at, at + size);

        return {2 + size, in[0] == encoding::StrCopyPartLast};
    }
};


//
// Shared integer codec, selected by width and signedness.
//
// A type is taken by its properties rather than by name, so `long` lands in the
// right codec whatever its width on the platform. A `bool` goes to U8 as 0 or 1.
//
template <typename Value, uint8_t ValueEncoding>
struct IntCodec {
    template <typename T>
    static constexpr auto can_encode() -> bool {
        if constexpr (etl::is_same_v<T, bool>) {
            return etl::is_same_v<Value, uint8_t>;
        } else {
            return etl::is_integral_v<T>
                && sizeof(T) == sizeof(Value)
                && etl::is_signed_v<T> == etl::is_signed_v<Value>;
        }
    }

    template <typename T>
    static auto encode(ByteSpan out, const T& src, size_t) -> EncoderState
    {
        if (out.size() < 1 + sizeof(Value)) { return {0, 0, false}; }

        out[0] = ValueEncoding;
        store_le(static_cast<Value>(src), out.data() + 1);

        return {sizeof(Value), 1 + sizeof(Value), true};
    }

    static auto can_decode(uint8_t value_encoding) -> bool {
        return value_encoding == ValueEncoding;
    }

    static auto decode_as_raw(ConstByteSpan in, ConstByteSpan& raw) -> DecoderState
    {
        raw = ConstByteSpan(in.data() + 1, sizeof(Value));

        return {1 + sizeof(Value), true};
    }

    static auto decode(ConstByteSpan in, etl::istring& out,
        const etl::string_view& spec) -> DecoderState
    {
        etl::format_spec parsed;
        FormatParser::parse_format(spec, 0, parsed);

        etl::to_string(load_le<Value>(in.data() + 1), out, parsed, true);

        return {1 + sizeof(Value), true};
    }
};


// `char` is signed or unsigned depending on the platform, so a byte above 0x7F
// prints differently. Use int8_t / uint8_t for a stable result.
struct I8  : IntCodec<int8_t,   encoding::I8>  {};
struct U8  : IntCodec<uint8_t,  encoding::U8>  {};
struct I16 : IntCodec<int16_t,  encoding::I16> {};
struct U16 : IntCodec<uint16_t, encoding::U16> {};
struct I32 : IntCodec<int32_t,  encoding::I32> {};
struct U32 : IntCodec<uint32_t, encoding::U32> {};

// Off by default: formatting may require 64-bit division helpers.
struct I64 : IntCodec<int64_t,  encoding::I64> {};
struct U64 : IntCodec<uint64_t, encoding::U64> {};


//
// Flt and Dbl share this implementation. Both are off by default because
// formatting may require software floating-point arithmetic.
//
template <typename Value, uint8_t ValueEncoding>
struct FloatCodec {
    // Floats travel as their bit pattern, in the same byte order as everything
    // else.
    using Bits = typename etl::conditional<sizeof(Value) == 4, uint32_t, uint64_t>::type;

    template <typename T>
    static auto encode(ByteSpan out, const T& src, size_t) -> EncoderState
    {
        if (out.size() < 1 + sizeof(Value)) { return {0, 0, false}; }

        Bits bits = 0;
        etl::mem_copy(reinterpret_cast<const uint8_t*>(&src), sizeof(Value),
                      reinterpret_cast<uint8_t*>(&bits));

        out[0] = ValueEncoding;
        store_le(bits, out.data() + 1);

        return {sizeof(Value), 1 + sizeof(Value), true};
    }

    static auto can_decode(uint8_t value_encoding) -> bool {
        return value_encoding == ValueEncoding;
    }

    static auto decode(ConstByteSpan in, etl::istring& out,
        const etl::string_view&) -> DecoderState
    {
        Value value = 0;
        Bits bits = load_le<Bits>(in.data() + 1);
        etl::mem_copy(reinterpret_cast<const uint8_t*>(&bits), sizeof(Value),
                      reinterpret_cast<uint8_t*>(&value));

        // Six digits after the point, like std::to_string; placeholder specs
        // are ignored. For fixed-point formatting, log the integer and
        // fractional parts separately.
        etl::to_string(value, out, etl::format_spec().precision(6), true);

        return {1 + sizeof(Value), true};
    }
};


struct Flt : FloatCodec<float, encoding::Flt> {
    template <typename T>
    static constexpr auto can_encode() -> bool { return etl::is_same_v<T, float>; }
};


struct Dbl : FloatCodec<double, encoding::Dbl> {
    template <typename T>
    static constexpr auto can_encode() -> bool { return etl::is_same_v<T, double>; }
};


//
// Codec dispatch.
//
namespace detail {

// Returns whether C accepts T. if constexpr avoids instantiating C::encode()
// for unsupported types.
template <typename C, typename T>
auto try_encode(
    ByteSpan out,
    const T& src,
    size_t src_offset,
    EncoderState& state
) -> bool
{
    if constexpr (C::template can_encode<T>()) {
        state = C::encode(out, src, src_offset);
        return true;
    } else {
        (void)out; (void)src; (void)src_offset; (void)state;
        return false;
    }
}

} // namespace detail


template <typename... Cs>
struct CodecList {
    template <typename T>
    static constexpr auto can_encode() -> bool {
        return (Cs::template can_encode<T>() || ...);
    }

    template <typename T>
    static auto encode(ByteSpan out, const T& src,
        size_t src_offset) -> EncoderState
    {
        static_assert(can_encode<T>(),
            "jetlog: no codec for this type - add one to Codecs<>");

        EncoderState state{src_offset, 0, false};

        // Short circuits on the first codec that takes the type.
        (detail::try_encode<Cs>(out, src, src_offset, state) || ...);

        return state;
    }

    static auto decode(ConstByteSpan in, etl::istring& out,
        const etl::string_view& spec, DecoderState& state) -> bool
    {
        state = DecoderState{0, false};

        // Short circuits on the first codec that claims the encoding. The cast
        // is for gcc 8, which warns about `&&` inside a `||` fold even when the
        // operand is parenthesized.
        return (bool(Cs::can_decode(in[0])
            && (state = Cs::decode(in, out, spec), true)) || ...);
    }
};

} // namespace jetlog
