#pragma once

#include <etl/algorithm.h>
#include <etl/array.h>
#include <etl/atomic.h>
#include <etl/limits.h>
#include <etl/vector.h>

#include <stddef.h>
#include <stdint.h>

namespace jetlog {

class IRingBuffer {
public:
    virtual auto writeRecord(const etl::ivector<uint8_t>& data) -> bool = 0;
    virtual auto writeRecord(const uint8_t* data, size_t size) -> bool = 0;
    virtual auto readRecord(etl::ivector<uint8_t>& data) -> bool = 0;
    virtual auto reset(bool unlock_only = false) -> void = 0;
};

template <size_t BufferSize>
class RingBuffer : public IRingBuffer {
public:
    struct RecordHeader {
        uint16_t size;
    };

    auto writeRecord(const etl::ivector<uint8_t>& data) -> bool override {
        return writeRecord(data.data(), data.size());
    }

    auto writeRecord(const uint8_t* data, size_t size) -> bool override {
        size_t record_size{sizeof(RecordHeader) + size};

        writers_count.fetch_add(1, etl::memory_order_relaxed);

        auto allocation_index = allocateSpace(record_size);
        bool allocation_success = (allocation_index != ALLOCATION_FAILED);

        if (allocation_success) {
            setRecordHeader(allocation_index, { static_cast<uint16_t>(size) });
            writeBuffer((allocation_index + sizeof(RecordHeader)) % BufferSize, data, size);
        }

        // Independent on write success, try to update head_idx if no more
        // writers are locking buffer.

        auto current_head = head_idx.load(etl::memory_order_relaxed);
        auto current_upcoming = upcoming_idx.load(etl::memory_order_relaxed);
        auto current_writers_count = writers_count.fetch_sub(1, etl::memory_order_relaxed);

        if (current_writers_count == 1) {
            if (current_head != current_upcoming) {
                // If update fails => another writer already did update
                //
                // In theory, current_upcoming can become outdated here, but
                // that will be fixed on next write.
                head_idx.compare_exchange_strong(current_head, current_upcoming,
                    etl::memory_order_release, etl::memory_order_relaxed);
            }
        }

        return allocation_success;
    }

    auto readRecord(etl::ivector<uint8_t>& data) -> bool override {
        while (true) {
            size_t tail{tail_idx.load(etl::memory_order_relaxed)};
            // Here we use ACQUIRE to sync with writer thread (it updates
            // head_idx on publish).
            size_t head{head_idx.load(etl::memory_order_acquire)};

            if (tail == head) {
                data.clear();
                return false;
            }

            RecordHeader header{};
            getRecordHeader(tail, header);
            size_t size{header.size};

            if (tail_idx.load(etl::memory_order_relaxed) != tail) {
                // If tail changed - header is invalid, need to retry.
                continue;
            }

            data.resize(size);
            size_t next_tail{(tail + sizeof(RecordHeader) + size) % BufferSize};

            readBuffer((tail + sizeof(RecordHeader)) % BufferSize, data.data(), size);

            if (tail_idx.compare_exchange_strong(tail, next_tail,
                // Here we use relaxed write, because reader has NO other write
                // operations to push. And atomics themselves are always ordered.
                etl::memory_order_relaxed, etl::memory_order_relaxed)) {
                return true;
            }
        }
    }

    //
    // Note, this is uncertain feature, to unlock buffer at global fuckup, like
    // watchdog reset. No ideas about real system demands. May be should be done
    // in a different way.
    //
    auto reset(bool unlock_only = false) -> void override {
        if (unlock_only) {
            writers_count = 0;
            upcoming_idx = head_idx.load();
            return;
        }

        writers_count = 0;
        tail_idx = 0;
        head_idx = 0;
        upcoming_idx = 0;
    }

private:
    static constexpr size_t ALLOCATION_FAILED = static_cast<size_t>(-1);

    // Allocate space for a record, returns the index to write at, or failure
    size_t allocateSpace(size_t required_size) {
        if (required_size > etl::numeric_limits<uint16_t>::max()) {
            return ALLOCATION_FAILED;
        }

        while (true) {
            size_t tail{tail_idx.load(etl::memory_order_relaxed)};
            size_t upcoming{upcoming_idx.load(etl::memory_order_relaxed)};
            // Here we use ACQUIRE to sync data for getRecordHeader
            size_t head{head_idx.load(etl::memory_order_acquire)};

            size_t space_available = upcoming >= tail
                ? BufferSize - upcoming + tail
                : tail - upcoming;

            size_t max_available = upcoming >= head
                ? BufferSize - upcoming + head
                : head - upcoming;

            // Check if we have enough space (after tail cleanup)
            // + 1 byte reserved, to distinguish empty from full
            if (required_size + 1 > max_available) {
                return ALLOCATION_FAILED;
            }

            // If current space less that needed - cut tail
            // + 1 byte reserved, to distinguish empty from full
            if (required_size + 1 > space_available) {
                RecordHeader header{};
                getRecordHeader(tail, header);
                // Here we can have invalid header, if tail_idx was updated.
                // But that's safe, because bad value will be ignored by CAS.
                size_t new_tail{(tail + sizeof(RecordHeader) + header.size) % BufferSize};

                tail_idx.compare_exchange_strong(tail, new_tail,
                    etl::memory_order_relaxed, etl::memory_order_relaxed);

                // Repeat from the beginning, to keep things simple
                continue;
            }

            // At this place we know that we have enough space.

            size_t new_upcoming{(upcoming + required_size) % BufferSize};

            if (!upcoming_idx.compare_exchange_strong(upcoming, new_upcoming,
                etl::memory_order_relaxed, etl::memory_order_relaxed)) {
                // Failed to update upcoming_idx, another writer changed it
                // => retry
                continue;
            }

            // Successfully allocated space
            return upcoming;
        }
    }

    inline void getRecordHeader(size_t index, RecordHeader& header) const {
        readBuffer(index, reinterpret_cast<uint8_t*>(&header), sizeof(RecordHeader));
    }

    inline void setRecordHeader(size_t index, const RecordHeader& header) {
        writeBuffer(index, reinterpret_cast<const uint8_t*>(&header), sizeof(RecordHeader));
    }

    inline void writeBuffer(size_t index, const uint8_t* data, size_t size) {
        if (index + size <= BufferSize) {
            etl::copy_n(data, size, &buffer[index]);
        } else {
            size_t first_part{BufferSize - index};
            etl::copy_n(data, first_part, &buffer[index]);
            etl::copy_n(data + first_part, size - first_part, &buffer[0]);
        }
    }

    inline void readBuffer(size_t index, uint8_t* data, size_t size) const {
        if (index + size <= BufferSize) {
            etl::copy_n(&buffer[index], size, data);
        } else {
            size_t first_part{BufferSize - index};
            etl::copy_n(&buffer[index], first_part, data);
            etl::copy_n(&buffer[0], size - first_part, data + first_part);
        }
    }

    etl::array<uint8_t, BufferSize> buffer{};
    etl::atomic<size_t> head_idx{0};       // Index visible to readers (published data)
    etl::atomic<size_t> upcoming_idx{0};   // Index for next allocation (pre-allocated data)
    etl::atomic<size_t> tail_idx{0};       // Index where reading starts from
    etl::atomic<size_t> writers_count{0};  // Number of active writers
};

} // namespace jetlog