#include <gtest/gtest.h>
#include "jetlog/private/codecs.hpp"
#include "jetlog/private/wire.hpp"

#include <string.h>

using Codecs = jetlog::CodecList<jetlog::U8, jetlog::U32, jetlog::StrRef, jetlog::StrCopy>;
using Writer = jetlog::WireWriter<Codecs>;
using Reader = jetlog::WireReader<Codecs>;

namespace {

// Drains the pool and says how many chunks it held. The ring has to be empty,
// or create_chunk() starts evicting records instead of running out.
auto free_chunks(jetlog::IRingBuffer& buf) -> int {
    int count = 0;
    while (buf.create_chunk() != jetlog::NoChunk) { count++; }

    return count;
}

// Reads the whole record into one string, values separated by '|'.
auto drain(Reader& rec) -> etl::string<512> {
    etl::string<512> out;

    while (rec.read(out, "{}") == jetlog::ReadStatus::Ok) { out.append("|"); }

    return out;
}

} // namespace

TEST(Wire, ValuesComeBackInOrder) {
    jetlog::RingBuffer<1024> buffer{};

    Writer rec{buffer};
    ASSERT_TRUE(rec.open());
    ASSERT_TRUE(rec.put(uint8_t{7}));
    ASSERT_TRUE(rec.put(uint32_t{0xDEADBEEF}));
    ASSERT_TRUE(rec.put(jetlog::static_str("tag")));
    ASSERT_TRUE(rec.commit());

    Reader scan{buffer};
    ASSERT_TRUE(scan.open());
    ASSERT_EQ(drain(scan), etl::string<512>("7|3735928559|tag|"));
    scan.close();
}

TEST(Wire, LongRecordSpansChunks) {
    jetlog::RingBuffer<320, 32> buffer{};   // 10 chunks, 30 bytes each

    Writer rec{buffer};
    ASSERT_TRUE(rec.open());

    // Six U32 values fill the 30-byte payload; the seventh needs a new chunk.
    for (int i = 0; i < 7; i++) { ASSERT_TRUE(rec.put(static_cast<uint32_t>(i))); }
    ASSERT_TRUE(rec.commit());

    Reader scan{buffer};
    ASSERT_TRUE(scan.open());
    ASSERT_EQ(drain(scan), etl::string<512>("0|1|2|3|4|5|6|"));
    scan.close();

    ASSERT_EQ(free_chunks(buffer), 10);
}

TEST(Wire, LongStringComesBackWhole) {
    jetlog::RingBuffer<1024, 32> buffer{};   // 30 bytes per chunk

    etl::string<400> source;
    source.resize(300, 'x');

    Writer rec{buffer};
    ASSERT_TRUE(rec.open());
    ASSERT_TRUE(rec.put(jetlog::detail::copy_str(source)));
    ASSERT_TRUE(rec.commit());

    Reader scan{buffer};
    ASSERT_TRUE(scan.open());

    etl::string<512> out;
    ASSERT_EQ(scan.read(out, "{}"), jetlog::ReadStatus::Ok);
    ASSERT_EQ(out.size(), source.size());
    ASSERT_EQ(out, etl::string<512>(source.c_str()));

    ASSERT_EQ(scan.read(out, "{}"), jetlog::ReadStatus::EndOfRecord);
    scan.close();
}

TEST(Wire, EmptyStringCostsNothingAndReadsBack) {
    jetlog::RingBuffer<1024> buffer{};

    Writer rec{buffer};
    ASSERT_TRUE(rec.open());
    ASSERT_TRUE(rec.put(jetlog::detail::copy_str(etl::string_view())));
    ASSERT_TRUE(rec.put(uint8_t{1}));
    ASSERT_TRUE(rec.commit());

    Reader scan{buffer};
    ASSERT_TRUE(scan.open());
    ASSERT_EQ(drain(scan), etl::string<512>("|1|"));
    scan.close();
}

TEST(Wire, DiscardedRecordIsNotVisibleAndGivesMemoryBack) {
    jetlog::RingBuffer<320, 32> buffer{};   // 10 chunks

    Writer rec{buffer};
    ASSERT_TRUE(rec.open());
    for (int i = 0; i < 9; i++) { ASSERT_TRUE(rec.put(static_cast<uint32_t>(i))); }
    rec.discard();

    Reader scan{buffer};
    ASSERT_FALSE(scan.open());

    ASSERT_EQ(free_chunks(buffer), 10);
}

TEST(Wire, RecordDoesNotOutgrowTheBuffer) {
    jetlog::RingBuffer<320, 32> buffer{};   // 10 chunks, 300 bytes of data

    etl::string<600> text;
    text.resize(500, 'x');

    Writer rec{buffer};
    ASSERT_TRUE(rec.open());
    ASSERT_FALSE(rec.put(jetlog::detail::copy_str(text)));

    // A failed record rejects further writes and cannot be committed.
    ASSERT_FALSE(rec.put(uint8_t{7}));
    ASSERT_FALSE(rec.commit());

    Reader scan{buffer};
    ASSERT_FALSE(scan.open());

    // Every chunk the failed record touched is back in the pool.
    ASSERT_EQ(free_chunks(buffer), 10);
}

TEST(Wire, CommittedRecordIsOutOfTheWritersHands) {
    jetlog::RingBuffer<320, 32> buffer{};   // 10 chunks

    Writer rec{buffer};
    ASSERT_TRUE(rec.open());
    ASSERT_TRUE(rec.put(uint32_t{0xDEADBEEF}));
    ASSERT_TRUE(rec.commit());

    // The ring owns the committed record: a second commit must fail, and
    // discard() must leave the published chain intact.
    ASSERT_FALSE(rec.commit());
    rec.discard();

    Reader scan{buffer};
    ASSERT_TRUE(scan.open());
    ASSERT_EQ(drain(scan), etl::string<512>("3735928559|"));
    scan.close();

    ASSERT_EQ(free_chunks(buffer), 10);
}

TEST(Wire, RecordEndIsNotAnError) {
    jetlog::RingBuffer<1024> buffer{};

    Writer rec{buffer};
    ASSERT_TRUE(rec.open());
    ASSERT_TRUE(rec.put(uint8_t{1}));
    ASSERT_TRUE(rec.commit());

    Reader scan{buffer};
    ASSERT_TRUE(scan.open());

    etl::string<128> out;
    ASSERT_EQ(scan.read(out, "{}"), jetlog::ReadStatus::Ok);
    ASSERT_EQ(scan.read(out, "{}"), jetlog::ReadStatus::EndOfRecord);
    scan.close();
}
