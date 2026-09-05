#pragma once

#include <etl/atomic.h>
#include <etl/binary.h>
#include <etl/memory.h>

#include <stddef.h>
#include <stdint.h>

namespace jetlog {

//
// Chunk pool + ring of records.
//
// A record is a chain of chunks. The buffer allocates chunks, links them,
// publishes finished chains and hands them out one at a time.
//
// Writers are many (threads, tasks, interrupts) and never wait. The reader is
// single and low priority. On overflow the oldest records are evicted and
// counted.
//

using ChunkId = uint16_t;

constexpr ChunkId NoChunk = 0xFFFF;


class IRingBuffer {
public:
    // Starts a chain. NoChunk if no free chunk or evictable record is available.
    virtual auto create_chunk() -> ChunkId = 0;

    // Appends a chunk to the end of the chain. Same failure condition.
    virtual auto add_chunk(ChunkId id) -> ChunkId = 0;

    // Walks the chain. NoChunk means the chain ends here.
    virtual auto next_chunk(ChunkId id) const -> ChunkId = 0;

    // The chunk's bytes, without the link the buffer keeps for itself.
    virtual auto chunk_data(ChunkId id) -> uint8_t* = 0;
    virtual auto chunk_data_size() const -> size_t = 0;

    // Publishes the chain. Until then it is invisible to everyone else.
    virtual auto ring_push(ChunkId id) -> void = 0;

    // Takes the oldest record out of the ring - nobody else can see or evict
    // it after that. NoChunk if there is nothing to take.
    virtual auto ring_pop() -> ChunkId = 0;

    virtual auto release_chunks(ChunkId id) -> void = 0;

    // Counts a rejected record, such as a failed write or malformed header.
    // Evicted records are counted automatically by the buffer.
    virtual auto report_lost() -> void = 0;

    // Total losses since start: evicted and explicitly reported records.
    virtual auto lost_count() const -> uint32_t = 0;

protected:
    // Protected to prevent deletion through this non-owning interface.
    ~IRingBuffer() = default;
};


//
// BufferSize is the size of the chunk pool, bookkeeping adds about 10% on top.
//
// ChunkSize is the granularity of allocation: a trade-off between the tail
// wasted by short records and the number of records the same memory holds.
//
template <size_t BufferSize, size_t ChunkSize = 32>
class RingBuffer final : public IRingBuffer {
public:
    RingBuffer() {
        // All chunks are free. Bits above CHUNKS_COUNT stay zero forever, so
        // they are never handed out.
        for (size_t word = 0; word < BITMAP_WORDS; word++) {
            size_t rest = CHUNKS_COUNT - word * BITS_PER_WORD;
            free_bits[word].store(
                rest >= BITS_PER_WORD
                    ? ~uint32_t{0}
                    : static_cast<uint32_t>((uint32_t{1} << rest) - 1),
                etl::memory_order_relaxed);
        }

        // Generation of position 0 is 0, so a cell tagged with 1 reads as
        // "nothing published here yet".
        for (size_t i = 0; i < RING_SIZE; i++) {
            ring[i].store(CELL_GENERATION, etl::memory_order_relaxed);
        }
    }

    auto create_chunk() -> ChunkId override {
        return alloc();
    }

    auto add_chunk(ChunkId id) -> ChunkId override {
        auto fresh = alloc();
        if (fresh == NoChunk) { return NoChunk; }

        ChunkId last = id;
        while (chunks[last].next != NoChunk) { last = chunks[last].next; }
        chunks[last].next = fresh;

        return fresh;
    }

    auto next_chunk(ChunkId id) const -> ChunkId override {
        return chunks[id].next;
    }

    auto chunk_data(ChunkId id) -> uint8_t* override {
        return chunks[id].data;
    }

    auto chunk_data_size() const -> size_t override {
        return CHUNK_DATA_SIZE;
    }

    auto ring_push(ChunkId id) -> void override {
        auto pos = head.fetch_add(1, etl::memory_order_relaxed);

        // Release, to hand the record's content over to whoever picks it.
        ring[cell_of(pos)].store(
            static_cast<uint16_t>(generation_of(pos) | id),
            etl::memory_order_release);
    }

    auto ring_pop() -> ChunkId override {
        while (true) {
            auto pos = tail.load(etl::memory_order_relaxed);
            auto cell = ring[cell_of(pos)].load(etl::memory_order_acquire);

            // The cell is reused every RING_SIZE positions, and a stale
            // value carries the previous lap's generation bit. So a mismatch
            // means "not published (yet)" and no cleanup is needed after a
            // successful pick.
            if ((cell & CELL_GENERATION) != generation_of(pos)) { return NoChunk; }

            // The CAS decides the owner. It also validates the cell we read:
            // the cell can only be reused after tail moves past it.
            if (tail.compare_exchange_strong(pos, pos + 1,
                etl::memory_order_relaxed, etl::memory_order_relaxed)) {
                return static_cast<ChunkId>(cell & ~CELL_GENERATION);
            }
        }
    }

    auto release_chunks(ChunkId id) -> void override {
        ChunkId cur = id;

        while (cur != NoChunk) {
            // Read the link before giving the chunk away: once freed, it may
            // already belong to somebody else.
            ChunkId next = chunks[cur].next;

            free_bits[cur / BITS_PER_WORD].fetch_or(
                uint32_t{1} << (cur % BITS_PER_WORD), etl::memory_order_release);

            cur = next;
        }
    }

    auto report_lost() -> void override {
        lost.fetch_add(1, etl::memory_order_relaxed);
    }

    auto lost_count() const -> uint32_t override {
        return lost.load(etl::memory_order_relaxed);
    }

private:
    static constexpr size_t BITS_PER_WORD = 32;

    // Top bit of a ring cell marks the lap, the rest is the chunk index.
    static constexpr uint16_t CELL_GENERATION = 0x8000;

    // The link to the next chunk lives in the chunk, data is what is left.
    static constexpr size_t CHUNK_DATA_SIZE = ChunkSize - sizeof(ChunkId);

    static constexpr size_t CHUNKS_COUNT = BufferSize / ChunkSize;
    static constexpr size_t BITMAP_WORDS = (CHUNKS_COUNT + BITS_PER_WORD - 1) / BITS_PER_WORD;

    static constexpr auto round_up_pow2(size_t n) -> size_t {
        size_t p = 1;
        while (p < n) { p <<= 1; }
        return p;
    }

    static constexpr auto log2_of(size_t n) -> size_t {
        size_t bits = 0;
        while ((size_t{1} << bits) != n) { bits++; }
        return bits;
    }

    // The ring never holds more records than there are chunks, because every
    // record in it owns at least one chunk. Rounded up to a power of two, so
    // that the position counter stays consistent across its own 32 bit wrap.
    static constexpr size_t RING_SIZE = round_up_pow2(CHUNKS_COUNT);
    static constexpr size_t RING_MASK = RING_SIZE - 1;
    static constexpr size_t RING_BITS = log2_of(RING_SIZE);

    // A uint64_t or double is the largest indivisible value: 8 payload bytes
    // plus its 1 byte type tag. Every chunk also carries a 2 byte link. Use 16
    // bytes as the minimum to leave some headroom.
    static_assert(ChunkSize >= 16, "Chunk size must be at least 16 bytes");
    static_assert(ChunkSize % sizeof(ChunkId) == 0, "Chunk size must be even, or chunks get padded");
    static_assert(CHUNKS_COUNT >= 2, "Buffer is too small to hold anything");
    static_assert(CHUNKS_COUNT <= CELL_GENERATION, "Buffer is too big, chunk index must fit 15 bits");
    static_assert((RING_SIZE & RING_MASK) == 0, "Ring size must be a power of two");
    static_assert(RING_SIZE >= CHUNKS_COUNT, "Ring must hold every chunk");

    struct Chunk {
        ChunkId next;
        uint8_t data[CHUNK_DATA_SIZE];
    };

    static_assert(sizeof(Chunk) == ChunkSize, "Chunk must not be padded");

    static auto cell_of(uint32_t pos) -> size_t { return pos & RING_MASK; }
    static auto generation_of(uint32_t pos) -> uint16_t {
        return ((pos >> RING_BITS) & 1U) ? CELL_GENERATION : uint16_t{0};
    }

    // Hands out a chunk linked to nothing. Linking it into a chain is the
    // caller's business.
    auto alloc() -> ChunkId {
        auto id = take_free_chunk();

        // Reclaim space by evicting the oldest complete records until a chunk
        // can be claimed or no victim is available.
        while (id == NoChunk) {
            auto victim = ring_pop();

            // No victim is available. Chunks held by active writers or the
            // reader cannot be reclaimed here.
            if (victim == NoChunk) { return NoChunk; }

            release_chunks(victim);
            lost.fetch_add(1, etl::memory_order_relaxed);

            id = take_free_chunk();
        }

        // The chain is private to its writer until published, so plain writes
        // are enough here - the release on ring_push() covers them.
        chunks[id].next = NoChunk;

        // Unwritten bytes must read as padding; zero them once here instead
        // of filling each unused tail when the writer moves to another chunk.
        etl::mem_set(chunks[id].data, CHUNK_DATA_SIZE, uint8_t{0});

        return id;
    }

    // Grabs any free chunk. No CAS loop: clearing a bit that is already clear
    // is harmless, and the fetched word tells whether we won the bit or not.
    auto take_free_chunk() -> ChunkId {
        for (size_t word = 0; word < BITMAP_WORDS; word++) {
            auto bits = free_bits[word].load(etl::memory_order_relaxed);

            while (bits != 0) {
                uint32_t bit = bits & (~bits + 1);
                auto was = free_bits[word].fetch_and(~bit, etl::memory_order_acquire);

                if (was & bit) {
                    return static_cast<ChunkId>(
                        word * BITS_PER_WORD + etl::count_trailing_zeros(bit));
                }

                // Another writer claimed this bit. Retry the other free bits
                // from the returned snapshot.
                bits = was & ~bit;
            }
        }

        return NoChunk;
    }

    Chunk chunks[CHUNKS_COUNT]{};
    etl::atomic<uint32_t> free_bits[BITMAP_WORDS];   // set bit == free chunk
    etl::atomic<uint16_t> ring[RING_SIZE];          // generation bit + first chunk

    etl::atomic<uint32_t> head{0};   // next ring position to reserve
    etl::atomic<uint32_t> tail{0};   // oldest ring position, taken by CAS
    etl::atomic<uint32_t> lost{0};   // records that never reached a reader
};

} // namespace jetlog
