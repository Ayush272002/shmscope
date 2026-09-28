#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <utility>

#include <ftxui/component/component_base.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>

#include "shmscope/ui/recent.hpp"

namespace shmscope {

    class Launcher {
    public:
        using SubmitFn = std::function<void(const std::string& name)>;
        using QuitFn = std::function<void()>;

        Launcher(SubmitFn onSubmit, QuitFn onQuit, int hz = 15,
                 const RecentList* recent = nullptr);

        Launcher(const Launcher&) = delete;
        Launcher& operator=(const Launcher&) = delete;
        Launcher(Launcher&&) = delete;
        Launcher& operator=(Launcher&&) = delete;
        ~Launcher() = default;

        [[nodiscard]] ftxui::Component component() const { return root_; }
        void setError(std::string message) { error_ = std::move(message); }
        void tick() noexcept { ++phase_; }

    private:
        [[nodiscard]] ftxui::Element render() const;
        [[nodiscard]] ftxui::Element renderScope() const;
        [[nodiscard]] ftxui::Element renderWelcome() const;
        [[nodiscard]] static ftxui::Element renderTips();
        [[nodiscard]] ftxui::Element renderRecent() const;
        [[nodiscard]] bool hasRecent() const noexcept;
        bool onEvent(const ftxui::Event& event);
        void cycleRecent(int step);

        static constexpr int RECENT_SHOWN = 5;

        SubmitFn onSubmit_;
        QuitFn onQuit_;
        int hz_;
        const RecentList* recent_;
        int selected_ = -1;
        std::string platform_;
        std::string input_;
        std::string error_;
        int cursor_ = 0;
        std::uint32_t phase_ = 0;
        ftxui::Component inputBox_;
        ftxui::Component root_;
    };
}  // namespace shmscope
