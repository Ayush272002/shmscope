#pragma once

#include <ftxui/component/app.hpp>
#include <ftxui/component/component_base.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>

namespace shmscope {

    class Application {
    public:
        Application();

        // Callbacks capture this, so it must not move.
        Application(const Application&) = delete;
        Application& operator=(const Application&) = delete;
        Application(Application&&) = delete;
        Application& operator=(Application&&) = delete;
        ~Application() = default;

        [[nodiscard]] int run();

    private:
        [[nodiscard]] ftxui::Element render() const;
        bool onEvent(const ftxui::Event& event);

        ftxui::App terminal_;
        ftxui::Component root_;
    };
}  // namespace shmscope
