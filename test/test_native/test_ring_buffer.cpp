#include <gtest/gtest.h>
#include "jetlog/private/ring_buffer.hpp"

using jetlog::ChunkId;
using jetlog::NoChunk;

namespace {

auto chain_length(jetlog::IRingBuffer& buf, ChunkId first) -> int {
    int count = 0;
    for (ChunkId id = first; id != NoChunk; id = buf.next_chunk(id)) { count++; }
    return count;
}

} // namespace

TEST(RingBuffer, UsableBytesAreLessThanTheChunkSize) {
    jetlog::RingBuffer<1024, 32> buffer{};
    ASSERT_EQ(buffer.chunk_data_size(), 30u);

    jetlog::RingBuffer<1024, 64> wide{};
    ASSERT_EQ(wide.chunk_data_size(), 62u);
}

TEST(RingBuffer, EmptyRingGivesNothing) {
    jetlog::RingBuffer<1024> buffer{};
    ASSERT_EQ(buffer.ring_pop(), NoChunk);
}

TEST(RingBuffer, ChainKeepsOrderAndData) {
    jetlog::RingBuffer<1024> buffer{};

    ChunkId first = buffer.create_chunk();
    ASSERT_NE(first, NoChunk);
    buffer.chunk_data(first)[0] = 1;

    ChunkId second = buffer.add_chunk(first);
    ASSERT_NE(second, NoChunk);
    buffer.chunk_data(second)[0] = 2;

    ChunkId third = buffer.add_chunk(first);
    ASSERT_NE(third, NoChunk);
    buffer.chunk_data(third)[0] = 3;

    ASSERT_EQ(chain_length(buffer, first), 3);
    ASSERT_EQ(buffer.next_chunk(first), second);
    ASSERT_EQ(buffer.next_chunk(second), third);
    ASSERT_EQ(buffer.next_chunk(third), NoChunk);
    ASSERT_EQ(buffer.chunk_data(first)[0], 1);
    ASSERT_EQ(buffer.chunk_data(second)[0], 2);
    ASSERT_EQ(buffer.chunk_data(third)[0], 3);
}

TEST(RingBuffer, PublishedRecordComesBackWhole) {
    jetlog::RingBuffer<1024> buffer{};

    ChunkId first = buffer.create_chunk();
    buffer.chunk_data(first)[0] = 42;
    buffer.add_chunk(first);
    buffer.ring_push(first);

    ChunkId taken = buffer.ring_pop();
    ASSERT_EQ(taken, first);
    ASSERT_EQ(buffer.chunk_data(taken)[0], 42);
    ASSERT_EQ(chain_length(buffer, taken), 2);

    buffer.release_chunks(taken);
    ASSERT_EQ(buffer.ring_pop(), NoChunk);
}

TEST(RingBuffer, UnpublishedRecordIsInvisible) {
    jetlog::RingBuffer<1024> buffer{};

    ChunkId first = buffer.create_chunk();
    ASSERT_NE(first, NoChunk);
    ASSERT_EQ(buffer.ring_pop(), NoChunk);

    buffer.ring_push(first);
    ASSERT_EQ(buffer.ring_pop(), first);
}

TEST(RingBuffer, ReleasedChunksComeBackToThePool) {
    jetlog::RingBuffer<320, 32> buffer{};   // 10 chunks

    ChunkId first = buffer.create_chunk();
    for (int i = 1; i < 10; i++) { ASSERT_NE(buffer.add_chunk(first), NoChunk); }

    // The pool is empty and the ring holds nothing to evict, so neither way of
    // asking for a chunk gets one.
    ASSERT_EQ(buffer.create_chunk(), NoChunk);
    ASSERT_EQ(buffer.add_chunk(first), NoChunk);

    buffer.release_chunks(first);

    for (int i = 0; i < 10; i++) { ASSERT_NE(buffer.create_chunk(), NoChunk); }
    ASSERT_EQ(buffer.create_chunk(), NoChunk);
}

TEST(RingBuffer, OverflowEvictsOldestAndCounts) {
    jetlog::RingBuffer<320, 32> buffer{};   // 10 chunks

    for (int i = 0; i < 25; i++) {
        ChunkId id = buffer.create_chunk();
        ASSERT_NE(id, NoChunk);
        buffer.chunk_data(id)[0] = static_cast<uint8_t>(i);
        buffer.ring_push(id);
    }

    ASSERT_EQ(buffer.lost_count(), 15u);

    // What is left is the newest, in order.
    for (int i = 15; i < 25; i++) {
        ChunkId id = buffer.ring_pop();
        ASSERT_NE(id, NoChunk);
        ASSERT_EQ(buffer.chunk_data(id)[0], static_cast<uint8_t>(i));
        buffer.release_chunks(id);
    }

    ASSERT_EQ(buffer.ring_pop(), NoChunk);
}

TEST(RingBuffer, EvictingAVictimFreesItsWholeChain) {
    jetlog::RingBuffer<320, 32> buffer{};   // 10 chunks

    // Mark every chunk to detect accidental reuse of a surviving record's data.
    //
    // Three 3-chunk records leave one free chunk. The fourth record needs one
    // eviction; freeing only the victim's head would require a second eviction.
    for (uint8_t mark = 1; mark <= 4; mark++) {
        ChunkId first = buffer.create_chunk();
        ASSERT_NE(first, NoChunk);
        buffer.chunk_data(first)[0] = mark;

        for (int i = 1; i < 3; i++) {
            ChunkId next = buffer.add_chunk(first);
            ASSERT_NE(next, NoChunk);
            buffer.chunk_data(next)[0] = mark;
        }

        buffer.ring_push(first);
    }

    ASSERT_EQ(buffer.lost_count(), 1u);

    // The two records nobody touched, then the new one, whole and in order.
    for (uint8_t mark = 2; mark <= 4; mark++) {
        ChunkId id = buffer.ring_pop();
        ASSERT_NE(id, NoChunk);
        ASSERT_EQ(chain_length(buffer, id), 3);

        for (ChunkId at = id; at != NoChunk; at = buffer.next_chunk(at)) {
            ASSERT_EQ(buffer.chunk_data(at)[0], mark);
        }

        buffer.release_chunks(id);
    }

    ASSERT_EQ(buffer.ring_pop(), NoChunk);
}

TEST(RingBuffer, ReportedLossesAndEvictionsShareTheCount) {
    jetlog::RingBuffer<320, 32> buffer{};   // 10 chunks

    // A record a writer never managed to build is reported by the writer.
    buffer.report_lost();
    buffer.report_lost();
    ASSERT_EQ(buffer.lost_count(), 2u);

    // Two records more than the pool holds, so two more are evicted.
    for (int i = 0; i < 12; i++) {
        ChunkId id = buffer.create_chunk();
        ASSERT_NE(id, NoChunk);
        buffer.ring_push(id);
    }

    ASSERT_EQ(buffer.lost_count(), 4u);
}

TEST(RingBuffer, RingKeepsOrderAfterItWrapsAround) {
    jetlog::RingBuffer<128, 32> buffer{};   // 4 chunks and 4 ring cells

    // Three complete laps exercise both generation bits and the return to the
    // initial one: 0, 1, 0.
    for (int lap = 0; lap < 3; lap++) {
        for (int i = 0; i < 4; i++) {
            ChunkId id = buffer.create_chunk();
            ASSERT_NE(id, NoChunk);
            buffer.chunk_data(id)[0] = static_cast<uint8_t>(lap * 4 + i);
            buffer.ring_push(id);
        }

        for (int i = 0; i < 4; i++) {
            ChunkId id = buffer.ring_pop();
            ASSERT_NE(id, NoChunk);
            ASSERT_EQ(buffer.chunk_data(id)[0], static_cast<uint8_t>(lap * 4 + i));
            buffer.release_chunks(id);
        }

        ASSERT_EQ(buffer.ring_pop(), NoChunk);
    }

    ASSERT_EQ(buffer.lost_count(), 0u);
}
