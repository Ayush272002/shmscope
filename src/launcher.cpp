#include "shmscope/launcher.hpp"

#include <sys/utsname.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>
#include <numbers>
#include <utility>

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/dom/canvas.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>

#include "shmscope/version.hpp"

namespace shmscope {

    namespace {

        constexpr int SCOPE_WIDTH = 52;
        constexpr int SCOPE_HEIGHT = 20;
        constexpr int GRID_STEP = 4;
        constexpr double CYCLES = 2.0;
        constexpr double PHASE_STEP = 0.12;

        const auto ACCENT = ftxui::Color::RGB(122, 162, 247);
        const auto MUTED = ftxui::Color::RGB(110, 110, 110);
        const auto GRID = ftxui::Color::RGB(70, 70, 70);
        const auto TRACE_START = ftxui::Color::RGB(230, 60, 60);
        const auto TRACE_END = ftxui::Color::RGB(255, 200, 0);

        std::string platformName() {
            utsname info{};
            if (::uname(&info) != 0) {
                return "unknown";
            }
            std::string system = info.sysname;
            std::ranges::transform(system, system.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return system + " " + info.machine;
        }

    }  // namespace

    Launcher::Launcher(SubmitFn onSubmit, QuitFn onQuit, int hz,
                       const RecentList* recent)
        : onSubmit_(std::move(onSubmit)),
          onQuit_(std::move(onQuit)),
          hz_(hz),
          recent_(recent),
          platform_(platformName()) {
        ftxui::InputOption option;
        option.multiline = false;
        option.placeholder = "shared memory name, e.g. /something.shm";
        option.cursor_position = &cursor_;
        option.transform = [](ftxui::InputState state) {
            if (state.is_placeholder) {
                state.element |= ftxui::color(MUTED);
            }
            return state.element;
        };

        option.on_enter = [this] {
            if (!input_.empty()) {
                selected_ = -1;
                onSubmit_(input_);
            }
        };

        inputBox_ = ftxui::Input(&input_, option);

        root_ = ftxui::Renderer(inputBox_, [this] { return render(); }) |
                ftxui::CatchEvent([this](const ftxui::Event& event) {
                    return onEvent(event);
                });
    }

    bool Launcher::onEvent(const ftxui::Event& event) {
        if (event == ftxui::Event::Escape) {
            onQuit_();
            return true;
        }

        if (event == ftxui::Event::ArrowDown) {
            cycleRecent(1);
            return true;
        }

        if (event == ftxui::Event::ArrowUp) {
            cycleRecent(-1);
            return true;
        }

        if (event.is_character() || event == ftxui::Event::Backspace) {
            error_.clear();
            selected_ = -1;
        }

        return false;
    }

    bool Launcher::hasRecent() const noexcept {
        return recent_ != nullptr && !recent_->entries().empty();
    }

    void Launcher::cycleRecent(int step) {
        if (!hasRecent()) {
            return;
        }
        const auto entries = recent_->entries();
        const int last =
            std::min(static_cast<int>(entries.size()), RECENT_SHOWN) - 1;
        selected_ = std::clamp(selected_ + step, -1, last);
        input_ = selected_ < 0 ? std::string()
                               : entries[static_cast<std::size_t>(selected_)];
        cursor_ = static_cast<int>(input_.size());
        error_.clear();
    }

    ftxui::Element Launcher::renderScope() const {
        auto canvas = ftxui::Canvas(SCOPE_WIDTH, SCOPE_HEIGHT);

        for (int x = 0; x < SCOPE_WIDTH; x += GRID_STEP) {
            for (int y = 0; y < SCOPE_HEIGHT; y += GRID_STEP) {
                canvas.DrawPoint(x, y, true, GRID);
            }
        }

        const double shift = static_cast<double>(phase_) * PHASE_STEP;
        constexpr double middle = (SCOPE_HEIGHT - 1) / 2.0;
        constexpr double amplitude = (SCOPE_HEIGHT / 2.0) - 3.0;

        int previousX = 0;
        int previousY = 0;
        for (int x = 0; x < SCOPE_WIDTH; ++x) {
            const double along =
                static_cast<double>(x) / static_cast<double>(SCOPE_WIDTH);
            const double angle =
                (along * CYCLES * 2.0 * std::numbers::pi) + shift;
            const int y = static_cast<int>(
                std::lround(middle - (amplitude * std::sin(angle))));
            if (x > 0) {
                const float t =
                    static_cast<float>(x) / static_cast<float>(SCOPE_WIDTH - 1);
                canvas.DrawPointLine(
                    previousX, previousY, x, y,
                    ftxui::Color::Interpolate(t, TRACE_START, TRACE_END));
            }
            previousX = x;
            previousY = y;
        }

        return ftxui::canvas(std::move(canvas)) |
               ftxui::borderStyled(ftxui::ROUNDED, MUTED);
    }

    ftxui::Element Launcher::renderWelcome() const {
        auto details = ftxui::vbox({
            ftxui::text(""),
            ftxui::text("shmscope v" SHMSCOPE_VERSION) | ftxui::bold,
            ftxui::text("live viewer for POSIX shared memory") |
                ftxui::color(MUTED),
            ftxui::text(""),
            ftxui::text(std::format("{} · {} Hz · read only", platform_, hz_)) |
                ftxui::color(MUTED),
        });

        return ftxui::hbox({
                   ftxui::text(" "),
                   renderScope(),
                   ftxui::text("   "),
                   details,
                   ftxui::filler(),
               }) |
               ftxui::borderStyled(ftxui::ROUNDED, ACCENT);
    }

    ftxui::Element Launcher::renderTips() {
        auto tip = [](std::string_view number, std::string_view text) {
            return ftxui::hbox({
                ftxui::text(std::format("  {} ", number)) | ftxui::color(MUTED),
                ftxui::text(std::string(text)),
            });
        };

        return ftxui::vbox({
            ftxui::text(" Tips for getting started") | ftxui::bold,
            tip("1.",
                "Type the name of a shared memory object and press enter"),
            tip("2.", "Press / in the viewer for commands"),
            tip("3.",
                "/live lists only the rows that change, /follow tracks "
                "the writer"),
            tip("4.", "The mapping is read only, shmscope never writes"),
        });
    }

    ftxui::Element Launcher::renderRecent() const {
        if (!hasRecent()) {
            return ftxui::emptyElement();
        }

        ftxui::Elements lines;
        lines.push_back(ftxui::text(" Recent") | ftxui::bold);

        const auto entries = recent_->entries();
        const auto shown =
            std::min(entries.size(), static_cast<std::size_t>(RECENT_SHOWN));
        for (std::size_t i = 0; i < shown; ++i) {
            const bool selected = static_cast<int>(i) == selected_;
            lines.push_back(ftxui::hbox({
                ftxui::text(selected ? "  › " : "    ") | ftxui::color(ACCENT),
                ftxui::text(entries[i]) |
                    ftxui::color(selected ? ACCENT : ftxui::Color::Default),
            }));
        }
        lines.push_back(ftxui::text(""));

        return ftxui::vbox(std::move(lines));
    }

    ftxui::Element Launcher::render() const {
        auto prompt = ftxui::hbox({
                          ftxui::text(" › ") | ftxui::color(ACCENT),
                          inputBox_->Render() | ftxui::xflex,
                      }) |
                      ftxui::borderStyled(ftxui::ROUNDED, MUTED);

        auto error = error_.empty() ? ftxui::emptyElement()
                                    : ftxui::text("   " + error_) |
                                          ftxui::color(ftxui::Color::Red);

        const auto* keys = hasRecent() ? "   ↑↓ recent · enter open · esc quit"
                                       : "   enter open · esc quit";

        return ftxui::vbox({
            renderWelcome() | ftxui::yflex_shrink,
            ftxui::text("") | ftxui::yflex_shrink,
            renderRecent() | ftxui::yflex_shrink,
            renderTips() | ftxui::yflex_shrink,
            ftxui::filler(),
            prompt,
            error,
            ftxui::text(keys) | ftxui::color(MUTED),
        });
    }

}  // namespace shmscope
