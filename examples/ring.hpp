#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <type_traits>

namespace ring {

    inline constexpr std::array<char, 8> MAGIC{'S', 'H', 'M', 'R',
                                               'I', 'N', 'G', '\0'};
    inline constexpr std::uint32_t VERSION = 1;
    inline constexpr std::uint32_t CAPACITY = 256;
    inline constexpr std::uint32_t RECORD_SIZE = 64;
    inline constexpr std::size_t HOT_OFFSET = 64;
    inline constexpr std::size_t RECORDS_OFFSET = 128;
    inline constexpr std::size_t BYTES =
        RECORDS_OFFSET + std::size_t{CAPACITY} * RECORD_SIZE;
    inline constexpr std::int64_t PRICE_SCALE = 10'000;

    enum class Kind : std::uint16_t { TRADE = 1, QUOTE = 2 };
    enum class Side : std::int8_t { BUY = 1, SELL = -1 };

    struct Header {
        std::array<char, 8> magic{};
        std::uint32_t version = 0;
        std::uint32_t recordSize = 0;
        std::uint32_t capacity = 0;
        std::uint32_t reserved0 = 0;
        std::uint64_t recordsOffset = 0;
        std::array<char, 16> name{};
        std::uint64_t startedAtNanos = 0;
        std::array<std::byte, 8> reserved{};
    };

    struct Hot {
        std::uint64_t sequence = 0;
        std::uint64_t heartbeatNanos = 0;
        std::int32_t writerPid = 0;
        std::array<std::byte, 44> reserved{};
    };

    struct RecordHeader {
        std::uint64_t sequence = 0;
        std::uint64_t timestampNanos = 0;
        Kind kind = Kind::TRADE;
        std::uint16_t flags = 0;
        std::array<std::byte, 4> reserved{};
    };

    struct Trade {
        std::int64_t price = 0;
        std::int64_t quantity = 0;
        Side side = Side::BUY;
        std::array<std::byte, 23> reserved{};
    };

    struct Quote {
        std::int64_t bidPrice = 0;
        std::int64_t bidQuantity = 0;
        std::int64_t askPrice = 0;
        std::int64_t askQuantity = 0;
        std::array<std::byte, 8> reserved{};
    };

    static_assert(sizeof(Header) == HOT_OFFSET);
    static_assert(HOT_OFFSET + sizeof(Hot) == RECORDS_OFFSET);
    static_assert(sizeof(RecordHeader) + sizeof(Trade) == RECORD_SIZE);
    static_assert(sizeof(RecordHeader) + sizeof(Quote) == RECORD_SIZE);
    static_assert(offsetof(Header, recordsOffset) == 24);
    static_assert(offsetof(Header, name) == 32);
    static_assert(offsetof(Header, startedAtNanos) == 48);
    static_assert(offsetof(Hot, writerPid) == 16);
    static_assert(offsetof(RecordHeader, kind) == 16);
    static_assert(offsetof(Trade, side) == 16);
    static_assert(std::is_trivially_copyable_v<Header>);
    static_assert(std::is_trivially_copyable_v<Hot>);
    static_assert(std::is_trivially_copyable_v<RecordHeader>);
    static_assert(std::is_trivially_copyable_v<Trade>);
    static_assert(std::is_trivially_copyable_v<Quote>);

    template <typename T>
    void store(const std::span<std::byte> mapping, const std::size_t offset,
               const T& value) noexcept {
        std::memcpy(mapping.data() + offset, &value, sizeof value);
    }

    inline void writeHeader(const std::span<std::byte> mapping,
                            const std::string_view name,
                            const std::uint64_t startedAtNanos,
                            const std::int32_t writerPid) noexcept {
        Header header{.magic = MAGIC,
                      .version = VERSION,
                      .recordSize = RECORD_SIZE,
                      .capacity = CAPACITY,
                      .recordsOffset = RECORDS_OFFSET,
                      .startedAtNanos = startedAtNanos};
        std::copy_n(name.begin(), std::min(name.size(), header.name.size()),
                    header.name.begin());
        store(mapping, 0, header);
        store(mapping, HOT_OFFSET, Hot{.writerPid = writerPid});
    }

    template <typename Body>
        requires std::same_as<Body, Trade> || std::same_as<Body, Quote>
    void writeRecord(const std::span<std::byte> mapping,
                     const std::uint64_t sequence, const std::uint64_t nanos,
                     const Body& body) noexcept {
        const auto at = RECORDS_OFFSET + (sequence % CAPACITY) * RECORD_SIZE;
        store(mapping, at,
              RecordHeader{.sequence = sequence,
                           .timestampNanos = nanos,
                           .kind = std::same_as<Body, Trade> ? Kind::TRADE
                                                             : Kind::QUOTE});
        store(mapping, at + sizeof(RecordHeader), body);
    }

    inline void publish(const std::span<std::byte> mapping,
                        const std::uint64_t written,
                        const std::uint64_t nanos) noexcept {
        std::atomic_thread_fence(std::memory_order_release);
        store(mapping, HOT_OFFSET + offsetof(Hot, sequence), written);
        store(mapping, HOT_OFFSET + offsetof(Hot, heartbeatNanos), nanos);
    }

}  // namespace ring
