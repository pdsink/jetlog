#pragma once

#include "format_parser.hpp"

#include <etl/to_string.h>
#include <etl/type_traits.h>
#include <etl/vector.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace jetlog {

constexpr uint8_t DataHeaderSize = 3;

struct DataHeader {
    uint16_t size;
    uint8_t typeId;
};

enum class DataType {
    I8, U8, I16, U16, I32, U32, I64, U64, Flt, Dbl, Str, LAST
};

struct FormatSpec {
    // In future, we can add spec parse to support width, precision, alignment,
    // etc. For now just remember spec data and do nothing.
    explicit FormatSpec(etl::string_view fmt = {}) : raw(fmt) {}

    etl::string_view raw;
};


template<typename T>
auto byteswap(T value) -> T {
    static_assert(etl::is_integral<T>::value, "Must be integral type");

    T result = 0;
    for(size_t i{0}; i < sizeof(T); ++i) {
        result = (result << 8) | (value & 0xFF);
        value >>= 8;
    }
    return result;
}


class EncoderHelpers {
protected:
    template<typename TOUT>
    static void writeHeader(uint32_t paramTypeID, uint32_t size, TOUT& out) {
        const uint32_t headerValue = (paramTypeID << 16) | size;
        out.push_back(static_cast<uint8_t>(headerValue));
        out.push_back(static_cast<uint8_t>(headerValue >> 8));
        out.push_back(static_cast<uint8_t>(headerValue >> 16));
    }
};

// Helper to define encoders
template <typename T, typename BaseType, DataType TypeId, bool IsSigned>
class EncoderNumeric : public EncoderHelpers {
public:
    static constexpr bool matchType = sizeof(T) == sizeof(BaseType)
        && etl::is_integral<T>::value
        && !(IsSigned ^ etl::is_signed<T>::value);

    template <typename TOUT>
    static void write(const T& value, TOUT& out) {
        T val = value;

        writeHeader(static_cast<uint8_t>(TypeId), sizeof(BaseType), out);

        for (size_t i{0}; i < sizeof(BaseType); i++) {
            out.push_back(static_cast<uint8_t>(val & 0xFF));
            val = val >> 8;
        }
    }
};


template <typename T>
using EncoderI8 = EncoderNumeric<T, int8_t, DataType::I8, true>;

template <typename T>
using EncoderI16 = EncoderNumeric<T, int16_t, DataType::I16, true>;

template <typename T>
using EncoderI32 = EncoderNumeric<T, int32_t, DataType::I32, true>;

template <typename T>
using EncoderI64 = EncoderNumeric<T, int64_t, DataType::I64, true>;

template <typename T>
using EncoderU8 = EncoderNumeric<T, uint8_t, DataType::U8, false>;

template <typename T>
using EncoderU16 = EncoderNumeric<T, uint16_t, DataType::U16, false>;

template <typename T>
using EncoderU32 = EncoderNumeric<T, uint32_t, DataType::U32, false>;

template <typename T>
using EncoderU64 = EncoderNumeric<T, uint64_t, DataType::U64, false>;


template <typename T>
class EncoderFlt : public EncoderHelpers {
public:
    static constexpr bool matchType = etl::is_same<T, float>::value && sizeof(float) == 4;

    template <typename TOUT>
    static void write(const T& value, TOUT& out) {
        // Transform to 4-bytes int
        uint32_t result{0};
        memcpy(&result, &value, sizeof(result));

        // Always store as little-endian (swap bytes if needed)
        #if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
            result = byteswap(result);
        #endif

        writeHeader(static_cast<uint8_t>(DataType::Flt), sizeof(result), out);

        for (size_t i{0}; i < sizeof(result); i++) {
            out.push_back(static_cast<uint8_t>(result & 0xFF));
            result = result >> 8;
        }
    }
};

template <typename T>
class EncoderDbl : public EncoderHelpers {
public:
    static constexpr bool matchType = etl::is_same<T, double>::value && sizeof(double) == 8;

    template <typename TOUT>
    static void write(const T& value, TOUT& out) {
        // Transform to 8-bytes int
        uint64_t result{0};
        memcpy(&result, &value, sizeof(result));

        // Always store as little-endian (swap bytes if needed)
        #if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
            result = byteswap(result);
        #endif

        writeHeader(static_cast<uint8_t>(DataType::Dbl), sizeof(result), out);

        for (size_t i{0}; i < sizeof(result); i++) {
            out.push_back(static_cast<uint8_t>(result & 0xFF));
            result = result >> 8;
        }
    }
};

// Encoder for string classes (std::string, etl::string etc)
template <typename T>
class EncoderStdString : public EncoderHelpers {
public:
    template <typename U = T, typename = typename U::value_type>
    static auto test(int) -> decltype(
        ((U*)nullptr)->begin(),
        ((U*)nullptr)->end(),
        ((U*)nullptr)->length(),
        ((U*)nullptr)->c_str(),  // Optional, let's use for more strict check
        // Check strings elements are 1-byte chars
        typename etl::enable_if<sizeof(typename U::value_type) == 1, int>::type(0),
        etl::true_type{}
    );

    template <typename>
    static auto test(...) -> etl::false_type;

    static constexpr bool matchType =
        etl::is_class<T>::value &&
        decltype(test<T>(0))::value;

    template <typename TOUT>
    static void write(const T& value, TOUT& out) {
        writeHeader(static_cast<uint8_t>(DataType::Str), value.length(), out);
        out.insert(out.end(), value.begin(), value.end());
    }
};

// Encoder for char*, const char*. Literal's char[N] are decayed in push() to
// reduce specialization for literal arguments
template <typename T>
class EncoderCString : public EncoderHelpers {
public:
    static constexpr bool matchType =
        etl::is_same<T, char*>::value ||
        etl::is_same<T, const char*>::value;

    template <typename TOUT>
    static void write(const char* value, TOUT& out) {
        size_t length{strlen(value)};
        writeHeader(static_cast<uint8_t>(DataType::Str), length, out);
        out.insert(out.end(), value, value + length);
    }
};


// Interface for all decoder classes
class IDecoder {
public:
    static auto matchTypeTag(uint8_t) -> bool {
        assert("This method must be overriden");
    }

    explicit IDecoder(const etl::ivector<uint8_t>& in, uint32_t recordOffset)
        : input{in}
        , dataOffset{recordOffset + DataHeaderSize}
        , dataSize{readHeader(in, recordOffset).size}
    {}

    void format(etl::istring& out, etl::string_view fmt = {}) {
        (void)out; (void)fmt;
        assert("This method must be overriden");
    }

    static auto isAvailableAt(const etl::ivector<uint8_t>& in, uint32_t recordOffset) -> bool {
        return (recordOffset + DataHeaderSize <= in.size()) &&
            (recordOffset + DataHeaderSize + readHeader(in, recordOffset).size <= in.size());
    }

    template<typename T>
    static auto getAsNum(const etl::ivector<uint8_t>& in, uint32_t recordOffset) -> T {
        static_assert(etl::is_integral<T>::value, "Type must be integral");

        if (!isAvailableAt(in, recordOffset)) { return T{0}; }

        const uint32_t dataOffset = recordOffset + DataHeaderSize;
        const uint32_t dataSize = readHeader(in, recordOffset).size;

        T result = 0;
        for (size_t i = 0; i < sizeof(T) && i < dataSize; ++i) {
            result |= static_cast<T>(in[dataOffset + i]) << (8 * i);
        }
        return result;
    }

    static auto getAsStringView(const etl::ivector<uint8_t>& in, uint32_t recordOffset) -> etl::string_view {
        if (!isAvailableAt(in, recordOffset)) { return etl::string_view(); }

        uint32_t dataSize = readHeader(in, recordOffset).size;
        if (dataSize == 0) { return etl::string_view(); }

        uint32_t dataOffset = recordOffset + DataHeaderSize;

        return etl::string_view(
            reinterpret_cast<const char*>(&in[dataOffset]),
            dataSize
        );
    }

    static auto getNextOffset(const etl::ivector<uint8_t>& in, uint32_t recordOffset) -> uint32_t {
        if (recordOffset + DataHeaderSize >= in.size()) { return in.size(); }

        return recordOffset + DataHeaderSize + readHeader(in, recordOffset).size;
    }

    static auto readHeader(const etl::ivector<uint8_t>& in, uint32_t recordOffset) -> DataHeader {
        return {
            static_cast<uint16_t>(in[recordOffset] | (static_cast<uint16_t>(in[recordOffset + 1]) << 8)),
            static_cast<uint8_t>(in[recordOffset + 2])
        };
    }


protected:
    const etl::ivector<uint8_t>& input;
    uint32_t dataOffset;
    uint32_t dataSize;
};

// Helper to define decoders
template <typename T, DataType TypeId>
class DecoderNumeric : public IDecoder {
public:
    explicit DecoderNumeric(const etl::ivector<uint8_t>& in, uint32_t recordOffset) : IDecoder(in, recordOffset) {}

    static auto matchTypeTag(uint8_t ttag) -> bool {
        return ttag == static_cast<uint8_t>(TypeId);
    }

    void format(etl::istring& out, etl::string_view fmt = {}) {
        // 0bXXX... for uint64_t - max possible lendth 64+2
        etl::format_spec spec;

        FormatParser::parse_format(fmt, 0, spec);
        etl::to_string(pickValue(), out, spec, true);
    }

protected:
    auto pickValue() -> T {
        T val{0};
        for (size_t i{0}; i < dataSize; i++) {
            val |= static_cast<T>(input[dataOffset + i]) << (i * 8);
        }
        return val;
    }
};


using DecoderI8 = DecoderNumeric<int8_t, DataType::I8>;

using DecoderI16 = DecoderNumeric<int16_t, DataType::I16>;

using DecoderI32 = DecoderNumeric<int32_t, DataType::I32>;

using DecoderI64 = DecoderNumeric<int64_t, DataType::I64>;

using DecoderU8 = DecoderNumeric<uint8_t, DataType::U8>;

using DecoderU16 = DecoderNumeric<uint16_t, DataType::U16>;

using DecoderU32 = DecoderNumeric<uint32_t, DataType::U32>;

using DecoderU64 = DecoderNumeric<uint64_t, DataType::U64>;

class DecoderFlt : public IDecoder {
public:
    explicit DecoderFlt(const etl::ivector<uint8_t>& in, uint32_t recordOffset) : IDecoder(in, recordOffset) {}

    static auto matchTypeTag(uint8_t ttag) -> bool {
        return ttag == static_cast<uint8_t>(DataType::Flt);
    }

    void format(etl::istring& out, etl::string_view fmt = {}) {
        (void)fmt;
        // force std::to_string() like behaviour - 6 digits fractional part.
        etl::to_string(pickValue(), out, etl::format_spec().precision(6), true);
    }

protected:
    auto pickValue() -> float {
        uint32_t val{0};
        for (size_t i{0}; i < dataSize; i++) {
            val |= static_cast<uint32_t>(input[dataOffset + i]) << (i * 8);
        }

        // Convert to big-endian is needed
        #if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
            val = byteswap(val);
        #endif

        float result;
        memcpy(&result, &val, sizeof(result));
        return result;
    }
};

class DecoderDbl : public IDecoder {
public:
    explicit DecoderDbl(const etl::ivector<uint8_t>& in, uint32_t recordOffset) : IDecoder(in, recordOffset) {}

    static auto matchTypeTag(uint8_t ttag) -> bool {
        return ttag == static_cast<uint8_t>(DataType::Dbl);
    }

    void format(etl::istring& out, etl::string_view fmt = {}) {
        (void)fmt;
        // force std::to_string() like behaviour - 6 digits fractional part.
        etl::to_string(pickValue(), out, etl::format_spec().precision(6), true);
    }

protected:
    auto pickValue() -> double {
        uint64_t val{0};
        for (size_t i{0}; i < dataSize; i++) {
            val |= static_cast<uint64_t>(input[dataOffset + i]) << (i * 8);
        }

        // Convert to big-endian is needed
        #if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
            val = byteswap(val);
        #endif

        double result{0};
        memcpy(&result, &val, sizeof(result));
        return result;
    }
};

// Decoder for strings
class DecoderStr : public IDecoder {
public:
    explicit DecoderStr(const etl::ivector<uint8_t>& in, uint32_t recordOffset)
        : IDecoder(in, recordOffset) {}

    static auto matchTypeTag(uint8_t ttag) -> bool {
        return ttag == static_cast<uint8_t>(DataType::Str);
    }

    void format(etl::istring& out, etl::string_view fmt = {}) {
        (void)fmt;
        out.append(reinterpret_cast<const char*>(&input[dataOffset]), dataSize);
    }
};

// Fake decoder for unrecognized types
class DecoderUnknown : public IDecoder {
public:
    explicit DecoderUnknown(const etl::ivector<uint8_t>& in, uint32_t recordOffset) : IDecoder(in, recordOffset) {}

    static auto matchTypeTag(uint8_t ttag) -> bool { (void)ttag; return false; }

    void format(etl::istring& out, etl::string_view fmt = {}) {
        (void)fmt;
        out.append("[UNKNOWN]");
    }
};

} // namespace jetlog
