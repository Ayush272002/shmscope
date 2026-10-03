#include "shmscope/ui/field_panel.hpp"

#include <algorithm>
#include <cstddef>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>

#include "shmscope/core/decode.hpp"
#include "shmscope/core/default_formatters.hpp"
#include "shmscope/layout/model.hpp"
#include "shmscope/layout/placement.hpp"

namespace shmscope {

    namespace {

        const auto MUTED = ftxui::Color::RGB(110, 110, 110);
        constexpr auto WARNING = ftxui::Color::Yellow;

        struct Row {
            std::string name{};
            std::string value{};
            std::string offset{};
        };

        std::size_t columns(const std::string_view text) noexcept {
            return static_cast<std::size_t>(
                std::ranges::count_if(text, [](const char c) {
                    return (static_cast<unsigned char>(c) & 0xC0U) != 0x80U;
                }));
        }

        std::string padded(std::string text, const std::size_t width,
                           const bool right) {
            const auto used = columns(text);
            if (used >= width) return text;

            const std::string pad(width - used, ' ');
            return right ? pad + text : text + pad;
        }

        struct Widths {
            std::size_t name = 0;
            std::size_t value = 0;
            std::size_t offset = 0;
        };

        ftxui::Element renderLine(const Row& row, const Widths& widths,
                                  const bool selected) {
            auto line = ftxui::hbox({
                ftxui::text(" " + padded(row.name, widths.name, false) + "  "),
                ftxui::text(padded(row.value, widths.value, true)),
                ftxui::text("  " + padded(row.offset, widths.offset, true) +
                            " ") |
                    ftxui::color(MUTED),
                ftxui::filler(),
            });
            return selected ? line | ftxui::inverted : line;
        }

        ftxui::Elements renderFooter(const Placement& placement) {
            ftxui::Elements footer;
            const auto& problems = placement.problems;

            if (!problems.empty()) {
                footer.push_back(
                    ftxui::text(std::format(
                        " {} {}", problems.size(),
                        problems.size() == 1 ? "problem" : "problems")) |
                    ftxui::bold | ftxui::color(WARNING));

                const auto shown = std::min(problems.size(), MAX_PROBLEM_LINES);
                for (std::size_t i = 0; i < shown; ++i) {
                    footer.push_back(
                        ftxui::text(std::format("  {}: {}", problems[i].path,
                                                problems[i].message)) |
                        ftxui::color(WARNING));
                }
                if (problems.size() > shown) {
                    footer.push_back(
                        ftxui::text(std::format("  … {} more",
                                                problems.size() - shown)) |
                        ftxui::color(MUTED));
                }
            }

            if (placement.truncated) {
                footer.push_back(
                    ftxui::text(std::format(" stopped after {} fields",
                                            placement.fields.size())) |
                    ftxui::color(WARNING));
            }
            return footer;
        }

    }  // namespace

    std::string_view labelOf(const std::string_view path) noexcept {
        const auto dot = path.rfind('.');
        const auto bracket = path.rfind('[');

        if (path.ends_with(']') && bracket != std::string_view::npos &&
            (dot == std::string_view::npos || bracket > dot)) {
            return path.substr(bracket);
        }
        return dot == std::string_view::npos ? path : path.substr(dot + 1);
    }

    std::string formatValue(const Layout& layout, const PlacedField& field,
                            const std::span<const std::byte> bytes) {
        if (field.value) {
            if (field.attribute != nullptr &&
                !field.attribute->format.empty()) {
                const auto found = layout.formats.find(field.attribute->format);
                if (found != layout.formats.end()) {
                    return DefaultFormatters::apply(found->second,
                                                    *field.value);
                }
            }
            if (const auto* text =
                    std::get_if<std::string>(&field.value->data)) {
                return std::format("\"{}\"", *text);
            }
            return toText(*field.value);
        }

        if (field.attribute == nullptr) return {};

        const auto kind = field.attribute->kind;
        if (kind != AttributeKind::BYTES && kind != AttributeKind::CONTENTS) {
            return {};
        }
        if (field.offset > bytes.size()) return {};

        const auto shown =
            std::min({field.size, BYTES_PREVIEW, bytes.size() - field.offset});
        std::string text;
        for (std::size_t i = 0; i < shown; ++i) {
            if (!text.empty()) text += ' ';

            text += std::format(
                "{:02x}", std::to_integer<unsigned>(bytes[field.offset + i]));
        }
        if (field.size > shown) text += " …";

        return text;
    }

    std::optional<std::size_t> fieldAt(const Placement& placement,
                                       const std::size_t offset) noexcept {
        const auto& fields = placement.fields;
        std::optional<std::size_t> best;

        for (std::size_t i = 0; i < fields.size(); ++i) {
            const auto& field = fields[i];
            if (offset < field.offset || offset - field.offset >= field.size) {
                continue;
            }
            if (!best || field.depth > fields[*best].depth ||
                (field.depth == fields[*best].depth &&
                 field.size < fields[*best].size)) {
                best = i;
            }
        }
        return best;
    }

    std::size_t panelCapacity(const Placement& placement,
                              const std::size_t rows) noexcept {
        const auto problems = placement.problems.size();
        std::size_t footer = 0;
        if (problems > 0) {
            footer += 1 + std::min(problems, MAX_PROBLEM_LINES) +
                      (problems > MAX_PROBLEM_LINES ? 1 : 0);
        }
        if (placement.truncated) ++footer;

        const auto used = PANEL_HEADER_LINES + footer;
        return rows > used ? rows - used : 1;
    }

    std::size_t clampTop(const std::size_t top, const std::size_t capacity,
                         const std::size_t count) noexcept {
        return std::min(top, count > capacity ? count - capacity : 0);
    }

    std::size_t centredTop(const std::size_t selected,
                           const std::size_t capacity,
                           const std::size_t count) noexcept {
        return clampTop(selected > capacity / 2 ? selected - capacity / 2 : 0,
                        capacity, count);
    }

    ftxui::Element renderFieldPanel(const Layout& layout,
                                    const Placement& placement,
                                    const std::span<const std::byte> bytes,
                                    const std::optional<std::size_t> selected,
                                    const std::size_t rows,
                                    const std::optional<std::size_t> top) {
        const auto& fields = placement.fields;

        ftxui::Elements lines;
        lines.push_back(
            selected
                ? ftxui::text(" " + fields[*selected].path) | ftxui::bold
                : ftxui::text(" no field at the cursor") | ftxui::color(MUTED));
        lines.push_back(ftxui::text(""));

        auto footer = renderFooter(placement);
        const auto capacity = panelCapacity(placement, rows);

        if (fields.empty()) {
            lines.push_back(ftxui::text(" nothing placed") |
                            ftxui::color(MUTED));
        } else {
            const auto first =
                top ? clampTop(*top, capacity, fields.size())
                    : centredTop(selected.value_or(0), capacity, fields.size());
            const auto end = std::min(fields.size(), first + capacity);

            std::vector<Row> table;
            Widths widths;
            for (std::size_t i = first; i < end; ++i) {
                const auto& field = fields[i];
                Row row{.name = std::string(field.depth * 2, ' ') +
                                std::string(labelOf(field.path)),
                        .value = formatValue(layout, field, bytes),
                        .offset = std::format("{:x}", field.offset)};
                widths.name = std::max(widths.name, columns(row.name));
                widths.value = std::max(widths.value, columns(row.value));
                widths.offset = std::max(widths.offset, columns(row.offset));
                table.push_back(std::move(row));
            }

            for (std::size_t i = 0; i < table.size(); ++i) {
                lines.push_back(
                    renderLine(table[i], widths, selected == first + i));
            }
        }

        if (!footer.empty()) {
            lines.push_back(ftxui::filler());
            for (auto& line : footer) {
                lines.push_back(std::move(line));
            }
        }
        return ftxui::vbox(std::move(lines));
    }

}  // namespace shmscope
