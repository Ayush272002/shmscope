#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "shmscope/core/decode.hpp"
#include "shmscope/core/format.hpp"

namespace shmscope {

    struct DecimalFormatter {
        static constexpr std::string_view NAME = "decimal";

        [[nodiscard]] static std::string apply(const Value& value);
    };

    struct HexFormatter {
        static constexpr std::string_view NAME = "hex";

        [[nodiscard]] static std::string apply(const Value& value);
    };

    struct ScaledFormatter {
        static constexpr std::string_view NAME = "scaled";
        static constexpr unsigned MAX_DIGITS = 18;
        struct Options {
            unsigned digits = 0;
            std::uint64_t divisor = 1;
        };

        [[nodiscard]] static std::expected<Options, std::string> parse(
            const FormatSpec& spec);
        [[nodiscard]] static std::string apply(const Value& value,
                                               const Options& options);
    };

    struct EnumFormatter {
        static constexpr std::string_view NAME = "enum";
        struct Options {
            std::vector<std::pair<std::uint64_t, std::string>> names{};
        };

        [[nodiscard]] static std::expected<Options, std::string> parse(
            const FormatSpec& spec);
        [[nodiscard]] static std::string apply(const Value& value,
                                               const Options& options);
    };

    struct TimestampFormatter {
        static constexpr std::string_view NAME = "timestamp";
        enum class Unit : std::uint8_t { S, MS, US, NS };
        struct Options {
            Unit unit = Unit::NS;
        };

        [[nodiscard]] static std::expected<Options, std::string> parse(
            const FormatSpec& spec);
        [[nodiscard]] static std::string apply(const Value& value,
                                               const Options& options);
    };

    static_assert(Formatter<DecimalFormatter>);
    static_assert(Formatter<HexFormatter>);
    static_assert(Formatter<ScaledFormatter>);
    static_assert(Formatter<EnumFormatter>);
    static_assert(Formatter<TimestampFormatter>);
}  // namespace shmscope
