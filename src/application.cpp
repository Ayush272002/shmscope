#include <ftxui/component/component.hpp>
#include <shmscope/application.hpp>
#include <shmscope/version.hpp>

namespace shmscope {

    Application::Application() : terminal_(ftxui::App::Fullscreen()) {
        root_ = ftxui::Renderer([this](bool) { return render(); }) |
                ftxui::CatchEvent([this](const ftxui::Event& event) {
                    return onEvent(event);
                });
    }

    int Application::run() {
        terminal_.Loop(root_);
        return 0;
    }

    bool Application::onEvent(const ftxui::Event& event) {
        if (event == ftxui::Event::Escape ||
            event == ftxui::Event::Character('q')) {
            terminal_.Exit();
            return true;
        }

        return false;
    }

    ftxui::Element Application::render() const {
        return ftxui::vbox({
                   ftxui::hbox({
                       ftxui::text(" shmscope " SHMSCOPE_VERSION) | ftxui::bold,
                       ftxui::filler(),
                       ftxui::text("live viewer for POSIX shared memory ") |
                           ftxui::dim,
                   }),
                   ftxui::separator(),
                   ftxui::filler(),
                   ftxui::text("nothing here yet") | ftxui::dim |
                       ftxui::hcenter,
                   ftxui::filler(),
                   ftxui::separator(),
                   ftxui::text(" q quit") | ftxui::dim,
               }) |
               ftxui::border;
    }
}  // namespace shmscope
