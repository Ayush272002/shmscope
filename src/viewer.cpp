#include "shmscope/viewer.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <format>
#include <string_view>
#include <utility>

#include <ftxui/component/component.hpp>
#include <ftxui/screen/color.hpp>
#include <ftxui/screen/terminal.hpp>

namespace shmscope {

    namespace {

        // Rows that are not hex, besides the command bar: two border lines,
        // the column ruler, the separator and the torn read warning.
        constexpr int CHROME_ROWS = 5;

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
        frozen_ = false;
        frame_ = source_->poll();
    }

    void Viewer::detach() noexcept {
        frame_ = {};
        source_.reset();
    }

    void Viewer::tick() noexcept {
        if (source_ && !frozen_) {
            frame_ = source_->poll();
        }
    }

    std::size_t Viewer::rowCount() const noexcept {
        return (frame_.bytes.size() + BYTES_PER_ROW - 1) / BYTES_PER_ROW;
    }

    std::size_t Viewer::maxTop() const noexcept {
        return rowCount() > visibleRows_ ? rowCount() - visibleRows_ : 0;
    }

    void Viewer::scrollBy(std::ptrdiff_t rows) noexcept {
        const auto target = static_cast<std::ptrdiff_t>(top_) + rows;
        top_ = static_cast<std::size_t>(std::clamp<std::ptrdiff_t>(
            target, 0, static_cast<std::ptrdiff_t>(maxTop())));
    }

    bool Viewer::onEvent(const ftxui::Event& event) {
        const auto page = static_cast<std::ptrdiff_t>(visibleRows_);

        if (commandBar_.onEvent(event)) {
            return true;
        }

        if (event.is_mouse()) {
            auto copy = event;
            const auto button = copy.mouse().button;
            if (button == ftxui::Mouse::WheelUp) {
                scrollBy(-WHEEL_ROWS);
                return true;
            }
            if (button == ftxui::Mouse::WheelDown) {
                scrollBy(WHEEL_ROWS);
                return true;
            }
            return false;
        }

        if (event == ftxui::Event::Escape ||
            event == ftxui::Event::Character('q')) {
            onClose_();
        } else if (event == ftxui::Event::Character(' ')) {
            frozen_ = !frozen_;
        } else if (event == ftxui::Event::ArrowDown ||
                   event == ftxui::Event::Character('j')) {
            scrollBy(1);
        } else if (event == ftxui::Event::ArrowUp ||
                   event == ftxui::Event::Character('k')) {
            scrollBy(-1);
        } else if (event == ftxui::Event::PageDown) {
            scrollBy(page);
        } else if (event == ftxui::Event::PageUp) {
            scrollBy(-page);
        } else if (event == ftxui::Event::Character('g') ||
                   event == ftxui::Event::Home) {
            top_ = 0;
        } else if (event == ftxui::Event::Character('G') ||
                   event == ftxui::Event::End) {
            top_ = maxTop();
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
            cells.emplace_back(
                ftxui::text(
                    std::format("{:02x} ", std::to_integer<unsigned>(value))) |
                ftxui::color(heatColor(frame_.heat[at], value)));
        }

        return ftxui::hbox(std::move(cells));
    }

    ftxui::Element Viewer::render() {
        // Only rows on screen are built; a 1 MiB object is 65536 rows.
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
        auto title = ftxui::vbox({
            ftxui::hbox({
                spacer,
                ftxui::text(std::format(" {} ", name)) | ftxui::bold,
                ftxui::filler(),
                ftxui::text(std::format(" {} bytes · {} Hz ",
                                        frame_.bytes.size(), hz_)) |
                    ftxui::dim,
                state,
                spacer,
            }),
            ftxui::filler(),
        });

        ftxui::Elements rows;
        rows.reserve(visibleRows_);
        const std::size_t end = std::min(rowCount(), top_ + visibleRows_);
        for (std::size_t row = top_; row < end; ++row) {
            rows.push_back(renderRow(row));
        }

        auto ruler = ftxui::text(
                         "  offset    00 01 02 03 04 05 06 07  "
                         "08 09 0a 0b 0c 0d 0e 0f") |
                     ftxui::dim;

        auto footer = ftxui::vbox({
            ftxui::text(
                " reads are unsynchronised; a value may be torn mid write") |
                ftxui::dim,
            commandBar_.render(),
        });

        auto frame = ftxui::vbox({
                         ruler,
                         ftxui::vbox(std::move(rows)) | ftxui::flex,
                         ftxui::separator(),
                         footer,
                     }) |
                     ftxui::borderLight;

        return ftxui::dbox({frame, title});
    }

    void Viewer::addCommands() {
        commandBar_.add(
            {.name = "jump",
             .args = "<offset>",
             .help = "scroll to a hex offset",
             .run = [this](std::string_view args) { return jump(args); }});
        commandBar_.add({.name = "freeze",
                         .help = "pause or resume live updates",
                         .run = [this](std::string_view) {
                             frozen_ = !frozen_;
                             return std::optional<std::string>{};
                         }});
        commandBar_.add({.name = "top",
                         .help = "go to offset 0",
                         .run = [this](std::string_view) {
                             top_ = 0;
                             return std::optional<std::string>{};
                         }});
        commandBar_.add({.name = "bottom",
                         .help = "go to the last row",
                         .run = [this](std::string_view) {
                             top_ = maxTop();
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
        top_ = std::min(*offset / BYTES_PER_ROW, maxTop());
        return std::nullopt;
    }

}  // namespace shmscope
