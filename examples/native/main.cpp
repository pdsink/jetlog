//
// All configuration and usage examples. Every section builds its own buffer,
// writes a few records and prints them.
//
//     pio run -e native -t exec
//

#include "jetlog/jetlog.hpp"

#include <etl/memory.h>

#include <cstdio>
#include <string>
#include <vector>

#include <etl/array.h>
#include <etl/vector.h>

#define LVL_ERROR   jetlog::level::error
#define LVL_WARN    jetlog::level::warn
#define LVL_INFO    jetlog::level::info
#define LVL_DEBUG   jetlog::level::debug
#define LVL_VERBOSE jetlog::level::verbose

// pull() returns one record or loss notification; false means nothing is ready.
template <typename Reader>
static void drain(Reader& reader) {
    etl::string<256> line;

    puts("");

    while (reader.pull(line)) {
        printf("    %s\n", line.c_str());
        line.clear();
    }
}


//
// 1. A buffer, a writer and a reader.
//
// The buffer owns the memory. Writer and reader are templates over a config,
// Config<> is the default.
//
static void basics() {
    puts("\n1. Basics");

    jetlog::RingBuffer<4096> buffer;
    jetlog::Writer<> writer{buffer};
    jetlog::Reader<> reader{buffer};

    writer.push("main", LVL_INFO, "hello");
    drain(reader);
}


//
// 2. Tags and levels.
//
// The tag is a subsystem name, pass "" for none. Levels are error, warn, info,
// debug and verbose, and the reader prints them as a letter.
//
static void tags_and_levels() {
    puts("\n2. Tags and levels");

    jetlog::RingBuffer<4096> buffer;
    jetlog::Writer<> writer{buffer};
    jetlog::Reader<> reader{buffer};

    writer.push("net", LVL_ERROR, "link down");
    writer.push("net", LVL_WARN, "retrying");
    writer.push("", LVL_DEBUG, "no tag on this one");

    drain(reader);
}


//
// 3. Formatting.
//
// A subset of std::format: {} on its own, or a spec for base, width and zero
// padding. Formatting happens in the reader, not at the log point.
//
static void formatting() {
    puts("\n3. Formatting");

    jetlog::RingBuffer<4096> buffer;
    jetlog::Writer<> writer{buffer};
    jetlog::Reader<> reader{buffer};

    writer.push("fmt", LVL_INFO,
        "plain {}, hex {:04x}, upper {:X}, prefixed {:#x}, binary {:b}",
        42, 255, 255, 255, 5);

    drain(reader);
}


//
// 4. Strings.
//
// Tag and format are stored by pointer and must outlive their records.
// String arguments are copied unless wrapped in jetlog::static_str.
//
static void strings() {
    puts("\n4. Strings");

    using Config = jetlog::Config<>;

    jetlog::RingBuffer<4096> buffer;
    jetlog::Writer<Config> writer{buffer};
    jetlog::Reader<Config> reader{buffer};

    std::string name = "device-7";
    const char* format = "literal={} runtime={}";

    writer.push("str", LVL_INFO, jetlog::static_str(format), "abc", name);

    // The record kept its own copy of `name`, so this changes nothing.
    name = "overwritten";

    drain(reader);
}


//
// 5. Optional codecs.
//
// Integers up to 32 bits, bool and strings are enabled by default. Floats and
// 64-bit integers are opt-in because formatting them may require software
// arithmetic or 64-bit division helpers.
//
static void optional_codecs() {
    puts("\n5. Optional codecs");

    using Config = jetlog::Config<
        jetlog::Codecs<jetlog::Flt, jetlog::I64>
    >;

    jetlog::RingBuffer<4096> buffer;
    jetlog::Writer<Config> writer{buffer};
    jetlog::Reader<Config> reader{buffer};

    writer.push("num", LVL_INFO,
        "float {} and a big one {}",
        1.5f, static_cast<int64_t>(-9000000000));

    drain(reader);
}


//
// 6. Timestamps.
//
// Give the writer anything callable that returns uint32_t - a function, a
// functor or a lambda. Without one the header carries no time.
// Functors and capturing lambdas must be named objects that outlive the writer.
//
static uint32_t fake_clock() { return 1234; }

static void timestamps() {
    puts("\n6. Timestamps");

    jetlog::RingBuffer<4096> buffer;
    jetlog::Writer<> with_time{buffer, fake_clock};
    jetlog::Writer<> without_time{buffer};
    jetlog::Reader<> reader{buffer};

    with_time.push("t", LVL_INFO, "stamped");
    without_time.push("t", LVL_INFO, "not stamped");

    drain(reader);
}


//
// 7. Overflow.
//
// Nothing blocks or waits: a full buffer drops the oldest records. They are
// counted, and the reader reports the count.
//
static void overflow() {
    puts("\n7. Overflow");

    jetlog::RingBuffer<320, 32> buffer;   // deliberately tiny
    jetlog::Writer<> writer{buffer};
    jetlog::Reader<> reader{buffer};

    for (int i = 0; i < 15; i++) {
        writer.push("of", LVL_INFO, "record {}", i);
    }

    drain(reader);
}


//
// 8. Your own header.
//
// Override writeLogHeader() to change the prefix, level2str() to change level
// labels, or writeLossReport() to change loss notifications.
//
namespace {

class CustomHeaderReader : public jetlog::Reader<> {
public:
    explicit CustomHeaderReader(jetlog::IRingBuffer& buf) : jetlog::Reader<>(buf) {}

    void writeLogHeader(etl::istring& out, uint32_t, const etl::string_view& tag,
                        uint8_t lvl) override {
        out.append("[");
        out.append(level2str(lvl));
        out.append("/");
        out.append(tag.begin(), tag.end());
        out.append("] ");
    }
};

} // namespace

static void custom_header() {
    puts("\n8. Your own header");

    jetlog::RingBuffer<4096> buffer;
    jetlog::Writer<> writer{buffer};
    CustomHeaderReader reader{buffer};

    writer.push("app", LVL_WARN, "looks different now");
    drain(reader);
}


//
// 9. A type of your own.
//
// A codec says which C++ types it takes, how to write one, which encoding it
// owns, how a value of that encoding is laid out and how to print it back.
// Encodings 0x0E..0x7F are available for custom codecs.
//
// This codec prints a byte buffer as hex in a single record. String codecs
// would print the bytes as text; calling push() per byte would split the output
// into separate records.
//
namespace {

// Anything with data(), size() and a value_type of int8_t or uint8_t:
// std::vector, etl::vector, std::array, etl::array, spans, a class of your own.
// Containers of char are strings and go to the core codecs instead.
template <typename T, typename = void>
struct is_byte_array : etl::false_type {};

template <typename T>
struct is_byte_array<T, etl::void_t<
        typename T::value_type,
        decltype(etl::declval<const T&>().data()),
        decltype(etl::declval<const T&>().size())>>
    : etl::bool_constant<etl::is_same_v<typename T::value_type, uint8_t>
                      || etl::is_same_v<typename T::value_type, int8_t>> {};

// A value too long for one chunk is written as several fragments, so the codec
// gets two encodings: one for a fragment that ends the value, one for a
// fragment with more to come.
struct ByteArrayCodec {
    static constexpr uint8_t ArrCopyPartLast = 0x0E;
    static constexpr uint8_t ArrCopyPart = 0x0F;

    template <typename T>
    static constexpr auto can_encode() -> bool { return is_byte_array<T>::value; }

    template <typename T>
    static auto encode(jetlog::ByteSpan out, const T& src,
        size_t src_offset) -> jetlog::EncoderState
    {
        const uint8_t* data = reinterpret_cast<const uint8_t*>(src.data());
        size_t size = src.size();
        size_t rest = size - src_offset;

        if (out.size() < 2 || (rest > 0 && out.size() < 3)) {
            return {src_offset, 0, false};
        }

        size_t take = rest;
        if (take > out.size() - 2) { take = out.size() - 2; }
        if (take > jetlog::encoding::MaxPartLen) {
            take = jetlog::encoding::MaxPartLen;
        }

        bool last = src_offset + take == size;

        out[0] = last ? ArrCopyPartLast : ArrCopyPart;
        out[1] = static_cast<uint8_t>(take);
        etl::mem_copy(data + src_offset, take, out.data() + 2);

        return {src_offset + take, 2 + take, last};
    }

    static auto can_decode(uint8_t value_encoding) -> bool {
        return value_encoding == ArrCopyPartLast
            || value_encoding == ArrCopyPart;
    }

    static auto decode(jetlog::ConstByteSpan in, etl::istring& out,
        const etl::string_view&) -> jetlog::DecoderState
    {
        uint8_t value_encoding = in[0];
        size_t size = in[1];

        etl::format_spec spec = etl::format_spec().base(16).width(2)
            .fill('0').upper_case(true);

        for (size_t i = 0; i < size; i++) {
            if (i > 0) { out.append(" "); }
            etl::to_string(static_cast<uint32_t>(in[2 + i]), out, spec, true);
        }

        // Keep a space between the last byte here and the next fragment.
        if (value_encoding == ArrCopyPart && size > 0) { out.append(" "); }

        return {2 + size, value_encoding == ArrCopyPartLast};
    }
};

} // namespace

static void custom_codec() {
    puts("\n9. A type of your own");

    using Config = jetlog::Config<
        jetlog::Codecs<ByteArrayCodec>
    >;

    jetlog::RingBuffer<4096> buffer;
    jetlog::Writer<Config> writer{buffer};
    jetlog::Reader<Config> reader{buffer};

    std::vector<uint8_t> frame{0x0a, 0x1b, 0xff, 0x00};
    etl::vector<uint8_t, 4> tail{0xde, 0xad};
    etl::array<uint8_t, 3> mac{1, 2, 3};

    writer.push("rx", LVL_INFO, "frame={} tail={} mac={} empty=[{}]",
        frame, tail, mac, std::vector<uint8_t>{});

    drain(reader);
}


int main() {
    basics();
    tags_and_levels();
    formatting();
    strings();
    optional_codecs();
    timestamps();
    overflow();
    custom_header();
    custom_codec();

    return 0;
}
