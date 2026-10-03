#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include <ftxui/dom/elements.hpp>

#include "shmscope/layout/model.hpp"
#include "shmscope/layout/placement.hpp"

namespace shmscope {

    inline constexpr std::size_t MAX_PROBLEM_LINES = 3;
    inline constexpr std::size_t BYTES_PREVIEW = 8;
    inline constexpr std::size_t PANEL_HEADER_LINES = 2;

    [[nodiscard]] std::string_view labelOf(std::string_view path) noexcept;

    [[nodiscard]] std::string formatValue(const Layout& layout,
                                          const PlacedField& field,
                                          std::span<const std::byte> bytes);

    [[nodiscard]] std::optional<std::size_t> fieldAt(
        const Placement& placement, std::size_t offset) noexcept;

    [[nodiscard]] std::size_t panelCapacity(const Placement& placement,
                                            std::size_t rows) noexcept;

    [[nodiscard]] std::size_t clampTop(std::size_t top, std::size_t capacity,
                                       std::size_t count) noexcept;

    [[nodiscard]] std::size_t centredTop(std::size_t selected,
                                         std::size_t capacity,
                                         std::size_t count) noexcept;

    [[nodiscard]] ftxui::Element renderFieldPanel(
        const Layout& layout, const Placement& placement,
        std::span<const std::byte> bytes, std::optional<std::size_t> selected,
        std::size_t rows, std::optional<std::size_t> top = std::nullopt);

}  // namespace shmscope
