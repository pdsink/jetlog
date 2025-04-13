#include <gtest/gtest.h>

#include "jetlog/private/typelists.hpp"
#include "jetlog/private/types.hpp"
#include "jetlog/jetlog.hpp"

#include <etl/to_arithmetic.h>

using namespace jetlog;
using Encoders = jetlog::ParamEncoders_64_And_Double;

// I8
TEST(TypesTest, I8EncodeDecode) {
    etl::vector<uint8_t, 100> buffer{};
    const int8_t test_val = 42;

    Encoders::write(test_val, buffer);
    EXPECT_EQ(IDecoder::readHeader(buffer, 0).typeId, static_cast<uint8_t>(DataType::I8));

    etl::string<100> result;
    DecoderI8 decoder(buffer, 0);
    decoder.format(result);

    EXPECT_EQ(result, "42");
}

TEST(TypesTest, CharEncodeDecode) {
    etl::vector<uint8_t, 100> buffer{};
    const char test_val = 42;

    Encoders::write(test_val, buffer);
    EXPECT_EQ(IDecoder::readHeader(buffer, 0).typeId, static_cast<uint8_t>(DataType::I8));

    etl::string<100> result;
    DecoderI8 decoder(buffer, 0);
    decoder.format(result);

    EXPECT_EQ(result, "42");
}

// U8
TEST(TypesTest, U8EncodeDecode) {
    etl::vector<uint8_t, 100> buffer{};
    const uint8_t test_val = 200;

    Encoders::write(test_val, buffer);
    EXPECT_EQ(IDecoder::readHeader(buffer, 0).typeId, static_cast<uint8_t>(DataType::U8));

    etl::string<100> result;
    DecoderU8 decoder(buffer, 0);
    decoder.format(result);

    EXPECT_EQ(result, "200");
}

TEST(TypesTest, UCharEncodeDecode) {
    etl::vector<uint8_t, 100> buffer{};
    const unsigned char test_val = 200;

    Encoders::write(test_val, buffer);
    EXPECT_EQ(IDecoder::readHeader(buffer, 0).typeId, static_cast<uint8_t>(DataType::U8));

    etl::string<100> result;
    DecoderU8 decoder(buffer, 0);
    decoder.format(result);

    EXPECT_EQ(result, "200");
}

// I16
TEST(TypesTest, I16EncodeDecode) {
    etl::vector<uint8_t, 100> buffer{};
    const int16_t test_val = 12345;

    Encoders::write(test_val, buffer);
    EXPECT_EQ(IDecoder::readHeader(buffer, 0).typeId, static_cast<uint8_t>(DataType::I16));

    etl::string<100> result;
    DecoderI16 decoder(buffer, 0);
    decoder.format(result);

    EXPECT_EQ(result, "12345");
}

TEST(TypesTest, ShortEncodeDecode) {
    etl::vector<uint8_t, 100> buffer{};
    const short test_val = 12345;

    Encoders::write(test_val, buffer);
    EXPECT_EQ(IDecoder::readHeader(buffer, 0).typeId, static_cast<uint8_t>(DataType::I16));

    etl::string<100> result;
    DecoderI16 decoder(buffer, 0);
    decoder.format(result);

    EXPECT_EQ(result, "12345");
}

// U16
TEST(TypesTest, U16EncodeDecode) {
    etl::vector<uint8_t, 100> buffer{};
    const uint16_t test_val = 65000;

    Encoders::write(test_val, buffer);
    EXPECT_EQ(IDecoder::readHeader(buffer, 0).typeId, static_cast<uint8_t>(DataType::U16));

    etl::string<100> result;
    DecoderU16 decoder(buffer, 0);
    decoder.format(result);

    EXPECT_EQ(result, "65000");
}

TEST(TypesTest, UShortEncodeDecode) {
    etl::vector<uint8_t, 100> buffer{};
    const unsigned short test_val = 65000;

    Encoders::write(test_val, buffer);
    EXPECT_EQ(IDecoder::readHeader(buffer, 0).typeId, static_cast<uint8_t>(DataType::U16));

    etl::string<100> result;
    DecoderU16 decoder(buffer, 0);
    decoder.format(result);

    EXPECT_EQ(result, "65000");
}

// I32
TEST(TypesTest, I32EncodeDecode) {
    etl::vector<uint8_t, 100> buffer{};
    const int32_t test_val = 123456789;

    Encoders::write(test_val, buffer);
    EXPECT_EQ(IDecoder::readHeader(buffer, 0).typeId, static_cast<uint8_t>(DataType::I32));

    etl::string<100> result;
    DecoderI32 decoder(buffer, 0);
    decoder.format(result);

    EXPECT_EQ(result, "123456789");
}

TEST(TypesTest, IntEncodeDecode) {
    etl::vector<uint8_t, 100> buffer{};
    const int test_val = 123456789;

    Encoders::write(test_val, buffer);
    EXPECT_EQ(IDecoder::readHeader(buffer, 0).typeId, static_cast<uint8_t>(DataType::I32));

    etl::string<100> result;
    DecoderI32 decoder(buffer, 0);
    decoder.format(result);

    EXPECT_EQ(result, "123456789");
}

// U32
TEST(TypesTest, U32EncodeDecode) {
    etl::vector<uint8_t, 100> buffer{};
    const uint32_t test_val = 4000000000U;

    Encoders::write(test_val, buffer);
    EXPECT_EQ(IDecoder::readHeader(buffer, 0).typeId, static_cast<uint8_t>(DataType::U32));

    etl::string<100> result;
    DecoderU32 decoder(buffer, 0);
    decoder.format(result);

    EXPECT_EQ(result, "4000000000");
}

TEST(TypesTest, UIntEncodeDecode) {
    etl::vector<uint8_t, 100> buffer{};
    const unsigned int test_val = 4000000000U;

    Encoders::write(test_val, buffer);
    EXPECT_EQ(IDecoder::readHeader(buffer, 0).typeId, static_cast<uint8_t>(DataType::U32));

    etl::string<100> result;
    DecoderU32 decoder(buffer, 0);
    decoder.format(result);

    EXPECT_EQ(result, "4000000000");
}

// I64
TEST(TypesTest, I64EncodeDecode) {
    etl::vector<uint8_t, 100> buffer{};
    const int64_t test_val = 1234567890123456789LL;

    Encoders::write(test_val, buffer);
    EXPECT_EQ(IDecoder::readHeader(buffer, 0).typeId, static_cast<uint8_t>(DataType::I64));

    etl::string<100> result;
    DecoderI64 decoder(buffer, 0);
    decoder.format(result);

    EXPECT_EQ(result, "1234567890123456789");
}

// U64
TEST(TypesTest, U64EncodeDecode) {
    etl::vector<uint8_t, 100> buffer{};
    const uint64_t test_val = 18000000000000000000ULL;

    Encoders::write(test_val, buffer);
    EXPECT_EQ(IDecoder::readHeader(buffer, 0).typeId, static_cast<uint8_t>(DataType::U64));

    etl::string<100> result;
    DecoderU64 decoder(buffer, 0);
    decoder.format(result);

    EXPECT_EQ(result, "18000000000000000000");
}

// Float
TEST(TypesTest, FloatEncodeDecode) {
    etl::vector<uint8_t, 100> buffer{};
    const float test_val = 123.456f;

    Encoders::write(test_val, buffer);
    EXPECT_EQ(IDecoder::readHeader(buffer, 0).typeId, static_cast<uint8_t>(DataType::Flt));

    etl::string<100> result;
    DecoderFlt decoder(buffer, 0);
    decoder.format(result);

    float decoded_val = etl::to_arithmetic<float>(result);
    EXPECT_NEAR(decoded_val, test_val, 0.0001f);
}

// Double
TEST(TypesTest, DoubleEncodeDecode) {
    etl::vector<uint8_t, 100> buffer{};
    const double test_val = 123.456789;

    Encoders::write(test_val, buffer);
    EXPECT_EQ(IDecoder::readHeader(buffer, 0).typeId, static_cast<uint8_t>(DataType::Dbl));

    etl::string<100> result;
    DecoderDbl decoder(buffer, 0);
    decoder.format(result);

    double decoded_val = etl::to_arithmetic<double>(result);
    EXPECT_NEAR(decoded_val, test_val, 0.000001);
}

// String
TEST(TypesTest, StdStringEncodeDecode) {
    etl::vector<uint8_t, 100> buffer{};
    const std::string test_str = "Hello, World!";

    Encoders::write(test_str, buffer);
    EXPECT_EQ(IDecoder::readHeader(buffer, 0).typeId, static_cast<uint8_t>(DataType::Str));

    etl::string<100> result;
    DecoderStr decoder(buffer, 0);
    decoder.format(result);

    EXPECT_EQ(result, test_str.c_str());
}

// C-strings
TEST(TypesTest, CStringEncodeDecode) {
    etl::vector<uint8_t, 100> buffer{};
    const char* test_str = "Hello, World!";

    Encoders::write(test_str, buffer);
    EXPECT_EQ(IDecoder::readHeader(buffer, 0).typeId, static_cast<uint8_t>(DataType::Str));

    etl::string<100> result;
    DecoderStr decoder(buffer, 0);
    decoder.format(result);

    EXPECT_EQ(result, test_str);
}

TEST(TypesTest, CharPtrEncodeDecode) {
    etl::vector<uint8_t, 100> buffer{};
    char test_str[] = "Hello, World!";

    Encoders::write(jetlog::decayLiteralArg(test_str), buffer);
    EXPECT_EQ(IDecoder::readHeader(buffer, 0).typeId, static_cast<uint8_t>(DataType::Str));

    etl::string<100> result;
    DecoderStr decoder(buffer, 0);
    decoder.format(result);

    EXPECT_EQ(result, test_str);
}

/* This is not actual, because we force literal types decay in push.
TEST(TypesTest, StringLiteralTest) {
    etl::vector<uint8_t, 100> buffer{};

    EncoderCString<decltype("Hello, World!")>::write("Hello, World!", buffer);

    std::string result;
    DecoderStr decoder(buffer, 0);
    decoder.format(result);

    EXPECT_EQ(result, "Hello, World!");
}
*/