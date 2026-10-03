#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "shmscope/core/decode.hpp"
#include "shmscope/layout/document.hpp"
#include "shmscope/layout/model.hpp"

namespace shmscope {

    struct PlacementLimits {
        std::size_t maxFields = 65536;
        std::size_t maxDepth = 32;
        std::size_t maxRepeat = 1U << 20U;
    };

    struct PlacedField {
        std::string path{};
        const Attribute* attribute = nullptr;
        std::size_t offset = 0;
        std::size_t size = 0;
        std::size_t depth = 0;
        std::optional<std::size_t> parent{};
        std::optional<Value> value{};
    };

    struct PlacementProblem {
        std::string path{};
        std::string message{};
        Location location{};
    };

    struct Placement {
        std::vector<PlacedField> fields{};
        std::vector<PlacementProblem> problems{};
        bool truncated = false;

        [[nodiscard]] const PlacedField* find(
            std::string_view path) const noexcept;
    };

    [[nodiscard]] Placement place(const Layout& layout,
                                  std::span<const std::byte> bytes,
                                  const PlacementLimits& limits = {});
}  // namespace shmscope
