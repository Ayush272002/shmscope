#pragma once

#include <ftxui/component/app.hpp>
#include <ftxui/component/component_base.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>

#include "launcher.hpp"

namespace shmscope {

    class Application {
    public:
        Application();

        Application(const Application&) = delete;
        Application& operator=(const Application&) = delete;
        Application(Application&&) = delete;
        Application& operator=(Application&&) = delete;
        ~Application() = default;

        [[nodiscard]] int run();

    private:
        void open(const std::string& name);

        ftxui::App terminal_;
        Launcher launcher_;
    };
}  // namespace shmscope
