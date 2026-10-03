#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include <ftxui/component/app.hpp>

#include "shmscope/ui/launcher.hpp"
#include "shmscope/ui/recent.hpp"
#include "shmscope/ui/viewer.hpp"

namespace shmscope {

    struct Options {
        std::optional<std::string> name{};
        std::optional<std::filesystem::path> layout{};
        int hz = 15;
    };

    class Application {
    public:
        explicit Application(Options options);

        Application(const Application&) = delete;
        Application& operator=(const Application&) = delete;
        Application(Application&&) = delete;
        Application& operator=(Application&&) = delete;
        ~Application() = default;

        [[nodiscard]] int run();

    private:
        static constexpr int LAUNCHER = 0;
        static constexpr int VIEWER = 1;

        [[nodiscard]] std::optional<std::string> open(const std::string& name);
        void close();

        Options options_;
        RecentList recent_;
        ftxui::App terminal_;
        int active_ = LAUNCHER;
        Launcher launcher_;
        Viewer viewer_;
    };
}  // namespace shmscope
