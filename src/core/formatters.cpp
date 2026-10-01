#include "shmscope/core/formatters.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <format>
#include <initializer_list>
#include <limits>
#include <optional>
#include <ranges>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <variant>

namespace shmscope {

    namespace {

        std::optional<std::string> rejectUnknown(
            const FormatSpec& spec,
            std::initializer_list<std::string_view> allowed,
            bool takesEntries = false) {
            for (const auto& key : spec.options | std::views::keys) {
                if (std::ranges::find(allowed, key) == allowed.end()) {
                    return std::format("unknown option '{}'", key);
                }
            }

            if (!takesEntries && !spec.entries.empty()) {
                return std::string("does not take a table of values");
            }

            return std::nullopt;
        }

        std::optional<std::uint64_t> parseUnsigned(std::string_view text) {
            int base = 10;
            if (text.starts_with("0x") || text.starts_with("0X")) {
                text.remove_prefix(2);
                base = 16;
            }

            std::uint64_t value = 0;
            const auto* end = text.data() + text.size();
            const auto [ptr, ec] =
                std::from_chars(text.data(), end, value, base);

            if (text.empty() || ec != std::errc{} || ptr != end)
                return std::nullopt;

            return value;
        }

        std::optional<std::uint64_t> parseKey(std::string_view text) {
            if (!text.starts_with('-')) {
                return parseUnsigned(text);
            }

            const auto magnitude = parseUnsigned(text.substr(1));
            constexpr auto limit =
                static_cast<std::uint64_t>(
                    std::numeric_limits<std::int64_t>::max()) +
                1;

            if (!magnitude || *magnitude > limit) return std::nullopt;
            return 0 - *magnitude;
        }

        std::optional<std::uint64_t> integerBits(const Value& value) {
            if (const auto* u = std::get_if<std::uint64_t>(&value.data)) {
                return *u;
            }

            if (const auto* i = std::get_if<std::int64_t>(&value.data)) {
                return static_cast<std::uint64_t>(*i);
            }

            return std::nullopt;
        }

        std::string scaled(std::uint64_t magnitude, bool negative,
                           const ScaledFormatter::Options& options) {
            if (options.digits == 0) {
                return std::format("{}{}", negative ? "-" : "", magnitude);
            }
            return std::format("{}{}.{:0{}}", negative ? "-" : "",
                               magnitude / options.divisor,
                               magnitude % options.divisor, options.digits);
        }

        template <typename Duration>
        std::optional<std::string> timestamp(std::int64_t count) {
            constexpr auto earliest = std::chrono::sys_days{
                std::chrono::year{1} / std::chrono::January / 1};
            constexpr auto latest = std::chrono::sys_days{
                std::chrono::year{9999} / std::chrono::December / 31};

            const auto seconds =
                floor<std::chrono::seconds>(Duration{count}).count();
            if (seconds < std::chrono::duration_cast<std::chrono::seconds>(
                              earliest.time_since_epoch())
                              .count() ||
                seconds > duration_cast<std::chrono::seconds>(
                              latest.time_since_epoch())
                              .count()) {
                return std::nullopt;
            }
            return std::format("{:%F %T} UTC", std::chrono::sys_time<Duration>{
                                                   Duration{count}});
        }
    }  // namespace

    std::string DecimalFormatter::apply(const Value& value) {
        return toText(value);
    }

    std::string HexFormatter::apply(const Value& value) {
        const auto bits = integerBits(value);
        if (!bits || value.width == 0 || value.width > 8) {
            return toText(value);
        }

        const std::uint64_t mask =
            value.width == 8 ? ~std::uint64_t{0}
                             : (std::uint64_t{1} << (value.width * 8)) - 1;

        return std::format("0x{:0{}x}", *bits & mask, value.width * 2);
    }

    std::expected<ScaledFormatter::Options, std::string> ScaledFormatter::parse(
        const FormatSpec& spec) {
        if (auto error = rejectUnknown(spec, {"digits"})) {
            return std::unexpected(std::move(*error));
        }
        const auto found = spec.options.find("digits");
        if (found == spec.options.end()) {
            return std::unexpected(std::string("needs 'digits'"));
        }
        const auto digits = parseUnsigned(found->second);
        if (!digits || *digits > MAX_DIGITS) {
            return std::unexpected(
                std::format("'digits' must be 0 to {}, got '{}'", MAX_DIGITS,
                            found->second));
        }

        Options options{.digits = static_cast<unsigned>(*digits)};
        for (unsigned i = 0; i < options.digits; ++i) {
            options.divisor *= 10;
        }
        return options;
    }

    std::string ScaledFormatter::apply(const Value& value,
                                       const Options& options) {
        if (const auto* u = std::get_if<std::uint64_t>(&value.data)) {
            return scaled(*u, false, options);
        }
        if (const auto* i = std::get_if<std::int64_t>(&value.data)) {
            const bool negative = *i < 0;
            const auto magnitude = negative ? 0 - static_cast<std::uint64_t>(*i)
                                            : static_cast<std::uint64_t>(*i);
            return scaled(magnitude, negative, options);
        }
        return toText(value);
    }

    std::expected<EnumFormatter::Options, std::string> EnumFormatter::parse(
        const FormatSpec& spec) {
        if (auto error = rejectUnknown(spec, {}, true)) {
            return std::unexpected(std::move(*error));
        }
        if (spec.entries.empty()) {
            return std::unexpected(std::string("needs at least one value"));
        }

        Options options;
        for (const auto& [key, name] : spec.entries) {
            const auto parsed = parseKey(key);
            if (!parsed) {
                return std::unexpected(
                    std::format("'{}' is not an integer", key));
            }
            if (std::ranges::find(
                    options.names, *parsed,
                    &std::pair<std::uint64_t, std::string>::first) !=
                options.names.end()) {
                return std::unexpected(
                    std::format("value '{}' is listed twice", key));
            }
            options.names.emplace_back(*parsed, name);
        }
        return options;
    }

    std::string EnumFormatter::apply(const Value& value,
                                     const Options& options) {
        const auto bits = integerBits(value);
        if (!bits) {
            return toText(value);
        }
        const auto found =
            std::ranges::find(options.names, *bits,
                              &std::pair<std::uint64_t, std::string>::first);
        return found == options.names.end() ? toText(value) : found->second;
    }

    std::expected<TimestampFormatter::Options, std::string>
    TimestampFormatter::parse(const FormatSpec& spec) {
        if (auto error = rejectUnknown(spec, {"unit"})) {
            return std::unexpected(std::move(*error));
        }
        const auto found = spec.options.find("unit");
        if (found == spec.options.end() || found->second == "ns") {
            return Options{.unit = Unit::NS};
        }
        if (found->second == "us") {
            return Options{.unit = Unit::US};
        }
        if (found->second == "ms") {
            return Options{.unit = Unit::MS};
        }
        if (found->second == "s") {
            return Options{.unit = Unit::S};
        }
        return std::unexpected(std::format(
            "'unit' must be s, ms, us or ns, got '{}'", found->second));
    }

    std::string TimestampFormatter::apply(const Value& value,
                                          const Options& options) {
        std::optional<std::int64_t> count;
        if (const auto* i = std::get_if<std::int64_t>(&value.data)) {
            count = *i;
        } else if (const auto* u = std::get_if<std::uint64_t>(&value.data);
                   u != nullptr &&
                   *u <= static_cast<std::uint64_t>(
                             std::numeric_limits<std::int64_t>::max())) {
            count = static_cast<std::int64_t>(*u);
        }
        if (!count) {
            return toText(value);
        }

        std::optional<std::string> text;
        switch (options.unit) {
            case Unit::S:
                text = timestamp<std::chrono::seconds>(*count);
                break;
            case Unit::MS:
                text = timestamp<std::chrono::milliseconds>(*count);
                break;
            case Unit::US:
                text = timestamp<std::chrono::microseconds>(*count);
                break;
            case Unit::NS:
                text = timestamp<std::chrono::nanoseconds>(*count);
                break;
        }
        return text ? std::move(*text) : toText(value);
    }
}  // namespace shmscope
