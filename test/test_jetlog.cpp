#include <gtest/gtest.h>
#include "jetlog/jetlog.hpp"

TEST(JetlogTest, BasicPush) {
    jetlog::RingBuffer<10000> ringBuffer;
    jetlog::Writer<> logWriter(ringBuffer);
    jetlog::Reader<> logReader(ringBuffer);
    etl::string<100> output;

    logWriter.push("", jetlog::level::info, "Hello, {}!", "World");
    ASSERT_TRUE(logReader.pull(output));
    EXPECT_EQ(output, "I: Hello, World!");

    output.clear();
    logWriter.push("", jetlog::level::debug, "Debug message: {}", 123);
    ASSERT_TRUE(logReader.pull(output));
    EXPECT_EQ(output, "D: Debug message: 123");

    output.clear();
    logWriter.push("", jetlog::level::error, "Error message: {}", 456);
    ASSERT_TRUE(logReader.pull(output));
    EXPECT_EQ(output, "E: Error message: 456");
}

TEST(JetlogTest, SupportedArgTypes) {
    jetlog::RingBuffer<10000> ringBuffer;
    jetlog::Writer<> logWriter(ringBuffer);
    jetlog::Reader<> logReader(ringBuffer);
    etl::string<100> output;

    int8_t int8_val = -8;
    uint8_t uint8_val = 8;
    int16_t int16_val = -16;
    uint16_t uint16_val = 16;
    int32_t int32_val = -32;
    uint32_t uint32_val = 32;
    const char* str_val = "test";
    char* mutable_str_val = const_cast<char*>("mutable");
    std::string std_str = "std_str";
    etl::string<100> etl_str = "etl_str";

    logWriter.push("", jetlog::level::info, "Test values: {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}",
        int8_val, uint8_val, int16_val, uint16_val,
        int32_val, uint32_val, str_val, mutable_str_val,
        std_str, etl_str, "literal");
    ASSERT_TRUE(logReader.pull(output));
    EXPECT_EQ(output, "I: Test values: -8, 8, -16, 16, -32, 32, test, mutable, std_str, etl_str, literal");
}

class TimestampedWriter : public jetlog::Writer<> {
public:
    TimestampedWriter(jetlog::IRingBuffer& buf) : jetlog::Writer<>(buf) {}
    // Mock timestamp
    auto getTime() -> uint32_t override { return 12345; }
};

TEST(JetlogTest, TimestampAndTag) {
    jetlog::RingBuffer<10000> ringBuffer;
    TimestampedWriter logWriter(ringBuffer);
    jetlog::Reader<> logReader(ringBuffer);
    etl::string<100> output;

    logWriter.push("TestTag", jetlog::level::info, "Message with timestamp and tag");
    ASSERT_TRUE(logReader.pull(output));
    EXPECT_EQ(output, "I (12345) TestTag: Message with timestamp and tag");
}


int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
