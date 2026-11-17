#pragma once

#include "private/config.hpp"
#include "private/arguments.hpp"
#include "private/wire.hpp"
#include "private/ring_buffer.hpp"
#include "private/string_tokenizer.hpp"

#include <etl/limits.h>
#include <etl/string.h>
#include <etl/string_view.h>
#include <etl/to_string.h>
#include <etl/type_traits.h>

#include <stdint.h>
#include <string.h>

// Keeps a function out of line and stops GCC from cloning it per translation
// unit.
#if defined(__GNUC__) && !defined(__clang__)
#define JETLOG_NOINLINE __attribute__((noipa))
#elif defined(__GNUC__)
#define JETLOG_NOINLINE __attribute__((noinline))
#else
#define JETLOG_NOINLINE
#endif

namespace jetlog {

namespace level {
    enum Type : int8_t {
        error,
        warn,
        info,
        debug,
        verbose
    };
} // namespace level


//
// Timestamp source. Functions and captureless lambdas are stored as function
// pointers. Other callables must be named objects that outlive the writer;
// they are stored by reference, and temporaries are rejected.
//
class TimeSource {
public:
    TimeSource() = default;

    template <typename T, etl::enable_if_t<
        etl::is_convertible_v<T, uint32_t (*)()>, int> = 0>
    TimeSource(const T& function)
        : plain{function} {}

    template <typename T, etl::enable_if_t<
        etl::is_lvalue_reference_v<T>
        && !etl::is_convertible_v<T, uint32_t (*)()>
        && !etl::is_same_v<detail::bare<T>, TimeSource>, int> = 0>
    TimeSource(T&& source)
        : context{&source}
        , thunk{[](const void* at) { return (*static_cast<const detail::bare<T>*>(at))(); }} {}

    auto empty() const -> bool { return plain == nullptr && thunk == nullptr; }

    auto operator()() const -> uint32_t {
        if (plain != nullptr) { return plain(); }
        return thunk(context);
    }

private:
    uint32_t (*plain)(){nullptr};
    const void* context{nullptr};
    uint32_t (*thunk)(const void*){nullptr};
};


//
// Tag and level of a record. Declare one constexpr object per tag and level
// and pass it by reference: a call then carries a single address.
//
struct PushOptions {
    static_str tag;
    uint8_t level;
};


//
// Writes records directly into buffer memory. Many writers can share one
// buffer without waiting.
//
template <typename Cfg = Config<>>
class Writer {
public:
    using Codecs = typename Cfg::codecs;

    Writer(IRingBuffer& buf, TimeSource time = {})
        : buffer{buf}, time_source{time} {}

    // Both overloads normalize strings before write(), so literal lengths do not
    // multiply its instantiations.
    template <typename Fmt, typename... Args>
    auto push(const PushOptions& opts, Fmt&& fmt, const Args&... args) -> bool {
        return write(opts, detail::as_static(static_cast<Fmt&&>(fmt)),
                     detail::as_arg(args)...);
    }

    template <typename Tag, typename Fmt, typename... Args>
    auto push(Tag&& tag, uint8_t lvl, Fmt&& fmt, const Args&... args) -> bool {
        return write(PushOptions{detail::as_static(static_cast<Tag&&>(tag)), lvl},
                     detail::as_static(static_cast<Fmt&&>(fmt)),
                     detail::as_arg(args)...);
    }

private:
    using PutArgFn = bool (*)(WireWriter<Codecs>&, const void*);

    template <typename T>
    static auto put_arg(WireWriter<Codecs>& rec, const void* value) -> bool {
        return rec.put(*static_cast<const T*>(value));
    }

    // Only collects the arguments. The record is built by vwrite(),
    // a single copy shared by all calls.
    template <typename... Args>
    auto write(const PushOptions& opts, static_str fmt, const Args&... args) -> bool {
        if constexpr (sizeof...(Args) == 0) {
            return vwrite(opts, fmt, nullptr, nullptr, 0);
        } else {
            const void* values[] = { static_cast<const void*>(&args)... };
            static constexpr PutArgFn puts[] = { &put_arg<Args>... };
            return vwrite(opts, fmt, values, puts, sizeof...(Args));
        }
    }

    JETLOG_NOINLINE
    auto vwrite(const PushOptions& opts, static_str fmt,
                const void* const* values, const PutArgFn* puts, size_t count) -> bool {
        WireWriter<Codecs> rec{buffer};

        if (!rec.open()) {
            buffer.report_lost();
            return false;
        }

        uint32_t stamp = !time_source.empty()
            ? time_source()
            : etl::numeric_limits<uint32_t>::max();

        bool ok = rec.put(stamp)
               && rec.put(opts.tag)
               && rec.put(opts.level)
               && rec.put(fmt);

        for (size_t i = 0; ok && i < count; i++) {
            ok = puts[i](rec, values[i]);
        }

        if (!ok) {
            rec.discard();
            buffer.report_lost();
            return false;
        }

        return rec.commit();
    }

    IRingBuffer& buffer;
    TimeSource time_source;
};


//
// Reads one record or loss notification per pull(). Single reader, low priority.
// Appends to the caller's output string; the reader has no buffer of its own.
//
template <typename Cfg = Config<>>
class Reader {
public:
    using Codecs = typename Cfg::codecs;

    explicit Reader(IRingBuffer& buf) : buffer{buf} {}

    Reader(const Reader&) = delete;
    auto operator=(const Reader&) -> Reader& = delete;

    virtual ~Reader() {
        if (pending != NoChunk) { buffer.release_chunks(pending); }
    }

    auto pull(etl::istring& output) -> bool {
        while (true) {
            if (pending == NoChunk) {
                // Take the record before reporting losses, so writers cannot
                // evict it while the caller prints the loss notification.
                pending = buffer.ring_pop();
                auto lost = buffer.lost_count();

                if (lost != lost_seen) {
                    uint32_t count = lost - lost_seen;
                    lost_seen = lost;
                    writeLossReport(output, count);

                    return true;
                }
            }

            if (pending == NoChunk) { return false; }

            // A retained record goes next even if more losses have arrived.
            WireReader<Codecs> rec{buffer};
            rec.open(pending);
            pending = NoChunk;

            if (readRecord(rec, output)) { return true; }

            // Report a broken header as a loss on the next iteration. Continue
            // draining: pull() must return false only when no record is
            // available, so a malformed header cannot stop the caller's loop.
            buffer.report_lost();
        }
    }

    virtual auto writeLogHeader(etl::istring& output, uint32_t timestamp,
                                const etl::string_view& tag, uint8_t lvl) -> void {
        output.append(level2str(lvl));

        if (timestamp != etl::numeric_limits<uint32_t>::max()) {
            output.append(" (");
            etl::to_string(timestamp, output, true);
            output.append(")");
        }

        if (tag.length() > 0) {
            output.append(" ");
            output.append(tag.begin(), tag.end());
        }

        output.append(": ");
    }

    virtual auto writeLossReport(etl::istring& output, uint32_t count) -> void {
        output.append("... records lost: ");
        etl::to_string(count, output, true);
        output.append(" ...");
    }

    virtual auto level2str(uint8_t lvl) -> const char* {
        switch (lvl) {
            case level::error: return "E";
            case level::warn: return "W";
            case level::info: return "I";
            case level::debug: return "D";
            case level::verbose: return "V";
            default: return "UNKNOWN";
        }
    }

private:
    //
    // Reads one record into `output`. False means the header did not parse:
    // nothing was appended, and the record is released either way.
    //
    auto readRecord(WireReader<Codecs>& rec, etl::istring& output) -> bool {
        ConstByteSpan raw{};

        // The four header values are always written first, so their encodings
        // are known here and the bytes can be taken as they are.
        if (!rec.template read_raw<U32>(raw)) { rec.close(); return false; }
        auto timestamp = load_le<uint32_t>(raw.data());

        if (!rec.template read_raw<StrRef>(raw)) { rec.close(); return false; }
        etl::string_view tag(reinterpret_cast<const char*>(raw.data()), raw.size());

        if (!rec.template read_raw<U8>(raw)) { rec.close(); return false; }
        uint8_t lvl = raw[0];

        if (!rec.template read_raw<StrRef>(raw)) { rec.close(); return false; }
        etl::string_view fmt(reinterpret_cast<const char*>(raw.data()), raw.size());

        writeLogHeader(output, timestamp, tag, lvl);

        for (const auto& token : StringTokenizer(fmt)) {
            if (!token.is_placeholder) {
                output.append(token.text.begin(), token.text.end());
                continue;
            }

            switch (rec.read(output, token.text)) {
                case ReadStatus::Ok: break;

                // Nothing left to print here, show the placeholder as written.
                case ReadStatus::EndOfRecord:
                    output.append(token.text.begin(), token.text.end());
                    break;

                // Writer and reader share the config, so this means a broken
                // record.
                case ReadStatus::Broken:
                    output.append("[UNKNOWN]");
                    break;
            }
        }

        rec.close();
        return true;
    }

    IRingBuffer& buffer;
    uint32_t lost_seen{0};
    ChunkId pending{NoChunk};
};

} // namespace jetlog
