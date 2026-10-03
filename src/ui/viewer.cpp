#include "shmscope/ui/viewer.hpp"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <filesystem>
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

#include "shmscope/core/decode.hpp"
#include "shmscope/core/source.hpp"
#include "shmscope/ui/field_panel.hpp"
#include "shmscope/ui/paths.hpp"

namespace shmscope {

    namespace {

        constexpr int CHROME_ROWS = 3;
        const auto FIELD_BACKGROUND = ftxui::Color::RGB(45, 55, 85);

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

    Viewer::Viewer(int hz, CloseFn onClose, LayoutLoader loader,
                   SourceOpener opener)
        : hz_(hz),
          onClose_(std::move(onClose)),
          loader_(std::move(loader)),
          opener_(std::move(opener)) {
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
        tracking_.reset();
        panelTop_.reset();
        frame_ = source_->poll();
        updatePlacement();
    }

    void Viewer::detach() noexcept {
        frame_ = {};
        placement_ = {};
        tracking_.reset();
        panelTop_.reset();
        source_.reset();
    }

    void Viewer::setLayout(std::optional<Layout> layout) {
        layout_ = std::move(layout);
        tracking_.reset();
        panelTop_.reset();
        updatePlacement();
    }

    std::optional<std::string> Viewer::loadLayout(
        const std::filesystem::path& file) {
        if (!loader_) return std::string("loading layouts is not available");

        auto layout = loader_(file);
        if (!layout) return std::move(layout.error());

        std::error_code error;
        auto absolute = std::filesystem::absolute(file, error);
        if (error) absolute = file;

        setLayout(std::move(*layout));
        layoutFile_ = std::move(absolute);
        return std::nullopt;
    }

    std::optional<std::string> Viewer::reopen() {
        if (!source_) return std::string("nothing is open");
        if (!opener_) return std::string("reopening is not available");

        auto source = opener_(std::string(source_->name()));
        if (!source) return std::move(source.error());

        const auto tracking = tracking_;
        const auto cursor = cursor_;
        attach(std::move(*source));
        tracking_ = tracking;
        setCursor(cursor);
        track();
        return std::nullopt;
    }

    std::optional<std::string> Viewer::layoutCommand(std::string_view args) {
        while (args.ends_with(' ')) args.remove_suffix(1);

        if (args == "off") {
            if (!layout_) return std::string("no layout is loaded");

            setLayout(std::nullopt);
            layoutFile_.reset();
            return std::nullopt;
        }
        if (args == "reload") {
            if (!layoutFile_) return std::string("no layout file to reload");

            const auto file = *layoutFile_;
            return loadLayout(file);
        }
        return loadLayout(expandHome(args));
    }

    void Viewer::track() noexcept {
        if (!tracking_) return;

        const auto* field = placement_.find(*tracking_);
        if (field == nullptr || field->offset >= frame_.bytes.size()) return;

        const std::size_t row = field->offset / BYTES_PER_ROW;
        if (row < top_ || row >= top_ + visibleRows_) goTo(row);
        setCursor(field->offset);
    }

    void Viewer::updatePlacement() {
        if (!layout_ || frame_.bytes.empty()) {
            placement_ = {};
            return;
        }
        placement_ = place(*layout_, frame_.bytes);
    }

    void Viewer::tick() noexcept {
        if (source_ && !frozen_) {
            frame_ = source_->poll();
            if (!frame_.bytes.empty() && cursor_ >= frame_.bytes.size()) {
                cursor_ = frame_.bytes.size() - 1;
            }
            if (following_) follow();
            updatePlacement();
            track();
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
        tracking_.reset();
        panelTop_.reset();
        if (live_) {
            liveTop_ = 0;
        } else {
            setCursor(0);
        }
    }

    void Viewer::toBottom() {
        tracking_.reset();
        panelTop_.reset();
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
        tracking_.reset();
        panelTop_.reset();
        const auto target = static_cast<std::ptrdiff_t>(cursor_) + bytes;
        setCursor(
            static_cast<std::size_t>(std::max<std::ptrdiff_t>(target, 0)));
    }

    void Viewer::scrollWithCursor(std::ptrdiff_t rows) noexcept {
        tracking_.reset();
        panelTop_.reset();
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
               ftxui::size(ftxui::WIDTH, ftxui::GREATER_THAN, INSPECTOR_WIDTH);
    }

    ftxui::Element Viewer::renderSide() {
        if (!layout_ || !showFields_) return renderInspector();

        const auto count = placement_.fields.size();
        const auto capacity = panelCapacity(placement_, visibleRows_);
        shownPanelTop_ = panelTop_ ? clampTop(*panelTop_, capacity, count)
                         : selected_
                             ? centredTop(*selected_, capacity, count)
                             : clampTop(shownPanelTop_, capacity, count);

        return renderFieldPanel(*layout_, placement_, frame_.bytes, selected_,
                                visibleRows_, shownPanelTop_) |
               ftxui::size(ftxui::WIDTH, ftxui::GREATER_THAN,
                           FIELD_PANEL_WIDTH) |
               ftxui::xflex | ftxui::reflect(sideBox_);
    }

    bool Viewer::onMouse(ftxui::Event event) {
        const auto& mouse = event.mouse();
        const bool overPanel = layout_ && showFields_ && !live_ &&
                               sideBox_.Contain(mouse.x, mouse.y);

        if (mouse.button == ftxui::Mouse::WheelUp ||
            mouse.button == ftxui::Mouse::WheelDown) {
            const bool up = mouse.button == ftxui::Mouse::WheelUp;
            if (overPanel) {
                scrollPanel(up ? -1 : 1);
                return true;
            }
            return onEvent(up ? ftxui::Event::ArrowUp
                              : ftxui::Event::ArrowDown);
        }

        if (mouse.button != ftxui::Mouse::Left ||
            mouse.motion != ftxui::Mouse::Pressed) {
            return false;
        }
        if (overPanel) return clickPanel(mouse.y);
        if (!live_ && hexBox_.Contain(mouse.x, mouse.y)) {
            return clickHex(mouse.x, mouse.y);
        }
        return false;
    }

    void Viewer::scrollPanel(const std::ptrdiff_t rows) noexcept {
        const auto count = placement_.fields.size();
        const auto capacity = panelCapacity(placement_, visibleRows_);
        const auto last = static_cast<std::ptrdiff_t>(
            count > capacity ? count - capacity : 0);
        const auto target = std::clamp<std::ptrdiff_t>(
            static_cast<std::ptrdiff_t>(shownPanelTop_) + rows, 0, last);

        shownPanelTop_ = static_cast<std::size_t>(target);
        panelTop_ = shownPanelTop_;
    }

    bool Viewer::clickPanel(const int y) {
        const int line =
            y - sideBox_.y_min - static_cast<int>(PANEL_HEADER_LINES);
        if (line < 0) return false;

        const auto capacity = panelCapacity(placement_, visibleRows_);
        const auto index = shownPanelTop_ + static_cast<std::size_t>(line);
        if (static_cast<std::size_t>(line) >= capacity ||
            index >= placement_.fields.size()) {
            return false;
        }

        const auto offset = placement_.fields[index].offset;
        if (offset >= frame_.bytes.size()) return true;

        tracking_.reset();
        panelTop_ = shownPanelTop_;
        const std::size_t row = offset / BYTES_PER_ROW;
        if (row < top_ || row >= top_ + visibleRows_) goTo(row);
        setCursor(offset);
        return true;
    }

    bool Viewer::clickHex(const int x, const int y) {
        constexpr int LABEL_WIDTH = 12;
        constexpr int CELL_WIDTH = 3;
        constexpr int HALF = static_cast<int>(BYTES_PER_ROW / 2) * CELL_WIDTH;

        const int line = y - hexBox_.y_min - 1;
        int column = x - hexBox_.x_min - LABEL_WIDTH;
        if (line < 0 || column < 0 || column == HALF) return false;
        if (column > HALF) --column;

        const auto col = static_cast<std::size_t>(column / CELL_WIDTH);
        if (col >= BYTES_PER_ROW) return false;

        const auto offset =
            (top_ + static_cast<std::size_t>(line)) * BYTES_PER_ROW + col;
        if (offset >= frame_.bytes.size()) return false;

        tracking_.reset();
        panelTop_.reset();
        setCursor(offset);
        return true;
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

        if (event.is_mouse()) return onMouse(event);

        if (event == ftxui::Event::Escape ||
            event == ftxui::Event::Character('q')) {
            onClose_();
        } else if (event == ftxui::Event::Character(' ')) {
            frozen_ = !frozen_;
        } else if (layout_ && event == ftxui::Event::Character('i')) {
            showFields_ = !showFields_;
        } else if (event == ftxui::Event::Character('f')) {
            tracking_.reset();
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
            auto gap = ftxui::text(" ");
            if (inSelectedField(at)) {
                hex = hex | ftxui::bgcolor(FIELD_BACKGROUND);
                if (col + 1 != BYTES_PER_ROW / 2 && col + 1 != BYTES_PER_ROW &&
                    inSelectedField(at + 1)) {
                    gap = gap | ftxui::bgcolor(FIELD_BACKGROUND);
                }
            }
            if (!live_ && at == cursor_) {
                hex = hex | ftxui::inverted;
            }
            cells.emplace_back(ftxui::hbox({hex, gap}));
        }

        return ftxui::hbox(std::move(cells));
    }

    ftxui::Element Viewer::render() {
        commandBar_.setWidth(ftxui::Terminal::Size().dimx);
        visibleRows_ = static_cast<std::size_t>(std::max(
            1,
            ftxui::Terminal::Size().dimy - CHROME_ROWS - commandBar_.height()));
        top_ = std::min(top_, maxTop());
        selected_ =
            layout_ && !live_ ? fieldAt(placement_, cursor_) : std::nullopt;

        const auto name = source_ ? source_->name() : std::string_view("-");
        auto spacer =
            ftxui::emptyElement() | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 1);
        auto state =
            frozen_ ? ftxui::text(" frozen ") | ftxui::bold |
                          ftxui::color(ftxui::Color::Cyan)
                    : ftxui::text(" live ") | ftxui::color(ftxui::Color::Green);
        const auto accent = ftxui::Color::RGB(122, 162, 247);
        auto layoutState = ftxui::emptyElement();
        if (layout_) {
            const auto fields = placement_.fields.size();
            const auto problems = placement_.problems.size();
            layoutState = ftxui::hbox({
                ftxui::text(std::format(" {} · {} {} ", layout_->id, fields,
                                        fields == 1 ? "field" : "fields")) |
                    ftxui::color(accent),
                problems == 0 ? ftxui::emptyElement()
                              : ftxui::text(std::format(
                                    "· {} {} ", problems,
                                    problems == 1 ? "problem" : "problems")) |
                                    ftxui::color(ftxui::Color::Yellow),
            });
        }
        auto follow =
            tracking_ ? ftxui::text(std::format(" tracking {} ", *tracking_)) |
                            ftxui::color(accent)
            : following_ ? ftxui::text(" following ") | ftxui::color(accent)
            : live_      ? ftxui::text(" changes only ") | ftxui::color(accent)
                         : ftxui::emptyElement();
        const auto health = source_ ? source_->state() : SourceState::LIVE;
        auto segment = health == SourceState::REMOVED
                           ? ftxui::text(" removed · showing the last data ") |
                                 ftxui::color(ftxui::Color::Yellow)
                       : health == SourceState::REPLACED
                           ? ftxui::text(" recreated · /reopen ") |
                                 ftxui::bold |
                                 ftxui::color(ftxui::Color::Yellow)
                           : ftxui::emptyElement();
        auto title = ftxui::vbox({
            ftxui::hbox({
                spacer,
                ftxui::text(std::format(" {} ", name)) | ftxui::bold,
                ftxui::filler(),
                ftxui::text(std::format(" {} bytes · {} Hz ",
                                        frame_.bytes.size(), hz_)) |
                    ftxui::dim,
                layoutState,
                follow,
                segment,
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

        auto hex =
            ftxui::vbox({
                ruler,
                ftxui::vbox(std::move(rows)) | ftxui::yframe | ftxui::flex,
            }) |
            ftxui::reflect(hexBox_);

        auto body =
            live_
                ? hex
                : ftxui::hbox({
                      hex | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, HEX_WIDTH),
                      ftxui::separatorLight(),
                      renderSide() | ftxui::xflex,
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
                                 tracking_.reset();
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
                                 tracking_.reset();
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
        commandBar_.add(
            {.name = "layout",
             .args = "<file|off|reload>",
             .help = "load, remove or reload a layout",
             .run =
                 [this](std::string_view args) { return layoutCommand(args); },
             .suggest =
                 [](std::string_view partial) {
                     auto choices = completePath(partial, LAYOUT_EXTENSIONS);
                     for (const std::string_view word : {"off", "reload"}) {
                         if (!partial.empty() && word.starts_with(partial)) {
                             choices.emplace_back(word);
                         }
                     }
                     return choices;
                 }});
        commandBar_.add(
            {.name = "fields",
             .help = "show the layout's fields beside the bytes",
             .run =
                 [this](std::string_view) {
                     showFields_ = true;
                     return std::optional<std::string>{};
                 },
             .available = [this] { return layout_ && !showFields_; }});
        commandBar_.add(
            {.name = "inspector",
             .help = "show every type decoded at the cursor",
             .run =
                 [this](std::string_view) {
                     showFields_ = false;
                     return std::optional<std::string>{};
                 },
             .available = [this] { return layout_ && showFields_; }});
        commandBar_.add(
            {.name = "field",
             .args = "<path>",
             .help = "track a field as it moves, e.g. latest.sequence",
             .run = [this](std::string_view args) { return jumpToField(args); },
             .available = [this] { return layout_.has_value(); }});
        commandBar_.add(
            {.name = "untrack",
             .help = "stop tracking the field",
             .run =
                 [this](std::string_view) {
                     tracking_.reset();
                     return std::optional<std::string>{};
                 },
             .available = [this] { return tracking_.has_value(); }});
        commandBar_.add(
            {.name = "reopen",
             .help = "open whatever the name points at now",
             .run = [this](std::string_view) { return reopen(); },
             .available =
                 [this] { return source_ != nullptr && opener_ != nullptr; }});
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
        tracking_.reset();
        panelTop_.reset();
        goTo(*offset / BYTES_PER_ROW);
        setCursor(*offset);
        return std::nullopt;
    }

    std::optional<std::string> Viewer::jumpToField(std::string_view path) {
        if (!layout_) {
            return std::string("/field needs a layout; start with --layout");
        }
        while (path.ends_with(' ')) path.remove_suffix(1);

        if (path.empty()) {
            return std::string("/field needs a field path, e.g. header.count");
        }
        const auto* field = placement_.find(path);
        if (field == nullptr) {
            return std::format("no field '{}' is placed", path);
        }
        if (field->offset >= frame_.bytes.size()) {
            return std::format("'{}' starts past the end (0x{:x} bytes)", path,
                               frame_.bytes.size());
        }
        live_ = false;
        goTo(field->offset / BYTES_PER_ROW);
        setCursor(field->offset);
        tracking_ = std::string(path);
        panelTop_.reset();
        return std::nullopt;
    }

    bool Viewer::inSelectedField(const std::size_t offset) const noexcept {
        if (!selected_ || *selected_ >= placement_.fields.size()) return false;

        const auto& field = placement_.fields[*selected_];
        return offset >= field.offset && offset - field.offset < field.size;
    }

}  // namespace shmscope
