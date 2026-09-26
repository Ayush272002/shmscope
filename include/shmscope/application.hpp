#pragma once

#include <string>

#include <ftxui/component/app.hpp>

#include "shmscope/launcher.hpp"
#include "shmscope/viewer.hpp"

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
        static constexpr int LAUNCHER = 0;
        static constexpr int VIEWER = 1;
        static constexpr int REFRESH_HZ = 15;

        void open(const std::string& name);
        void close();

        ftxui::App terminal_;
        int active_ = LAUNCHER;
        Launcher launcher_;
        Viewer viewer_;
    };
}  // namespace shmscope
