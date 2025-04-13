#include <gtest/gtest.h>
#include "jetlog/private/ring_buffer.hpp"

TEST(RingBufferTest, WriteAndReadSingleRecord) {
    jetlog::RingBuffer<1024> buffer{};
    etl::vector<uint8_t, 100> data(13, 0);
    etl::vector<uint8_t, 100> readData{};

    ASSERT_TRUE(buffer.writeRecord(data));
    ASSERT_TRUE(buffer.readRecord(readData));
    ASSERT_EQ(readData, data);
}

TEST(RingBufferTest, OverflowOnRecordHeader) {
    constexpr size_t bufferSize = 32;
    jetlog::RingBuffer<bufferSize> buffer{};

    etl::vector<uint8_t, 100> data1(6, 0);
    etl::vector<uint8_t, 100> data2(21, 1);
    etl::vector<uint8_t, 100> data3(6, 2);
    etl::vector<uint8_t, 100> readData{};

    ASSERT_TRUE(buffer.writeRecord(data1));
    ASSERT_TRUE(buffer.writeRecord(data2));
    ASSERT_TRUE(buffer.writeRecord(data3));

    ASSERT_TRUE(buffer.readRecord(readData));
    ASSERT_EQ(readData, data2);

    ASSERT_TRUE(buffer.readRecord(readData));
    ASSERT_EQ(readData, data3);
}

TEST(RingBufferTest, OverflowOnRecordData) {
    constexpr size_t bufferSize = 32;
    jetlog::RingBuffer<bufferSize> buffer{};

    etl::vector<uint8_t, 100> data1(6, 0);
    etl::vector<uint8_t, 100> data2(19, 1);
    etl::vector<uint8_t, 100> data3(6, 2);
    etl::vector<uint8_t, 100> readData{};

    ASSERT_TRUE(buffer.writeRecord(data1));
    ASSERT_TRUE(buffer.writeRecord(data2));
    ASSERT_TRUE(buffer.writeRecord(data3));

    ASSERT_TRUE(buffer.readRecord(readData));
    ASSERT_EQ(readData, data2);

    ASSERT_TRUE(buffer.readRecord(readData));
    ASSERT_EQ(readData, data3);
}

TEST(RingBufferTest, ReadFromEmptyBuffer) {
    jetlog::RingBuffer<1024> buffer{};
    etl::vector<uint8_t, 100> readData{};
    ASSERT_FALSE(buffer.readRecord(readData));
    ASSERT_TRUE(readData.empty());
}
