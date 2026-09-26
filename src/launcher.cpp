#include "shmscope/launcher.hpp"

#include <utility>

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/screen/color.hpp>

#include "shmscope/version.hpp"

namespace shmscope {

    Launcher::Launcher(SubmitFn onSubmit, QuitFn onQuit)
        : onSubmit_(std::move(onSubmit)), onQuit_(std::move(onQuit)) {
        ftxui::InputOption option;
        option.multiline = false;
        option.placeholder = "/name";
        option.cursor_position = &cursor_;
        option.transform = [](ftxui::InputState state) {
            if (state.is_placeholder) {
                state.element |= ftxui::dim;
            }
            return state.element;
        };
        option.on_enter = [this] {
            if (!input_.empty()) {
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

        if (event.is_character() || event == ftxui::Event::Backspace) {
            error_.clear();
        }

        return false;
    }

    ftxui::Element Launcher::render() const {
        auto spacer =
            ftxui::emptyElement() | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 1);
        auto title = ftxui::vbox({
            ftxui::hbox({
                spacer,
                ftxui::text(" shmscope " SHMSCOPE_VERSION " ") | ftxui::bold,
                ftxui::filler(),
                ftxui::text(" live viewer for POSIX shared memory ") |
                    ftxui::dim,
                spacer,
            }),
            ftxui::filler(),
        });

        auto open = ftxui::hbox({
            ftxui::text("  open ›  ") | ftxui::bold | ftxui::vcenter,
            ftxui::hbox({ftxui::text(" "), inputBox_->Render() | ftxui::flex}) |
                ftxui::borderLight | ftxui::flex,
            ftxui::text("  "),
        });

        auto error = error_.empty() ? ftxui::text("")
                                    : ftxui::text("            " + error_) |
                                          ftxui::color(ftxui::Color::Red);

        auto body = ftxui::vbox({
            ftxui::text(""),
            open,
            error,
            ftxui::filler(),
        });

        auto footer = ftxui::text(" enter open · esc quit") | ftxui::dim;

        auto frame = ftxui::vbox({
                         body | ftxui::flex,
                         ftxui::separator(),
                         footer,
                     }) |
                     ftxui::borderLight;

        return ftxui::dbox({frame, title});
    }

}  // namespace shmscope
