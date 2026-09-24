#include <ftxui/component/component.hpp>
#include <shmscope/application.hpp>

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
        launcher_.setError(std::format("cannot open {} yet: no viewer", name));
    }

}  // namespace shmscope
