#pragma once

#include "ring_buffer.hpp"
#include "codecs.hpp"

#include <stddef.h>
#include <stdint.h>

namespace jetlog {

namespace encoding {

// Fills the gap left by a value that did not fit a chunk tail, and the tail of
// a record's last chunk. Skipped by the reader.
constexpr uint8_t SkipByte = 0x00;

} // namespace encoding

enum class ReadStatus {
    Ok,           // printed
    EndOfRecord,  // the record has no more values
    Broken        // bytes no codec owns, or a value cut short
};

//
// A record is a chain of chunks holding values one after another. A value is an
// encoding byte followed by a codec-defined payload. Values too long for one
// chunk use multiple fragments, each with its own encoding byte.
//

//
// Writes values into buffer memory, without a record buffer of its own.
//
// Allocation failure releases the chain immediately; further put()/commit()
// calls return false until a new record is opened.
//
template <typename Codecs>
class WireWriter {
public:
    explicit WireWriter(IRingBuffer& buf) : buffer{buf} {}

    auto open() -> bool {
        first = buffer.create_chunk();
        if (first == NoChunk) { return false; }

        current = first;
        pos = 0;

        return true;
    }

    //
    // Hands the codec what is left of the chunk until it says it is done. A
    // codec that took nothing needs more room than the tail has: the tail
    // becomes padding and the value goes on in the next chunk.
    //
    template <typename T>
    auto put(const T& value) -> bool {
        if (first == NoChunk) { return false; }

        EncoderState state{};

        while (!state.done) {
            state = Codecs::encode(ByteSpan(buffer.chunk_data(current) + pos, available()),
                value, state.input_offset);

            if (state.produced_bytes == 0) {
                // Nothing fits into an empty chunk either - the value is larger
                // than a chunk can ever hold.
                if (pos == 0) { discard(); return false; }

                // The tail stays as the buffer gave it, zeroed, and the reader
                // skips it.
                if (!grow()) { return false; }

                continue;
            }

            pos += state.produced_bytes;
        }

        return true;
    }

    auto commit() -> bool {
        if (first == NoChunk) { return false; }

        buffer.ring_push(first);
        first = NoChunk;

        return true;
    }

    auto discard() -> void {
        if (first == NoChunk) { return; }

        buffer.release_chunks(first);
        first = NoChunk;
    }

private:
    auto available() const -> size_t { return buffer.chunk_data_size() - pos; }

    auto grow() -> bool {
        auto next = buffer.add_chunk(current);

        if (next == NoChunk) {
            // Discard the whole record: an incomplete chain cannot be published.
            discard();
            return false;
        }

        current = next;
        pos = 0;

        return true;
    }

    IRingBuffer& buffer;
    ChunkId first{NoChunk};
    ChunkId current{NoChunk};
    size_t pos{0};
};


//
// Reads values back. The record leaves the ring on open() and belongs to the
// reader until close().
//
template <typename Codecs>
class WireReader {
public:
    explicit WireReader(IRingBuffer& buf) : buffer{buf} {}

    auto open() -> bool {
        return open(buffer.ring_pop());
    }

    // Takes ownership of a record already removed from the ring.
    auto open(ChunkId id) -> bool {
        first = id;
        if (first == NoChunk) { return false; }

        current = first;
        pos = 0;

        return true;
    }

    //
    // Prints one value into `output`. Keeps calling the codec while it asks for
    // more, so a value split into fragments comes out whole.
    //
    auto read(etl::istring& output, const etl::string_view& spec) -> ReadStatus {
        DecoderState state{};

        while (!state.done) {
            ConstByteSpan value{};

            if (!next_value(value)) {
                // A value that started and did not finish is a broken record,
                // an empty one is simply the end.
                return state.consumed_bytes == 0 ? ReadStatus::EndOfRecord : ReadStatus::Broken;
            }

            if (!Codecs::decode(value, output, spec, state)) {
                return ReadStatus::Broken;
            }

            // Reject a decoder that makes no progress to avoid an infinite loop.
            if (state.consumed_bytes == 0 && !state.done) { return ReadStatus::Broken; }

            pos += state.consumed_bytes;
        }

        return ReadStatus::Ok;
    }

    //
    // Reads the next value through a known codec without formatting it.
    // False means no value is available or the codec could not decode it.
    //
    template <typename Codec>
    auto read_raw(ConstByteSpan& raw) -> bool {
        ConstByteSpan value{};

        if (!next_value(value)) { return false; }
        if (!Codec::can_decode(value[0])) { return false; }

        auto state = Codec::decode_as_raw(value, raw);
        if (!state.done) { return false; }

        pos += state.consumed_bytes;

        return true;
    }

    auto close() -> void {
        if (first == NoChunk) { return; }

        buffer.release_chunks(first);
        first = NoChunk;
    }

private:
    auto available() const -> size_t { return buffer.chunk_data_size() - pos; }

    // Steps over the zeroed gaps, and over chunk ends, to the next value. The
    // span starts at its encoding byte and runs to the end of the chunk.
    auto next_value(ConstByteSpan& value) -> bool {
        if (first == NoChunk) { return false; }

        while (true) {
            if (pos == buffer.chunk_data_size() && !follow_next_chunk()) { return false; }
            if (buffer.chunk_data(current)[pos] != encoding::SkipByte) { break; }
            pos++;
        }

        value = ConstByteSpan(buffer.chunk_data(current) + pos, available());

        return true;
    }

    auto follow_next_chunk() -> bool {
        auto next = buffer.next_chunk(current);
        if (next == NoChunk) { return false; }

        current = next;
        pos = 0;

        return true;
    }

    IRingBuffer& buffer;
    ChunkId first{NoChunk};
    ChunkId current{NoChunk};
    size_t pos{0};
};

} // namespace jetlog
