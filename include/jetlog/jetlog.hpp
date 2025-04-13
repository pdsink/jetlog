#pragma once

#include "private/ring_buffer.hpp"
#include "private/string_tokenizer.hpp"
#include "private/typelists.hpp"

#include <etl/limits.h>
#include <etl/type_traits.h>
#include <etl/utility.h>

namespace jetlog {

namespace level {
    enum Type : int8_t {
        error,
        warn,
        info,
        debug,
        verbose
    };
} // namespace level

template <typename Char, size_t N>
constexpr auto decayLiteralArg(Char (&s)[N]) noexcept -> typename
etl::enable_if<etl::is_same<typename etl::remove_cv<Char>::type, char>::value, const char*>::type
{
    return static_cast<const char*>(s);
}

template <typename T>
constexpr T&& decayLiteralArg(T&& x) noexcept
{
    return etl::forward<T>(x);
}




template <
    size_t MaxRecordSize = 256,
    typename Encoders = jetlog::ParamEncoders_32_And_Float
>
class Writer {
public:
    explicit Writer(jetlog::IRingBuffer& buf) : ringBuffer{buf} {}

    template<typename... Args>
    auto push(const char* tag, uint8_t level, const char* message, const Args&... msgArgs) -> bool {
        // record should be vector, but we use string to control overflow
        etl::string<MaxRecordSize> record{};
        record.clear();

        Encoders::write(getTime(), record);
        Encoders::write(tag, record);
        Encoders::write(level, record);
        Encoders::write(message, record);

        int dummy[] = { 0, (Encoders::write(jetlog::decayLiteralArg(msgArgs), record), 0)... };
        (void)dummy;

        bool size_ok = !record.is_truncated();

        if (!size_ok) {
            // If data too big, write truncated stub
            record.clear();
            Encoders::write(getTime(), record);
            Encoders::write(tag, record);
            Encoders::write(level, record);
            static const char* stub = "[TRUNCATED]";
            Encoders::write(stub, record);
        }

        return ringBuffer.writeRecord(reinterpret_cast<const uint8_t*>(record.data()), record.size()) &&
            size_ok;
    }

    virtual auto getTime() -> uint32_t {
        return etl::numeric_limits<uint32_t>::max();
    }

private:
    jetlog::IRingBuffer& ringBuffer;
};


template <
    size_t MaxRecordSize = 256,
    typename Decoders = jetlog::ParamDecoders_32_And_Float
>
class Reader {
public:
    explicit Reader(jetlog::IRingBuffer& buf) : ringBuffer{buf} {}

    auto pull(etl::istring& output) -> bool {
        etl::vector<uint8_t, MaxRecordSize> record;
        if (!ringBuffer.readRecord(record)) { return false; }

        int32_t offset = 0;

        if (!IDecoder::isAvailableAt(record, offset)) { return false; }
        auto timestamp = IDecoder::getAsNum<uint32_t>(record, offset);
        offset = IDecoder::getNextOffset(record, offset);

        if (!IDecoder::isAvailableAt(record, offset)) { return false; }
        auto tag = IDecoder::getAsStringView(record, offset);
        offset = IDecoder::getNextOffset(record, offset);

        if (!IDecoder::isAvailableAt(record, offset)) { return false; }
        auto level = IDecoder::getAsNum<uint8_t>(record, offset);
        offset = IDecoder::getNextOffset(record, offset);

        if (!IDecoder::isAvailableAt(record, offset)) { return false; }
        auto tokenizer = StringTokenizer(IDecoder::getAsStringView(record, offset));
        offset = IDecoder::getNextOffset(record, offset);

        writeLogHeader(output, timestamp, tag, level);

        for (const auto& token : tokenizer) {
            if (token.is_placeholder) {
                if (Decoders::format(record, offset, output, token.text)) {
                    offset = IDecoder::getNextOffset(record, offset);
                } else {
                    // no params left => write placeholder source
                    output.append(token.text.begin(), token.text.end());
                }
            } else {
                output.append(token.text.begin(), token.text.end());
            }
        }

        return true;
    }

    virtual void writeLogHeader(etl::istring& output, uint32_t timestamp, const etl::string_view& tag, uint8_t level) {
        output.append(level2str(level));

        if (timestamp != etl::numeric_limits<uint32_t>::max()) {
            output.append(" (");
            etl::to_string(timestamp, output, true);
            output.append(")");
        }

        if (tag.length() > 0) {
            output.append(" ");
            output.append(tag.begin(), tag.end());
        }
        output.append(": ");
    }

    virtual auto level2str(uint8_t level) -> const char* {
        switch (level) {
            case level::error: return "E";
            case level::warn: return "W";
            case level::info: return "I";
            case level::debug: return "D";
            case level::verbose: return "V";
            default: return "UNKNOWN";
        }
    }

private:
    jetlog::IRingBuffer& ringBuffer;
};

} // namespace jetlog
