#include <gtest/gtest.h>
#include "jetlog/jetlog.hpp"

#include <etl/limits.h>

#include <etl/memory.h>

#include <string>
#include <vector>
#include <array>

#include <etl/array.h>
#include <etl/vector.h>

namespace {

using Config = jetlog::Config<
    jetlog::Codecs<jetlog::Flt, jetlog::Dbl, jetlog::I64, jetlog::U64>
>;

uint32_t fake_time() { return 100; }

// Header out of the way, so tests look at the message only.
class BareReader : public jetlog::Reader<Config> {
public:
    explicit BareReader(jetlog::IRingBuffer& buf) : jetlog::Reader<Config>(buf) {}

    void writeLogHeader(etl::istring&, uint32_t, const etl::string_view&, uint8_t) override {}
};

} // namespace

//
// Basic writer/reader behavior.
//

TEST(Jetlog, HeaderCarriesTimeTagAndLevel) {
    jetlog::RingBuffer<4096> buffer{};
    jetlog::Writer<Config> writer{buffer, fake_time};
    jetlog::Reader<Config> reader{buffer};

    writer.push("app", jetlog::level::warn, "hello");

    etl::string<128> out;
    ASSERT_TRUE(reader.pull(out));
    ASSERT_EQ(out, etl::string<128>("W (100) app: hello"));
    ASSERT_FALSE(reader.pull(out));
}

TEST(Jetlog, TagAndFormatCanBeStaticStrings) {
    jetlog::RingBuffer<4096> buffer{};
    jetlog::Writer<Config> writer{buffer, fake_time};
    jetlog::Reader<Config> reader{buffer};

    jetlog::static_str tag{"app"};
    jetlog::static_str fmt{"n={}"};

    ASSERT_TRUE(writer.push(tag, jetlog::level::info, fmt, 5));

    etl::string<128> out;
    ASSERT_TRUE(reader.pull(out));
    ASSERT_EQ(out, etl::string<128>("I (100) app: n=5"));
}

TEST(Jetlog, EmptyBufferReadsNothing) {
    jetlog::RingBuffer<4096> buffer{};
    jetlog::Reader<Config> reader{buffer};

    etl::string<128> out;
    ASSERT_FALSE(reader.pull(out));
}

TEST(Jetlog, WritersOnOneBufferKeepPushOrder) {
    jetlog::RingBuffer<4096> buffer{};
    jetlog::Writer<Config> first{buffer};
    jetlog::Writer<Config> second{buffer};
    BareReader reader{buffer};

    first.push("t", 0, "from first");
    second.push("t", 0, "from second");
    first.push("t", 0, "first again");

    etl::string<128> out;

    ASSERT_TRUE(reader.pull(out));
    ASSERT_EQ(out, etl::string<128>("from first"));

    out.clear();
    ASSERT_TRUE(reader.pull(out));
    ASSERT_EQ(out, etl::string<128>("from second"));

    out.clear();
    ASSERT_TRUE(reader.pull(out));
    ASSERT_EQ(out, etl::string<128>("first again"));
}

namespace {

uint32_t plain_clock() { return 111; }

struct StatefulClock {
    uint32_t base;
    auto operator()() const -> uint32_t { return base + 1; }
};

} // namespace

TEST(Jetlog, TimeSourceTakesFunctionFunctorAndLambda) {
    jetlog::RingBuffer<4096> buffer{};
    jetlog::Reader<Config> reader{buffer};

    static StatefulClock clock_obj{2000};
    static uint32_t ticks = 300;
    static auto lambda = [] { return ticks; };

    jetlog::Writer<Config> from_function{buffer, plain_clock};
    jetlog::Writer<Config> from_functor{buffer, clock_obj};
    jetlog::Writer<Config> from_lambda{buffer, lambda};
    jetlog::Writer<Config> without_time{buffer};

    from_function.push("t", jetlog::level::info, "a");
    from_functor.push("t", jetlog::level::info, "b");
    from_lambda.push("t", jetlog::level::info, "c");
    without_time.push("t", jetlog::level::info, "d");

    etl::string<128> out;

    ASSERT_TRUE(reader.pull(out));
    ASSERT_EQ(out, etl::string<128>("I (111) t: a"));

    out.clear();
    ASSERT_TRUE(reader.pull(out));
    ASSERT_EQ(out, etl::string<128>("I (2001) t: b"));

    out.clear();
    ASSERT_TRUE(reader.pull(out));
    ASSERT_EQ(out, etl::string<128>("I (300) t: c"));

    // No time source at all: the header carries no timestamp.
    out.clear();
    ASSERT_TRUE(reader.pull(out));
    ASSERT_EQ(out, etl::string<128>("I t: d"));
}

TEST(Jetlog, EveryLevelHasItsLetter) {
    jetlog::RingBuffer<4096> buffer{};
    jetlog::Writer<Config> writer{buffer};
    jetlog::Reader<Config> reader{buffer};

    writer.push("", jetlog::level::error, "m");
    writer.push("", jetlog::level::warn, "m");
    writer.push("", jetlog::level::info, "m");
    writer.push("", jetlog::level::debug, "m");
    writer.push("", jetlog::level::verbose, "m");

    const char* expected[] = {"E: m", "W: m", "I: m", "D: m", "V: m"};

    for (const char* line : expected) {
        etl::string<64> out;
        ASSERT_TRUE(reader.pull(out));
        ASSERT_EQ(out, etl::string<64>(line));
    }
}

TEST(Jetlog, UnknownLevelPrintsAsUnknown) {
    jetlog::RingBuffer<4096> buffer{};
    jetlog::Writer<Config> writer{buffer};
    jetlog::Reader<Config> reader{buffer};

    writer.push("t", 99, "msg");

    etl::string<128> out;
    ASSERT_TRUE(reader.pull(out));
    ASSERT_EQ(out, etl::string<128>("UNKNOWN t: msg"));
}

//
// Values and formatting.
//

// Extremes expose wrong signedness during widening and overflow when formatting
// the minimum signed value.
TEST(JetlogFormatting, EveryIntegerWidthPrintsAtItsLimits) {
    jetlog::RingBuffer<4096> buffer{};
    jetlog::Writer<Config> writer{buffer};
    BareReader reader{buffer};

    writer.push("t", 0, "{} {} {}",
                etl::numeric_limits<int8_t>::min(),
                etl::numeric_limits<int8_t>::max(),
                etl::numeric_limits<uint8_t>::max());

    writer.push("t", 0, "{} {} {}",
                etl::numeric_limits<int16_t>::min(),
                etl::numeric_limits<int16_t>::max(),
                etl::numeric_limits<uint16_t>::max());

    writer.push("t", 0, "{} {} {}",
                etl::numeric_limits<int32_t>::min(),
                etl::numeric_limits<int32_t>::max(),
                etl::numeric_limits<uint32_t>::max());

    writer.push("t", 0, "{} {} {}",
                etl::numeric_limits<int64_t>::min(),
                etl::numeric_limits<int64_t>::max(),
                etl::numeric_limits<uint64_t>::max());

    const char* expected[] = {
        "-128 127 255",
        "-32768 32767 65535",
        "-2147483648 2147483647 4294967295",
        "-9223372036854775808 9223372036854775807 18446744073709551615"
    };

    for (const char* line : expected) {
        SCOPED_TRACE(line);

        etl::string<128> out;
        ASSERT_TRUE(reader.pull(out));
        ASSERT_EQ(out, etl::string<128>(line));
    }
}

TEST(JetlogFormatting, FloatsAndDoublesPrintTheirValue) {
    jetlog::RingBuffer<4096> buffer{};
    jetlog::Writer<Config> writer{buffer};
    BareReader reader{buffer};

    writer.push("t", 0, "{} {}", 1.5f, 2.25);

    etl::string<128> out;
    ASSERT_TRUE(reader.pull(out));
    ASSERT_EQ(out, etl::string<128>("1.500000 2.250000"));
}

TEST(JetlogFormatting, BoolPrintsAsNumber) {
    jetlog::RingBuffer<4096> buffer{};
    jetlog::Writer<Config> writer{buffer};
    BareReader reader{buffer};

    writer.push("t", 0, "{} {}", true, false);

    etl::string<128> out;
    ASSERT_TRUE(reader.pull(out));
    ASSERT_EQ(out, etl::string<128>("1 0"));
}

TEST(JetlogFormatting, MutableBitFields) {
    jetlog::RingBuffer<4096> buffer{};
    jetlog::Writer<Config> writer{buffer};
    BareReader reader{buffer};

    struct Flags {
        uint32_t ocp : 1;
        uint32_t otp : 1;
        int32_t signed_value : 5;
        bool ready : 1;
        uint64_t wide : 40;
    } flags{1, 0, -7, true, uint64_t{1} << 35};

    ASSERT_TRUE(writer.push("APP", jetlog::level::info,
        "OCP={}, OTP={}", flags.ocp, flags.otp));
    ASSERT_TRUE(writer.push("APP", jetlog::level::info,
        "{} {} {}", flags.signed_value, flags.ready, flags.wide));

    etl::string<128> out;
    ASSERT_TRUE(reader.pull(out));
    ASSERT_EQ(out, etl::string<128>("OCP=1, OTP=0"));
    out.clear();
    ASSERT_TRUE(reader.pull(out));
    ASSERT_EQ(out, etl::string<128>("-7 1 34359738368"));
}

TEST(JetlogFormatting, PlaceholderSpecPicksBaseAndPadding) {
    jetlog::RingBuffer<4096> buffer{};
    jetlog::Writer<Config> writer{buffer};
    BareReader reader{buffer};

    writer.push("t", 0, "{:x} {:X} {:#x} {:04x} {:b} {:d}",
                255, 255, 255, 255, 42, 42);

    etl::string<128> out;
    ASSERT_TRUE(reader.pull(out));
    ASSERT_EQ(out, etl::string<128>("ff FF 0xff 00ff 101010 42"));
}

TEST(JetlogFormatting, SameSpecWorksOnEveryIntegerWidth) {
    jetlog::RingBuffer<4096> buffer{};
    jetlog::Writer<Config> writer{buffer};
    BareReader reader{buffer};

    writer.push("t", 0, "{:04x} {:04x} {:04x} {:04x}",
                static_cast<uint8_t>(0xAB), static_cast<uint16_t>(0xABCD),
                static_cast<uint32_t>(0xABCD), static_cast<uint64_t>(0xABCD));

    etl::string<128> out;
    ASSERT_TRUE(reader.pull(out));
    ASSERT_EQ(out, etl::string<128>("00ab abcd abcd abcd"));
}

TEST(JetlogFormatting, MissingArgumentPrintsThePlaceholder) {
    jetlog::RingBuffer<4096> buffer{};
    jetlog::Writer<Config> writer{buffer};
    BareReader reader{buffer};

    writer.push("t", 0, "{} and {}", 1);

    etl::string<128> out;
    ASSERT_TRUE(reader.pull(out));
    ASSERT_EQ(out, etl::string<128>("1 and {}"));
}

TEST(JetlogFormatting, ExtraArgumentsAreIgnored) {
    jetlog::RingBuffer<4096> buffer{};
    jetlog::Writer<Config> writer{buffer};
    BareReader reader{buffer};

    writer.push("t", 0, "{}", 1, 2, 3);

    etl::string<128> out;
    ASSERT_TRUE(reader.pull(out));
    ASSERT_EQ(out, etl::string<128>("1"));
}

//
// String storage and boundaries.
//

namespace {

// A string class of its own, unrelated to etl and std.
class OwnString {
public:
    using value_type = char;

    explicit OwnString(const char* text) { strcpy(bytes, text); }

    auto data() const -> const char* { return bytes; }
    auto size() const -> size_t { return strlen(bytes); }

private:
    char bytes[16] = {};
};

const char long_static[] =
    "0123456789012345678901234567890123456789012345678901234567890123456789"
    "0123456789012345678901234567890123456789012345678901234567890123456789"
    "abcdefghij";

} // namespace

TEST(JetlogStrings, StaticStringArgumentsTravelAsPointers) {
    jetlog::RingBuffer<4096> buffer{};
    jetlog::Writer<Config> writer{buffer};
    BareReader reader{buffer};

    char text[] = "static";
    ASSERT_TRUE(writer.push("t", 0, "{}", jetlog::static_str(text)));
    strcpy(text, "edited");

    etl::string<128> out;
    ASSERT_TRUE(reader.pull(out));
    ASSERT_EQ(out, etl::string<128>("edited"));
}

TEST(JetlogStrings, RuntimeStringsAreCopied) {
    jetlog::RingBuffer<4096> buffer{};
    jetlog::Writer<Config> writer{buffer};
    BareReader reader{buffer};

    etl::string<64> owned{"owned"};
    char scratch[] = "scratch";
    const char* pointer = scratch;

    writer.push("t", 0, "{} {}", owned, pointer);

    // Overwrite both sources before reading to verify the record owns copies.
    owned.assign("xxxxx");
    memset(scratch, 'x', sizeof(scratch) - 1);

    etl::string<128> out;
    ASSERT_TRUE(reader.pull(out));
    ASSERT_EQ(out, etl::string<128>("owned scratch"));
}

TEST(JetlogStrings, MutableCharBufferIsCopied) {
    jetlog::RingBuffer<4096> buffer{};
    jetlog::Writer<Config> writer{buffer};
    BareReader reader{buffer};

    char scratch[16];
    strcpy(scratch, "alive");

    writer.push("t", 0, "{}", scratch);

    memset(scratch, 'X', sizeof(scratch) - 1);
    scratch[sizeof(scratch) - 1] = '\0';

    etl::string<128> out;
    ASSERT_TRUE(reader.pull(out));
    ASSERT_EQ(out, etl::string<128>("alive"));
}

TEST(JetlogStrings, StdStringIsCopied) {
    jetlog::RingBuffer<4096> buffer{};
    jetlog::Writer<Config> writer{buffer};
    BareReader reader{buffer};

    std::string text = "std_str";
    writer.push("t", 0, "{}", text);
    text = "xxxxxxx";

    etl::string<128> out;
    ASSERT_TRUE(reader.pull(out));
    ASSERT_EQ(out, etl::string<128>("std_str"));
}

TEST(JetlogStrings, ForeignStringClassIsCopied) {
    jetlog::RingBuffer<4096> buffer{};
    jetlog::Writer<Config> writer{buffer};
    BareReader reader{buffer};

    OwnString text{"foreign"};
    writer.push("t", 0, "{}", text);
    text = OwnString{"changed"};

    etl::string<128> out;
    ASSERT_TRUE(reader.pull(out));
    ASSERT_EQ(out, etl::string<128>("foreign"));
}

TEST(JetlogStrings, EmptyTagAndStringsPrintAsNothing) {
    jetlog::RingBuffer<4096> buffer{};
    jetlog::Writer<Config> writer{buffer};
    BareReader reader{buffer};

    etl::string<8> empty;
    const char* null_string = nullptr;
    writer.push("", 0, "[{}][{}]", empty, null_string);

    etl::string<128> out;
    ASSERT_TRUE(reader.pull(out));
    ASSERT_EQ(out, etl::string<128>("[][]"));
}

TEST(JetlogStrings, EmptyLiteralsPrintAsNothing) {
    jetlog::RingBuffer<4096> buffer{};
    jetlog::Writer<Config> writer{buffer};
    BareReader reader{buffer};

    writer.push("t", 0, "[{}][{}]", "", jetlog::static_str(""));

    etl::string<128> out;
    ASSERT_TRUE(reader.pull(out));
    ASSERT_EQ(out, etl::string<128>("[][]"));
}

TEST(JetlogStrings, LongCopiedStringSurvivesChunkBoundaries) {
    jetlog::RingBuffer<4096, 32> buffer{};
    jetlog::Writer<Config> writer{buffer};
    BareReader reader{buffer};

    etl::string<300> long_one;
    for (int i = 0; i < 200; i++) { long_one.push_back(static_cast<char>('a' + i % 26)); }

    writer.push("t", 0, "[{}]", long_one);

    etl::string<400> out;
    ASSERT_TRUE(reader.pull(out));

    etl::string<400> expected("[");
    expected.append(long_one.begin(), long_one.end());
    expected.append("]");

    ASSERT_EQ(out, expected);
}

TEST(JetlogStrings, StaticStringOver127Chars) {
    jetlog::RingBuffer<4096, 32> buffer{};
    jetlog::Writer<Config> writer{buffer};
    BareReader reader{buffer};

    jetlog::static_str source{long_static, jetlog::encoding::MaxStrRefCompactLen + 1};
    ASSERT_EQ(source.size(), jetlog::encoding::MaxStrRefCompactLen + 1);

    writer.push("t", 0, "[{}]", source);

    etl::string<400> out;
    ASSERT_TRUE(reader.pull(out));

    etl::string<400> expected("[");
    expected.append(source.begin(), source.end());
    expected.append("]");

    ASSERT_EQ(out, expected);
}

TEST(JetlogStrings, CopiedStringOver255Chars) {
    jetlog::RingBuffer<4096, 32> buffer{};
    jetlog::Writer<Config> writer{buffer};
    BareReader reader{buffer};

    etl::string<400> source;
    source.resize(jetlog::encoding::MaxPartLen + 1, 'x');
    ASSERT_EQ(source.size(), jetlog::encoding::MaxPartLen + 1);

    writer.push("t", 0, "{}", source);

    etl::string<400> out;
    ASSERT_TRUE(reader.pull(out));
    ASSERT_EQ(out, source);
}

TEST(JetlogStrings, TagAndFormatTravelAsPointers) {
    jetlog::RingBuffer<4096> buffer{};
    jetlog::Writer<> writer{buffer};
    jetlog::Reader<> reader{buffer};

    char tag[] = "old";
    char format[] = "old={}";
    const char (&const_tag)[sizeof(tag)] = tag;
    const char (&const_format)[sizeof(format)] = format;
    ASSERT_TRUE(writer.push(const_tag, jetlog::level::info, const_format, 7));
    ASSERT_TRUE(writer.push(jetlog::static_str(tag), jetlog::level::info,
        jetlog::static_str(format), 8));
    strcpy(tag, "new");
    strcpy(format, "new={}");

    etl::string<128> out;
    ASSERT_TRUE(reader.pull(out));
    ASSERT_EQ(out, etl::string<128>("I new: new=7"));
    out.clear();
    ASSERT_TRUE(reader.pull(out));
    ASSERT_EQ(out, etl::string<128>("I new: new=8"));
}

//
// User codecs.
//

namespace {

struct Point { int16_t x, y; };

struct CodecPoint {
    static constexpr uint8_t ValueEncoding = 0x20;

    template <typename T>
    static constexpr auto can_encode() -> bool { return etl::is_same_v<T, Point>; }

    template <typename T>
    static auto encode(
        jetlog::ByteSpan out,
        const T& src,
        size_t
    ) -> jetlog::EncoderState
    {
        if (out.size() < 5) { return {0, 0, false}; }

        out[0] = ValueEncoding;
        jetlog::store_le(src.x, out.data() + 1);
        jetlog::store_le(src.y, out.data() + 3);

        return {4, 5, true};
    }

    static auto can_decode(uint8_t value_encoding) -> bool {
        return value_encoding == ValueEncoding;
    }

    static auto decode(
        jetlog::ConstByteSpan in,
        etl::istring& out,
        const etl::string_view&
    ) -> jetlog::DecoderState
    {
        out.append("(");
        etl::to_string(jetlog::load_le<int16_t>(in.data() + 1), out, true);
        out.append(",");
        etl::to_string(jetlog::load_le<int16_t>(in.data() + 3), out, true);
        out.append(")");

        return {5, true};
    }
};

using PointConfig = jetlog::Config<jetlog::Codecs<CodecPoint>>;

class PointReader : public jetlog::Reader<PointConfig> {
public:
    explicit PointReader(jetlog::IRingBuffer& buf) : jetlog::Reader<PointConfig>(buf) {}

    void writeLogHeader(etl::istring&, uint32_t, const etl::string_view&, uint8_t) override {}
};

// Containers with data(), size() and a value_type of int8_t or uint8_t.
template <typename T, typename = void>
struct is_byte_array : etl::false_type {};

template <typename T>
struct is_byte_array<T, etl::void_t<
        typename T::value_type,
        decltype(etl::declval<const T&>().data()),
        decltype(etl::declval<const T&>().size())>>
    : etl::bool_constant<etl::is_same_v<typename T::value_type, uint8_t>
                      || etl::is_same_v<typename T::value_type, int8_t>> {};

struct ByteArrayCodec {
    static constexpr uint8_t ArrCopyPartLast = 0x21;
    static constexpr uint8_t ArrCopyPart     = 0x22;

    template <typename T>
    static constexpr auto can_encode() -> bool { return is_byte_array<T>::value; }

    template <typename T>
    static auto encode(
        jetlog::ByteSpan out,
        const T& src,
        size_t src_offset
    ) -> jetlog::EncoderState
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

    static auto decode(
        jetlog::ConstByteSpan in,
        etl::istring& out,
        const etl::string_view&
    ) -> jetlog::DecoderState
    {
        uint8_t value_encoding = in[0];
        size_t size = in[1];

        etl::format_spec spec = etl::format_spec().base(16).width(2)
            .fill('0').upper_case(true);

        for (size_t i = 0; i < size; i++) {
            if (i > 0) { out.append(" "); }
            etl::to_string(static_cast<uint32_t>(in[2 + i]), out, spec, true);
        }

        if (value_encoding == ArrCopyPart && size > 0) { out.append(" "); }

        return {2 + size, value_encoding == ArrCopyPartLast};
    }
};

using ArrayConfig = jetlog::Config<jetlog::Codecs<ByteArrayCodec>>;

class ArrayReader : public jetlog::Reader<ArrayConfig> {
public:
    explicit ArrayReader(jetlog::IRingBuffer& buf) : jetlog::Reader<ArrayConfig>(buf) {}

    void writeLogHeader(etl::istring&, uint32_t, const etl::string_view&, uint8_t) override {}
};

} // namespace

TEST(JetlogCustomCodecs, UserCodecPrintsItsOwnType) {
    jetlog::RingBuffer<4096> buffer{};
    jetlog::Writer<PointConfig> writer{buffer};
    PointReader reader{buffer};

    writer.push("geo", 0, "at {} and {}", Point{3, -4}, Point{10, 20});

    etl::string<128> out;
    ASSERT_TRUE(reader.pull(out));
    ASSERT_EQ(out, etl::string<128>("at (3,-4) and (10,20)"));
}

TEST(JetlogCustomCodecs, UserCodecHandlesValuesLargerThanAChunk) {
    jetlog::RingBuffer<4096, 32> buffer{};   // 30 bytes per chunk
    jetlog::Writer<ArrayConfig> writer{buffer};
    ArrayReader reader{buffer};

    std::vector<uint8_t> bytes(100);
    for (size_t i = 0; i < bytes.size(); i++) { bytes[i] = static_cast<uint8_t>(i); }

    writer.push("t", 0, "{}", bytes);

    etl::string<400> out;
    ASSERT_TRUE(reader.pull(out));

    etl::string<400> expected;
    for (size_t i = 0; i < bytes.size(); i++) {
        if (i > 0) { expected.append(" "); }
        etl::to_string(static_cast<uint32_t>(bytes[i]), expected,
                       etl::format_spec().base(16).width(2).fill('0').upper_case(true), true);
    }

    ASSERT_EQ(out, expected);
}

TEST(JetlogCustomCodecs, UserCodecTakesEveryByteContainer) {
    jetlog::RingBuffer<4096> buffer{};
    jetlog::Writer<ArrayConfig> writer{buffer};
    ArrayReader reader{buffer};

    etl::vector<uint8_t, 4> etl_vec{0xde, 0xad};
    etl::array<uint8_t, 3> etl_arr{1, 2, 3};
    std::array<uint8_t, 2> std_arr{0x0a, 0x1b};
    std::vector<int8_t> signed_vec{-1, 2};
    std::vector<uint8_t> empty;

    writer.push("t", 0, "{} {} {} {} [{}]", etl_vec, etl_arr, std_arr, signed_vec, empty);

    etl::string<128> out;
    ASSERT_TRUE(reader.pull(out));
    ASSERT_EQ(out, etl::string<128>("DE AD 01 02 03 0A 1B FF 02 []"));
}

//
// Hand-built records or mismatched codecs may produce malformed headers or
// unknown body types. Headers count as losses; unknown types print [UNKNOWN].
// Neither case may stop draining subsequent records.
//

namespace {

// Inject encodings and payloads the normal writer cannot produce. RawCodec
// only writes them; the reader's normal codecs handle the malformed records.
struct Raw {
    uint8_t value_encoding;
    const uint8_t* data;
    size_t size;
};

struct RawCodec {
    template <typename T>
    static constexpr auto can_encode() -> bool { return etl::is_same_v<T, Raw>; }

    template <typename T>
    static auto encode(
        jetlog::ByteSpan out,
        const T& src,
        size_t
    ) -> jetlog::EncoderState
    {
        if (out.size() < 1 + src.size) { return {0, 0, false}; }

        out[0] = src.value_encoding;
        memcpy(out.data() + 1, src.data, src.size);

        return {src.size, 1 + src.size, true};
    }

    static auto can_decode(uint8_t) -> bool { return false; }

    static auto decode(
        const uint8_t*,
        uint8_t,
        etl::istring&,
        const etl::string_view&
    ) -> jetlog::DecoderState
    {
        return {0, false};
    }
};

using RawList = jetlog::CodecList<RawCodec>;

// The first `valid` values of a header, then the record ends, so the reader
// fails on value number `valid` + 1.
void push_short_header(jetlog::IRingBuffer& buf, int valid) {
    jetlog::WireWriter<RawList> rec{buf};
    ASSERT_TRUE(rec.open());

    uint32_t stamp = 0;
    uint8_t stamp_bytes[4];
    jetlog::store_le(stamp, stamp_bytes);

    uint8_t lvl = 0;

    const char* fmt = "x";
    uint8_t pointer[sizeof(char*)];
    memcpy(pointer, &fmt, sizeof(char*));

    if (valid > 0) { ASSERT_TRUE(rec.put(Raw{jetlog::encoding::U32, stamp_bytes, 4})); }
    if (valid > 1) { ASSERT_TRUE(rec.put(Raw{jetlog::encoding::StrRefCompact, nullptr, 0})); }
    if (valid > 2) { ASSERT_TRUE(rec.put(Raw{jetlog::encoding::U8, &lvl, 1})); }
    if (valid > 3) {
        ASSERT_TRUE(rec.put(Raw{static_cast<uint8_t>(jetlog::encoding::StrRefCompact | 1),
                                pointer, sizeof(char*)}));
    }

    ASSERT_TRUE(rec.commit());
}

} // namespace

TEST(JetlogBrokenRecords, TruncatedHeaderIsCountedAndSteppedOver) {
    // Every place the header can end short: no values at all, then one, two and
    // three of the four.
    for (int valid = 0; valid < 4; valid++) {
        SCOPED_TRACE(valid);

        jetlog::RingBuffer<4096> buffer{};
        jetlog::Writer<Config> writer{buffer};
        BareReader reader{buffer};

        push_short_header(buffer, valid);
        writer.push("t", 0, "survivor");

        etl::string<128> out;
        ASSERT_TRUE(reader.pull(out));
        ASSERT_EQ(out, etl::string<128>("... records lost: 1 ..."));

        // The record behind the broken one is still reachable.
        out.clear();
        ASSERT_TRUE(reader.pull(out));
        ASSERT_EQ(out, etl::string<128>("survivor"));

        out.clear();
        ASSERT_FALSE(reader.pull(out));
    }
}

TEST(JetlogBrokenRecords, HeaderOfTheWrongTypeIsCountedAndSteppedOver) {
    jetlog::RingBuffer<4096> buffer{};
    jetlog::Writer<Config> writer{buffer};
    BareReader reader{buffer};

    // Four values, so nothing runs short - but the timestamp is not a U32.
    jetlog::WireWriter<RawList> rec{buffer};
    ASSERT_TRUE(rec.open());

    uint8_t byte = 0;
    ASSERT_TRUE(rec.put(Raw{jetlog::encoding::U8, &byte, 1}));
    ASSERT_TRUE(rec.put(Raw{jetlog::encoding::StrRefCompact, nullptr, 0}));
    ASSERT_TRUE(rec.put(Raw{jetlog::encoding::U8, &byte, 1}));

    const char* fmt = "x";
    uint8_t pointer[sizeof(char*)];
    memcpy(pointer, &fmt, sizeof(char*));
    ASSERT_TRUE(rec.put(Raw{static_cast<uint8_t>(jetlog::encoding::StrRefCompact | 1),
                            pointer, sizeof(char*)}));
    ASSERT_TRUE(rec.commit());

    writer.push("t", 0, "survivor");

    etl::string<128> out;
    ASSERT_TRUE(reader.pull(out));
    ASSERT_EQ(out, etl::string<128>("... records lost: 1 ..."));

    out.clear();
    ASSERT_TRUE(reader.pull(out));
    ASSERT_EQ(out, etl::string<128>("survivor"));
}

TEST(JetlogBrokenRecords, BrokenRecordGivesEveryChunkBack) {
    jetlog::RingBuffer<320, 32> buffer{};   // 10 chunks, 30 bytes each
    BareReader reader{buffer};

    // Two chunks of an encoding byte no codec owns, so the very first header check
    // fails.
    jetlog::WireWriter<RawList> rec{buffer};
    ASSERT_TRUE(rec.open());

    uint8_t junk[2] = {1, 2};
    for (int i = 0; i < 20; i++) { ASSERT_TRUE(rec.put(Raw{0x33, junk, 2})); }
    ASSERT_TRUE(rec.commit());

    etl::string<128> out;
    ASSERT_TRUE(reader.pull(out));
    ASSERT_EQ(out, etl::string<128>("... records lost: 1 ..."));
    ASSERT_FALSE(reader.pull(out));

    // All 10 chunks must be available again; an eleventh allocation must fail.
    for (int i = 0; i < 10; i++) {
        ASSERT_NE(buffer.create_chunk(), jetlog::NoChunk);
    }
    ASSERT_EQ(buffer.create_chunk(), jetlog::NoChunk);
}

TEST(JetlogBrokenRecords, UnknownTypeIsMarked) {
    jetlog::RingBuffer<4096> buffer{};
    BareReader reader{buffer};

    // A record built by hand, with an encoding byte no codec owns.
    jetlog::WireWriter<RawList> rec{buffer};
    ASSERT_TRUE(rec.open());

    uint32_t stamp = 0;
    uint8_t stamp_bytes[4];
    jetlog::store_le(stamp, stamp_bytes);
    ASSERT_TRUE(rec.put(Raw{jetlog::encoding::U32, stamp_bytes, 4}));
    ASSERT_TRUE(rec.put(Raw{jetlog::encoding::StrRefCompact, nullptr, 0}));

    uint8_t lvl = 0;
    ASSERT_TRUE(rec.put(Raw{jetlog::encoding::U8, &lvl, 1}));

    const char* fmt = "x={}";
    uint8_t pointer[sizeof(char*)];
    memcpy(pointer, &fmt, sizeof(char*));
    ASSERT_TRUE(rec.put(Raw{static_cast<uint8_t>(jetlog::encoding::StrRefCompact | 4),
                            pointer, sizeof(char*)}));

    uint8_t junk[2] = {1, 2};
    ASSERT_TRUE(rec.put(Raw{0x33, junk, 2}));
    ASSERT_TRUE(rec.commit());

    etl::string<128> out;
    ASSERT_TRUE(reader.pull(out));
    ASSERT_EQ(out, etl::string<128>("x=[UNKNOWN]"));
}

//
// Loss reporting and writer allocation failures.
//

TEST(JetlogLossReporting, ReportsOnlyNewLossesBeforeSurvivingRecords) {
    jetlog::RingBuffer<320, 32> buffer{};   // 10 chunks
    jetlog::Writer<Config> writer{buffer};
    BareReader reader{buffer};

    for (int i = 0; i < 30; i++) { writer.push("t", 0, "{}", i); }

    etl::string<128> out;
    ASSERT_TRUE(reader.pull(out));
    ASSERT_EQ(out, etl::string<128>("... records lost: 20 ..."));

    for (int i = 30; i < 35; i++) { writer.push("t", 0, "{}", i); }

    // Record 20 was taken out of the ring before the first report. It survives
    // the new overflow and comes next, even though more losses have arrived.
    out.clear();
    ASSERT_TRUE(reader.pull(out));
    ASSERT_EQ(out, etl::string<128>("20"));

    // Reports the five new losses, not the cumulative total of 25.
    out.clear();
    ASSERT_TRUE(reader.pull(out));
    ASSERT_EQ(out, etl::string<128>("... records lost: 5 ..."));

    // The new writes evicted records 21 through 25 while 20 was retained.
    // Reading continues with the oldest surviving record.
    out.clear();
    ASSERT_TRUE(reader.pull(out));
    ASSERT_EQ(out, etl::string<128>("26"));
}

TEST(JetlogLossReporting, RecordThatDoesNotFitCountsAsLost) {
    jetlog::RingBuffer<320, 32> buffer{};   // 10 chunks, 300 bytes of data
    jetlog::Writer<Config> writer{buffer};
    BareReader reader{buffer};

    etl::string<600> too_big;
    too_big.resize(500, 'x');

    ASSERT_FALSE(writer.push("t", 0, "{}", too_big));
    ASSERT_EQ(buffer.lost_count(), 1u);

    etl::string<128> out;
    ASSERT_TRUE(reader.pull(out));
    ASSERT_EQ(out, etl::string<128>("... records lost: 1 ..."));

    // Nothing else is left to read.
    out.clear();
    ASSERT_FALSE(reader.pull(out));
}

TEST(JetlogLossReporting, NoMemoryToEvenStartARecordCountsAsLost) {
    jetlog::RingBuffer<320, 32> buffer{};   // 10 chunks
    jetlog::Writer<Config> writer{buffer};

    // Every chunk is held by a record nobody published, so there is nothing to
    // evict and nothing to hand out.
    for (int i = 0; i < 10; i++) {
        ASSERT_NE(buffer.create_chunk(), jetlog::NoChunk);
    }

    ASSERT_FALSE(writer.push("t", 0, "anything"));
    ASSERT_EQ(buffer.lost_count(), 1u);
}

//
// Configuration.
//

TEST(JetlogConfig, DefaultAndOptionalCodecs) {
    using Bare = jetlog::Config<>;
    using WithFloat = jetlog::Config<jetlog::Codecs<jetlog::Flt>>;

    static_assert(Bare::codecs::template can_encode<int32_t>(),
        "core integer codec is enabled by default");
    static_assert(!Bare::codecs::template can_encode<float>(),
        "optional codecs are disabled by default");
    static_assert(WithFloat::codecs::template can_encode<float>(),
        "codec option enables float");

    SUCCEED();
}


//
// Check exact little-endian bytes: a round trip alone can pass when store and
// load both use the wrong byte order.
//

TEST(JetlogEndian, StoreLePutsTheLeastSignificantByteFirst) {
    uint8_t out[8];

    jetlog::store_le(static_cast<uint8_t>(0x01), out);
    ASSERT_EQ(memcmp(out, "\x01", 1), 0);

    jetlog::store_le(static_cast<uint16_t>(0x0102), out);
    ASSERT_EQ(memcmp(out, "\x02\x01", 2), 0);

    jetlog::store_le(static_cast<uint32_t>(0x01020304), out);
    ASSERT_EQ(memcmp(out, "\x04\x03\x02\x01", 4), 0);

    jetlog::store_le(static_cast<uint64_t>(0x0102030405060708), out);
    ASSERT_EQ(memcmp(out, "\x08\x07\x06\x05\x04\x03\x02\x01", 8), 0);
}

TEST(JetlogEndian, StoreLeKeepsTheTwosComplementOfNegatives) {
    uint8_t out[8];

    jetlog::store_le(static_cast<int8_t>(-2), out);
    ASSERT_EQ(memcmp(out, "\xFE", 1), 0);

    jetlog::store_le(static_cast<int16_t>(-2), out);
    ASSERT_EQ(memcmp(out, "\xFE\xFF", 2), 0);

    jetlog::store_le(static_cast<int32_t>(-2), out);
    ASSERT_EQ(memcmp(out, "\xFE\xFF\xFF\xFF", 4), 0);

    jetlog::store_le(static_cast<int64_t>(-2), out);
    ASSERT_EQ(memcmp(out, "\xFE\xFF\xFF\xFF\xFF\xFF\xFF\xFF", 8), 0);
}

TEST(JetlogEndian, StoreLeTouchesOnlyItsOwnBytes) {
    uint8_t out[12];
    memset(out, 0xAA, sizeof(out));

    jetlog::store_le(static_cast<uint32_t>(0), out + 4);

    ASSERT_EQ(memcmp(out, "\xAA\xAA\xAA\xAA", 4), 0);
    ASSERT_EQ(memcmp(out + 4, "\x00\x00\x00\x00", 4), 0);
    ASSERT_EQ(memcmp(out + 8, "\xAA\xAA\xAA\xAA", 4), 0);
}

TEST(JetlogEndian, LoadLeReadsTheLeastSignificantByteFirst) {
    const uint8_t bytes[] = {0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01};

    ASSERT_EQ(jetlog::load_le<uint8_t>(bytes), 0x08u);
    ASSERT_EQ(jetlog::load_le<uint16_t>(bytes), 0x0708u);
    ASSERT_EQ(jetlog::load_le<uint32_t>(bytes), 0x05060708u);
    ASSERT_EQ(jetlog::load_le<uint64_t>(bytes), 0x0102030405060708u);
}

TEST(JetlogEndian, LoadLeRestoresNegativesFromTheirTwosComplement) {
    const uint8_t bytes[] = {0xFE, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

    ASSERT_EQ(jetlog::load_le<int8_t>(bytes), -2);
    ASSERT_EQ(jetlog::load_le<int16_t>(bytes), -2);
    ASSERT_EQ(jetlog::load_le<int32_t>(bytes), -2);
    ASSERT_EQ(jetlog::load_le<int64_t>(bytes), -2);

    // The same bytes read unsigned are the top of the range.
    ASSERT_EQ(jetlog::load_le<uint8_t>(bytes), 0xFEu);
    ASSERT_EQ(jetlog::load_le<uint32_t>(bytes), 0xFFFFFFFEu);
}

TEST(JetlogEndian, ExtremesSurviveTheRoundTrip) {
    uint8_t out[8];

    auto both_ways = [&](auto value) {
        jetlog::store_le(value, out);
        ASSERT_EQ(jetlog::load_le<decltype(value)>(out), value);
    };

    both_ways(etl::numeric_limits<int8_t>::min());
    both_ways(etl::numeric_limits<int8_t>::max());
    both_ways(etl::numeric_limits<uint8_t>::max());
    both_ways(etl::numeric_limits<int16_t>::min());
    both_ways(etl::numeric_limits<int16_t>::max());
    both_ways(etl::numeric_limits<uint16_t>::max());
    both_ways(etl::numeric_limits<int32_t>::min());
    both_ways(etl::numeric_limits<int32_t>::max());
    both_ways(etl::numeric_limits<uint32_t>::max());
    both_ways(etl::numeric_limits<int64_t>::min());
    both_ways(etl::numeric_limits<int64_t>::max());
    both_ways(etl::numeric_limits<uint64_t>::max());
}
