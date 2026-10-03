#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "shmscope/layout/document.hpp"
#include "shmscope/layout/model.hpp"

namespace shmscope {

    template <typename D>
    concept Dialect = requires(const Node& root, std::string_view source) {
        { D::NAME } -> std::convertible_to<std::string_view>;
        { D::matches(root) } -> std::same_as<bool>;
        { D::load(root, source) } -> std::same_as<LayoutResult>;
    };

    template <Dialect... Ds>
    class DialectSet {
    public:
        static constexpr std::array<std::string_view, sizeof...(Ds)> NAMES = {
            Ds::NAME...};

        [[nodiscard]] static bool recognise(const Node& root) {
            return (Ds::matches(root) || ...);
        }

        [[nodiscard]] static LayoutResult load(const Node& root,
                                               std::string_view source) {
            std::optional<LayoutResult> result;
            static_cast<void>(
                ((Ds::matches(root) &&
                  (result.emplace(Ds::load(root, source)), true)) ||
                 ...));

            if (!result) {
                return std::unexpected(LoadError{
                    .message = std::format(
                        "not a layout this build understands (dialects: {})",
                        knownList()),
                    .source = std::string(source),
                    .location = root.location()});
            }

            return std::move(*result);
        }

    private:
        static_assert(sizeof...(Ds) > 0, "a DialectSet needs a dialect");

        static constexpr bool uniqueNames() {
            for (std::size_t i = 0; i < NAMES.size(); ++i) {
                for (std::size_t j = i + 1; j < NAMES.size(); ++j) {
                    if (NAMES[i] == NAMES[j]) return false;
                }
            }

            return true;
        }

        static_assert(uniqueNames(), "two dialects share a NAME");

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
