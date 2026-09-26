#include "shmscope/application.hpp"

#include <format>

#include <ftxui/component/component.hpp>

#include "shmscope/shm_source.hpp"

namespace shmscope {

    Application::Application()
        : terminal_(ftxui::App::Fullscreen()),
          launcher_([this](const std::string& name) { open(name); },
                    [this] { terminal_.Exit(); }) {}

    int Application::run() {
        terminal_.Loop(launcher_.component());
        return 0;
    }

    void Application::open(const std::string& name) {
        auto source = ShmSource::open(name);
        if (!source) {
            launcher_.setError(source.error());
            return;
        }

        const auto frame = (*source)->poll();
        launcher_.setError(std::format("{}: {} bytes mapped, viewer next",
                                       (*source)->name(), frame.bytes.size()));
    }

}  // namespace shmscope
