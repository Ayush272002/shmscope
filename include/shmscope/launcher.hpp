#pragma once

#include <ftxui/component/component_base.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <functional>
#include <string>

namespace shmscope {

    class Launcher {
    public:
        using SubmitFn = std::function<void(const std::string& name)>;
        using QuitFn = std::function<void()>;

        Launcher(SubmitFn onSubmit, QuitFn onQuit);

        Launcher(const Launcher&) = delete;
        Launcher& operator=(const Launcher&) = delete;
        Launcher(Launcher&&) = delete;
        Launcher& operator=(Launcher&&) = delete;
        ~Launcher() = default;

        [[nodiscard]] ftxui::Component component() const { return root_; }
        void setError(std::string message) { error_ = std::move(message); }

    private:
        [[nodiscard]] ftxui::Element render() const;
        bool onEvent(const ftxui::Event& event);

        SubmitFn onSubmit_;
        QuitFn onQuit_;
        std::string input_;
        std::string error_;
        int cursor_ = 0;
        ftxui::Component inputBox_;
        ftxui::Component root_;
    };
}  // namespace shmscope
