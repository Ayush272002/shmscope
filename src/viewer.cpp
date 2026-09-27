#include "shmscope/viewer.hpp"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>
#include <ftxui/screen/terminal.hpp>

#include "shmscope/decode.hpp"
#include "shmscope/source.hpp"

namespace shmscope {

    namespace {

        constexpr int CHROME_ROWS = 3;

        ftxui::Color heatColor(std::uint8_t heat, std::byte value) {
            const auto cold = value == std::byte{0}
                                  ? ftxui::Color::RGB(90, 90, 90)
                                  : ftxui::Color::RGB(170, 170, 170);

            if (heat == 0) return cold;
            const float t = static_cast<float>(heat) / HEAT_MAX;
            return ftxui::Color::Interpolate(t, cold,
                                             ftxui::Color::RGB(255, 200, 0));
        }

        // Accepts "259e00", "0x259e00" or "0X259E00".
        std::optional<std::size_t> parseOffset(std::string_view text) {
            if (text.starts_with("0x") || text.starts_with("0X")) {
                text.remove_prefix(2);
            }
            std::size_t value = 0;
            const auto* end = text.data() + text.size();
            const auto [ptr, ec] = std::from_chars(text.data(), end, value, 16);
            if (text.empty() || ec != std::errc{} || ptr != end) {
                return std::nullopt;
            }
            return value;
        }

    }  // namespace

    Viewer::Viewer(int hz, CloseFn onClose)
        : hz_(hz), onClose_(std::move(onClose)) {
        addCommands();

        root_ = ftxui::Renderer([this](bool /*focused*/) { return render(); }) |
                ftxui::CatchEvent([this](const ftxui::Event& event) {
                    return onEvent(event);
                });
    }

    void Viewer::attach(std::unique_ptr<Source> source) {
        source_ = std::move(source);
        top_ = 0;
        cursor_ = 0;
        followBlock_ = 0;
        following_ = false;
        frozen_ = false;
        live_ = false;
        liveTop_ = 0;
        frame_ = source_->poll();
    }

    void Viewer::detach() noexcept {
        frame_ = {};
        source_.reset();
    }

    void Viewer::tick() noexcept {
        if (source_ && !frozen_) {
            frame_ = source_->poll();
            if (following_) follow();
        }
    }

    void Viewer::follow() noexcept {
        constexpr std::size_t blockBytes = FOLLOW_BLOCK_ROWS * BYTES_PER_ROW;
        const auto heat = frame_.heat;

        std::size_t bestBlock = 0;
        std::size_t bestScore = 0;
        std::size_t currentScore = 0;
        for (std::size_t start = 0; start < heat.size(); start += blockBytes) {
            const auto chunk =
                heat.subspan(start, std::min(blockBytes, heat.size() - start));
            std::size_t score = 0;
            for (const std::uint8_t h : chunk) {
                score += h == HEAT_MAX ? 1 : 0;
            }
            const std::size_t block = start / blockBytes;
            if (block == followBlock_) {
                currentScore = score;
            }
            if (score > bestScore) {
                bestScore = score;
                bestBlock = block;
            }
        }

        if (bestScore == 0) {
            return;
        }
        if (bestScore > 2 * currentScore) {
            followBlock_ = bestBlock;
        }

        const std::size_t start = followBlock_ * blockBytes;
        const auto chunk =
            heat.subspan(start, std::min(blockBytes, heat.size() - start));
        const auto first = std::ranges::find(chunk, HEAT_MAX);
        const std::size_t offset =
            start + static_cast<std::size_t>(first - chunk.begin());
        const std::size_t row = offset / BYTES_PER_ROW;
        const std::size_t lead = visibleRows_ / 4;
        top_ = std::min(row > lead ? row - lead : 0, maxTop());
    }

    void Viewer::goTo(std::size_t row) noexcept {
        following_ = false;
        top_ = std::min(row, maxTop());
    }

    void Viewer::toTop() noexcept {
        if (live_) {
            liveTop_ = 0;
        } else {
            setCursor(0);
        }
    }

    void Viewer::toBottom() {
        if (live_) {
            buildLive();
            liveTop_ = liveMaxTop();
        } else if (!frame_.bytes.empty()) {
            setCursor(frame_.bytes.size() - 1);
        }
    }

    void Viewer::setCursor(std::size_t offset) noexcept {
        following_ = false;
        if (frame_.bytes.empty()) {
            cursor_ = 0;
            return;
        }
        cursor_ = std::min(offset, frame_.bytes.size() - 1);

        const std::size_t row = cursor_ / BYTES_PER_ROW;
        if (row < top_) {
            top_ = row;
        } else if (row >= top_ + visibleRows_) {
            top_ = row - visibleRows_ + 1;
        }
    }

    void Viewer::moveCursor(std::ptrdiff_t bytes) noexcept {
        const auto target = static_cast<std::ptrdiff_t>(cursor_) + bytes;
        setCursor(
            static_cast<std::size_t>(std::max<std::ptrdiff_t>(target, 0)));
    }

    void Viewer::scrollWithCursor(std::ptrdiff_t rows) noexcept {
        scrollBy(rows);
        if (frame_.bytes.empty()) {
            return;
        }

        const std::size_t row = cursor_ / BYTES_PER_ROW;
        const std::size_t lastRow = (frame_.bytes.size() - 1) / BYTES_PER_ROW;
        if ((rows < 0 && row == 0) || (rows > 0 && row == lastRow)) {
            return;
        }

        moveCursor(rows * static_cast<std::ptrdiff_t>(BYTES_PER_ROW));
    }

    ftxui::Element Viewer::renderInspector() const {
        const auto muted = ftxui::Color::RGB(110, 110, 110);

        ftxui::Elements lines;
        lines.push_back(ftxui::text(std::format(" cursor 0x{:x}", cursor_)) |
                        ftxui::bold);
        lines.push_back(ftxui::text(""));

        for (const FieldType type : ALL_FIELD_TYPES) {
            const auto value = decode(type, frame_.bytes, cursor_);
            lines.push_back(ftxui::hbox({
                ftxui::text(std::format(" {:<8}", nameOf(type))) |
                    ftxui::color(muted),
                value ? ftxui::text(*value)
                      : ftxui::text("–") | ftxui::color(muted),
            }));
        }

        return ftxui::vbox(std::move(lines)) |
               ftxui::size(ftxui::WIDTH, ftxui::EQUAL, INSPECTOR_WIDTH);
    }

    bool Viewer::rowChanged(std::size_t row) const noexcept {
        const auto heat = frame_.heat;
        const std::size_t begin = row * BYTES_PER_ROW;
        const std::size_t end = std::min(begin + BYTES_PER_ROW, heat.size());
        return std::any_of(heat.begin() + static_cast<std::ptrdiff_t>(begin),
                           heat.begin() + static_cast<std::ptrdiff_t>(end),
                           [](std::uint8_t h) { return h > 0; });
    }

    void Viewer::buildLive() {
        struct Region {
            std::size_t first;
            std::size_t last;
        };
        std::vector<Region> regions;

        const std::size_t rows = rowCount();
        std::size_t row = 0;
        while (row < rows) {
            if (!rowChanged(row)) {
                ++row;
                continue;
            }
            Region region{.first = row, .last = row};
            for (std::size_t next = row + 1;
                 next < rows && next <= region.last + LIVE_MERGE_ROWS + 1;
                 ++next) {
                if (rowChanged(next)) {
                    region.last = next;
                }
            }
            regions.push_back(region);
            row = region.last + 1;
        }

        const auto linesFor = [&regions](std::size_t cap) {
            std::size_t lines = 0;
            std::size_t shownUpTo = 0;
            for (const Region& region : regions) {
                const std::size_t length = region.last - region.first + 1;
                lines += (region.first > shownUpTo ? 1 : 0) +
                         std::min(length, cap) + (length > cap ? 1 : 0);
                shownUpTo = region.last + 1;
            }
            return lines;
        };

        std::size_t longest = 0;
        for (const Region& region : regions) {
            longest = std::max(longest, region.last - region.first + 1);
        }
        std::size_t cap = LIVE_RUN_ROWS;
        while (cap < longest && linesFor(cap + 1) <= visibleRows_) {
            ++cap;
        }

        liveLines_.clear();
        std::size_t shownUpTo = 0;
        for (const Region& region : regions) {
            if (region.first > shownUpTo) {
                liveLines_.push_back(
                    {.kind = LiveLine::Kind::GAP,
                     .value = (region.first - shownUpTo) * BYTES_PER_ROW});
            }
            const std::size_t length = region.last - region.first + 1;
            const std::size_t shown = std::min(length, cap);
            for (std::size_t i = 0; i < shown; ++i) {
                liveLines_.push_back(
                    {.kind = LiveLine::Kind::ROW, .value = region.first + i});
            }
            if (length > shown) {
                liveLines_.push_back(
                    {.kind = LiveLine::Kind::MORE, .value = length - shown});
            }
            shownUpTo = region.last + 1;
        }
    }

    std::size_t Viewer::liveMaxTop() const noexcept {
        return liveLines_.size() > visibleRows_
                   ? liveLines_.size() - visibleRows_
                   : 0;
    }

    ftxui::Elements Viewer::renderLive(std::size_t count) const {
        ftxui::Elements lines;
        if (liveLines_.empty()) {
            lines.push_back(ftxui::text("  nothing is changing") | ftxui::dim);
            return lines;
        }

        const std::size_t end = std::min(liveLines_.size(), liveTop_ + count);
        for (std::size_t i = liveTop_; i < end; ++i) {
            const LiveLine& line = liveLines_[i];
            switch (line.kind) {
                case LiveLine::Kind::ROW:
                    lines.push_back(renderRow(line.value));
                    break;
                case LiveLine::Kind::GAP:
                    lines.push_back(ftxui::text(std::format(
                                        "            ⋯ 0x{:x} bytes unchanged",
                                        line.value)) |
                                    ftxui::dim);
                    break;
                case LiveLine::Kind::MORE:
                    lines.push_back(
                        ftxui::text(std::format("            ⋯ {} more rows",
                                                line.value)) |
                        ftxui::dim);
                    break;
            }
        }
        return lines;
    }

    std::size_t Viewer::rowCount() const noexcept {
        return (frame_.bytes.size() + BYTES_PER_ROW - 1) / BYTES_PER_ROW;
    }

    std::size_t Viewer::maxTop() const noexcept {
        return rowCount() > visibleRows_ ? rowCount() - visibleRows_ : 0;
    }

    void Viewer::scrollBy(std::ptrdiff_t rows) noexcept {
        following_ = false;
        if (live_) {
            const auto target = static_cast<std::ptrdiff_t>(liveTop_) + rows;
            liveTop_ = static_cast<std::size_t>(std::clamp<std::ptrdiff_t>(
                target, 0, static_cast<std::ptrdiff_t>(liveMaxTop())));
            return;
        }
        const auto target = static_cast<std::ptrdiff_t>(top_) + rows;
        top_ = static_cast<std::size_t>(std::clamp<std::ptrdiff_t>(
            target, 0, static_cast<std::ptrdiff_t>(maxTop())));
    }

    bool Viewer::onEvent(const ftxui::Event& event) {
        const auto page = static_cast<std::ptrdiff_t>(visibleRows_);

        if (commandBar_.onEvent(event)) {
            return true;
        }

        if (event == ftxui::Event::Escape ||
            event == ftxui::Event::Character('q')) {
            onClose_();
        } else if (event == ftxui::Event::Character(' ')) {
            frozen_ = !frozen_;
        } else if (event == ftxui::Event::Character('f')) {
            following_ = !following_;
            live_ = live_ && !following_;  // follow belongs to the hex view
        } else if (live_ && event == ftxui::Event::ArrowDown) {
            scrollBy(1);
        } else if (live_ && event == ftxui::Event::ArrowUp) {
            scrollBy(-1);
        } else if (live_ && event == ftxui::Event::PageDown) {
            scrollBy(page);
        } else if (live_ && event == ftxui::Event::PageUp) {
            scrollBy(-page);
        } else if (event == ftxui::Event::ArrowDown) {
            scrollWithCursor(1);
        } else if (event == ftxui::Event::ArrowUp) {
            scrollWithCursor(-1);
        } else if (event == ftxui::Event::PageDown) {
            scrollWithCursor(page);
        } else if (event == ftxui::Event::PageUp) {
            scrollWithCursor(-page);
        } else if (!live_ && event == ftxui::Event::ArrowRight) {
            moveCursor(1);
        } else if (!live_ && event == ftxui::Event::ArrowLeft) {
            moveCursor(-1);
        } else if (event == ftxui::Event::Home) {
            toTop();
        } else if (event == ftxui::Event::End) {
            toBottom();
        } else {
            return false;
        }
        return true;
    }

    ftxui::Element Viewer::renderRow(std::size_t row) const {
        const std::size_t base = row * BYTES_PER_ROW;
        ftxui::Elements cells;
        cells.reserve(BYTES_PER_ROW + 2);
        cells.emplace_back(ftxui::text(std::format("  {:08x}  ", base)) |
                           ftxui::dim);

        for (std::size_t col = 0; col < BYTES_PER_ROW; ++col) {
            if (col == BYTES_PER_ROW / 2) {
                cells.emplace_back(ftxui::text(" "));
            }
            const std::size_t at = base + col;
            if (at >= frame_.bytes.size()) {
                break;
            }
            const std::byte value = frame_.bytes[at];
            auto hex = ftxui::text(std::format(
                           "{:02x}", std::to_integer<unsigned>(value))) |
                       ftxui::color(heatColor(frame_.heat[at], value));
            if (!live_ && at == cursor_) {
                hex = hex | ftxui::inverted;
            }
            cells.emplace_back(ftxui::hbox({hex, ftxui::text(" ")}));
        }

        return ftxui::hbox(std::move(cells));
    }

    ftxui::Element Viewer::render() {
        visibleRows_ = static_cast<std::size_t>(std::max(
            1,
            ftxui::Terminal::Size().dimy - CHROME_ROWS - commandBar_.height()));
        top_ = std::min(top_, maxTop());

        const auto name = source_ ? source_->name() : std::string_view("-");
        auto spacer =
            ftxui::emptyElement() | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 1);
        auto state =
            frozen_ ? ftxui::text(" frozen ") | ftxui::bold |
                          ftxui::color(ftxui::Color::Cyan)
                    : ftxui::text(" live ") | ftxui::color(ftxui::Color::Green);
        const auto accent = ftxui::Color::RGB(122, 162, 247);
        auto follow =
            following_ ? ftxui::text(" following ") | ftxui::color(accent)
            : live_    ? ftxui::text(" changes only ") | ftxui::color(accent)
                       : ftxui::emptyElement();
        auto title = ftxui::vbox({
            ftxui::hbox({
                spacer,
                ftxui::text(std::format(" {} ", name)) | ftxui::bold,
                ftxui::filler(),
                ftxui::text(std::format(" {} bytes · {} Hz ",
                                        frame_.bytes.size(), hz_)) |
                    ftxui::dim,
                follow,
                state,
                spacer,
            }),
            ftxui::filler(),
        });

        auto caveat = ftxui::vbox({
            ftxui::filler(),
            ftxui::hbox({
                ftxui::filler(),
                ftxui::text(" reads are unsynchronised · values may tear "
                            "mid write ") |
                    ftxui::dim,
                spacer,
            }),
        });

        const std::size_t drawn = 2 * visibleRows_;

        ftxui::Elements rows;
        if (live_) {
            buildLive();
            liveTop_ = std::min(liveTop_, liveMaxTop());
            rows = renderLive(drawn);
        } else {
            rows.reserve(drawn);
            const std::size_t end = std::min(rowCount(), top_ + drawn);
            for (std::size_t row = top_; row < end; ++row) {
                rows.push_back(renderRow(row));
            }
        }

        auto ruler = ftxui::text(
                         "  offset    00 01 02 03 04 05 06 07  "
                         "08 09 0a 0b 0c 0d 0e 0f") |
                     ftxui::dim;

        auto hex = ftxui::vbox({
            ruler,
            ftxui::vbox(std::move(rows)) | ftxui::yframe | ftxui::flex,
        });

        auto body = live_ ? hex
                          : ftxui::hbox({
                                hex | ftxui::flex,
                                ftxui::separatorLight(),
                                renderInspector(),
                            });

        auto pane = body | ftxui::borderRounded;

        return ftxui::vbox({
            ftxui::dbox({pane, title, caveat}) | ftxui::flex,
            commandBar_.render(),
        });
    }

    void Viewer::addCommands() {
        commandBar_.add(
            {.name = "jump",
             .args = "<offset>",
             .help = "scroll to a hex offset",
             .run = [this](std::string_view args) { return jump(args); }});
        commandBar_.add({.name = "live",
                         .help = "show only the rows that are changing",
                         .run =
                             [this](std::string_view) {
                                 live_ = true;
                                 following_ = false;
                                 liveTop_ = 0;
                                 return std::optional<std::string>{};
                             },
                         .available = [this] { return !live_; }});
        commandBar_.add({.name = "hex",
                         .help = "show the whole mapping again",
                         .run =
                             [this](std::string_view) {
                                 live_ = false;
                                 return std::optional<std::string>{};
                             },
                         .available = [this] { return live_; }});
        commandBar_.add({.name = "follow",
                         .help = "keep the view on the writer",
                         .run =
                             [this](std::string_view) {
                                 following_ = true;
                                 live_ = false;
                                 return std::optional<std::string>{};
                             },
                         .available = [this] { return !following_; }});
        commandBar_.add({.name = "unfollow",
                         .help = "stop tracking the writer",
                         .run =
                             [this](std::string_view) {
                                 following_ = false;
                                 return std::optional<std::string>{};
                             },
                         .available = [this] { return following_; }});
        commandBar_.add({.name = "freeze",
                         .help = "pause live updates",
                         .run =
                             [this](std::string_view) {
                                 frozen_ = true;
                                 return std::optional<std::string>{};
                             },
                         .available = [this] { return !frozen_; }});
        commandBar_.add({.name = "unfreeze",
                         .help = "resume live updates",
                         .run =
                             [this](std::string_view) {
                                 frozen_ = false;
                                 return std::optional<std::string>{};
                             },
                         .available = [this] { return frozen_; }});
        commandBar_.add({.name = "top",
                         .help = "go to the first row",
                         .run = [this](std::string_view) {
                             toTop();
                             return std::optional<std::string>{};
                         }});
        commandBar_.add({.name = "bottom",
                         .help = "go to the last row",
                         .run = [this](std::string_view) {
                             toBottom();
                             return std::optional<std::string>{};
                         }});
        commandBar_.add({.name = "close",
                         .help = "back to the launcher",
                         .run = [this](std::string_view) {
                             onClose_();
                             return std::optional<std::string>{};
                         }});
    }

    std::optional<std::string> Viewer::jump(std::string_view args) {
        const auto offset = parseOffset(args);
        if (!offset) {
            return std::format("/jump needs a hex offset, got '{}'", args);
        }
        if (*offset >= frame_.bytes.size()) {
            return std::format("0x{:x} is past the end (0x{:x} bytes)", *offset,
                               frame_.bytes.size());
        }
        live_ = false;
        goTo(*offset / BYTES_PER_ROW);
        setCursor(*offset);
        return std::nullopt;
    }

}  // namespace shmscope
