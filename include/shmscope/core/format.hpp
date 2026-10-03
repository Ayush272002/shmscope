#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <expected>
#include <format>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "shmscope/core/decode.hpp"

namespace shmscope {

    struct FormatSpec {
        std::string kind{};
        std::map<std::string, std::string, std::less<>> options{};
        std::vector<std::pair<std::string, std::string>> entries{};
    };

    template <typename F>
    concept HasOptions = requires { typename F::Options; };

    template <typename F>
    concept OptionsFormatter =
        HasOptions<F> && std::copyable<typename F::Options> &&
        requires(const FormatSpec& spec, const Value& value,
                 const typename F::Options& options) {
            {
                F::parse(spec)
            } -> std::same_as<std::expected<typename F::Options, std::string>>;
            { F::apply(value, options) } -> std::same_as<std::string>;
        };

    template <typename F>
    concept PlainFormatter = !HasOptions<F> && requires(const Value& value) {
        { F::apply(value) } -> std::same_as<std::string>;
    };

    template <typename F>
    concept Formatter = requires {
        { F::NAME } -> std::convertible_to<std::string_view>;
    } && (OptionsFormatter<F> || PlainFormatter<F>);

    namespace detail {

        template <typename F>
        struct OptionsOf {
            using type = std::monostate;
        };

        template <HasOptions F>
        struct OptionsOf<F> {
            using type = typename F::Options;
        };

    }  // namespace detail

    template <typename F>
    using OptionsOf = typename detail::OptionsOf<F>::type;

    template <Formatter F>
    struct Bound {
        OptionsOf<F> options{};
    };

    template <Formatter... Fs>
    class FormatterSet {
    public:
        using Compiled = std::variant<Bound<Fs>...>;

        static constexpr std::array<std::string_view, sizeof...(Fs)> NAMES = {
            Fs::NAME...};

        [[nodiscard]] static constexpr bool knows(std::string_view kind) {
            return ((Fs::NAME == kind) || ...);
        }

        [[nodiscard]] static std::expected<Compiled, std::string> compile(
            const FormatSpec& spec) {
            std::optional<std::expected<Compiled, std::string>> result;

            static_cast<void>(((Fs::NAME == spec.kind &&
                                (result.emplace(bind<Fs>(spec)), true)) ||
                               ...));

            if (!result) {
                return std::unexpected(std::format(
                    "unknown format '{}' (known: {})", spec.kind, knownList()));
            }
            return std::move(*result);
        }

        [[nodiscard]] static std::string apply(const Compiled& format,
                                               const Value& value) {
            return std::visit(
                [&]<typename F>(const Bound<F>& bound) {
                    if constexpr (OptionsFormatter<F>) {
                        return F::apply(value, bound.options);
                    } else {
                        return F::apply(value);
                    }
                },
                format);
        }

    private:
        static_assert(sizeof...(Fs) > 0, "a FormatterSet needs a formatter");

        static constexpr bool uniqueNames() {
            for (std::size_t i = 0; i < NAMES.size(); ++i) {
                for (std::size_t j = i + 1; j < NAMES.size(); ++j) {
                    if (NAMES[i] == NAMES[j]) return false;
                }
            }

            return true;
        }

        static_assert(uniqueNames(), "two formatters share a NAME");

        template <Formatter F>
        static std::expected<Compiled, std::string> bind(
            const FormatSpec& spec) {
            if constexpr (OptionsFormatter<F>) {
                auto options = F::parse(spec);
                if (!options) {
                    return std::unexpected(
                        std::format("format '{}': {}", F::NAME,
                                    std::move(options.error())));
                }
                return Compiled{std::in_place_type<Bound<F>>,
                                Bound<F>{std::move(*options)}};
            } else {
                if (!spec.options.empty() || !spec.entries.empty()) {
                    return std::unexpected(
                        std::format("format '{}': takes no options", F::NAME));
                }
                return Compiled{std::in_place_type<Bound<F>>, Bound<F>{}};
            }
        }

        static std::string knownList() {
            std::string list;
            for (const std::string_view name : NAMES) {
                if (!list.empty()) list += ", ";
                list += name;
            }

            return list;
        }
    };

}  // namespace shmscope
