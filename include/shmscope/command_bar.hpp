#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>

namespace shmscope {

    class CommandBar {
    public:
        using RunFn =
            std::function<std::optional<std::string>(std::string_view args)>;

        struct Command {
            std::string name;    // without the slash
            std::string args{};  // hint such as "<offset>", empty if none
            std::string help{};
            RunFn run;
            std::function<bool()> available{};
        };

        void add(Command command);

        // True when the event was consumed. Only "/" is taken while closed.
        bool onEvent(const ftxui::Event& event);

        [[nodiscard]] bool isOpen() const noexcept { return open_; }

        [[nodiscard]] int height() const noexcept;

        [[nodiscard]] ftxui::Element render() const;

    private:
        [[nodiscard]] std::string_view word() const noexcept;
        [[nodiscard]] std::string_view args() const noexcept;
        [[nodiscard]] std::vector<std::size_t> matches() const;

        void close() noexcept;
        void complete();
        void execute();

        std::vector<Command> commands_;
        std::string input_;  // text after the slash
        std::string error_;
        std::size_t selected_ = 0;  // index into matches()
        bool open_ = false;
    };
}  // namespace shmscope
