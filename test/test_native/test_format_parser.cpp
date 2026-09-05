#include <gtest/gtest.h>

#include <cstdint>
#include <string>

#include "jetlog/jetlog.hpp"

using Config = jetlog::Config<>;

class HeadlessReader : public jetlog::Reader<Config> {
public:
    explicit HeadlessReader(jetlog::IRingBuffer& buf) : jetlog::Reader<Config>(buf) {}

    void writeLogHeader(etl::istring&, uint32_t, const etl::string_view&, uint8_t) override {}
};

template <typename... Args>
std::string toString(const char* fmt, Args&&... args) {
    jetlog::RingBuffer<10000> ringBuffer;
    jetlog::Writer<Config> logWriter(ringBuffer);
    HeadlessReader logReader(ringBuffer);
    etl::string<100> output;

    // fmt is a pointer, so mark its lifetime explicitly; pull() reads it before
    // this helper returns.
    logWriter.push("", jetlog::level::info, jetlog::static_str(fmt), args...);
    logReader.pull(output);

    return output.c_str();
}

TEST(FormatParserTest, PlaceholderLength) {
    using jetlog::FormatParser;

    // Basic formats
    EXPECT_EQ(FormatParser::get_placeholder_length("{}", 0), 2u);
    EXPECT_EQ(FormatParser::get_placeholder_length("{:d}", 0), 4u);
    EXPECT_EQ(FormatParser::get_placeholder_length("{:x}", 0), 4u);
    EXPECT_EQ(FormatParser::get_placeholder_length("{:X}", 0), 4u);
    EXPECT_EQ(FormatParser::get_placeholder_length("{:b}", 0), 4u);

    // With prefix
    EXPECT_EQ(FormatParser::get_placeholder_length("{:#x}", 0), 5u);
    EXPECT_EQ(FormatParser::get_placeholder_length("{:#b}", 0), 5u);

    // With padding
    EXPECT_EQ(FormatParser::get_placeholder_length("{:04x}", 0), 6u);
    EXPECT_EQ(FormatParser::get_placeholder_length("{:4d}", 0), 5u);

    // With trailing content
    EXPECT_EQ(FormatParser::get_placeholder_length("{:x} 123", 0), 4u);
    EXPECT_EQ(FormatParser::get_placeholder_length("{:x}ABC", 0), 4u);

    // Invalid formats
    EXPECT_EQ(FormatParser::get_placeholder_length("{:}", 0), 0u);
    EXPECT_EQ(FormatParser::get_placeholder_length("{:", 0), 0u);
    EXPECT_EQ(FormatParser::get_placeholder_length("{:z}", 0), 0u);

    // Locate placeholders at nonzero offsets in mixed text.
    etl::string<100> pattern = "{} {:x} abc {:X} {:015X}";
    EXPECT_EQ(FormatParser::get_placeholder_length(pattern, 0), 2u);   // {}
    EXPECT_EQ(FormatParser::get_placeholder_length(pattern, 3), 4u);   // {:x}
    EXPECT_EQ(FormatParser::get_placeholder_length(pattern, 12), 4u);  // {:X}
    EXPECT_EQ(FormatParser::get_placeholder_length(pattern, 17), 7u);  // {:015X}
}

TEST(FormatParserTest, DecimalFormat) {
    EXPECT_EQ(toString("{}", 123), "123");
    EXPECT_EQ(toString("{:d}", 123), "123");
    EXPECT_EQ(toString("{}", -123), "-123");
    EXPECT_EQ(toString("{:d}", -123), "-123");
    EXPECT_EQ(toString("{}", 0), "0");
}

TEST(FormatParserTest, HexFormat) {
    EXPECT_EQ(toString("{:x}", 255), "ff");
    EXPECT_EQ(toString("{:X}", 255), "FF");
    EXPECT_EQ(toString("{:#x}", 255), "0xff");
    EXPECT_EQ(toString("{:#X}", 255), "0XFF");
    EXPECT_EQ(toString("{:x}", 0), "0");
}

TEST(FormatParserTest, BinaryFormat) {
    EXPECT_EQ(toString("{:b}", 42), "101010");
    EXPECT_EQ(toString("{:#b}", 42), "0b101010");
    EXPECT_EQ(toString("{:b}", 0), "0");
}

TEST(FormatParserTest, WidthAndPadding) {
    // Zero padding
    EXPECT_EQ(toString("{:04x}", 255), "00ff");
    EXPECT_EQ(toString("{:04X}", 255), "00FF");
    EXPECT_EQ(toString("{:08b}", 5), "00000101");
    EXPECT_EQ(toString("{:03d}", 7), "007");

    // Width without padding
    EXPECT_EQ(toString("{:4d}", 42), "  42");
    EXPECT_EQ(toString("{:4d}", -42), " -42");
}

TEST(FormatParserTest, EdgeCases) {
    // Max/min values
    EXPECT_EQ(toString("{}", INT32_MAX), "2147483647");
    EXPECT_EQ(toString("{}", INT32_MIN), "-2147483648");

    // Multiple format specifiers
    EXPECT_EQ(toString("{} {:x} {:X}", 123, 255, 255), "123 ff FF");
}

TEST(FormatParserTest, InvalidFormats) {
    // Invalid placeholders remain literal text.
    EXPECT_EQ(toString("{:z}", 42), "{:z}");  // Invalid type
    EXPECT_EQ(toString("{:", 42), "{:");    // Incomplete format
    EXPECT_EQ(toString("{:0}", 42), "{:0}");  // Missing type
}
