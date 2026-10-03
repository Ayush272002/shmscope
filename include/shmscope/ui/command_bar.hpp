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
        using SuggestFn =
            std::function<std::vector<std::string>(std::string_view partial)>;

        static constexpr std::size_t MAX_CHOICES_SHOWN = 8;

        struct Command {
            std::string name;    // without the slash
            std::string args{};  // hint such as "<offset>", empty if none
            std::string help{};
            RunFn run;
            std::function<bool()> available{};
            SuggestFn suggest{};
        };

        void add(Command command);

        // True when the event was consumed. Only "/" is taken while closed.
        bool onEvent(const ftxui::Event& event);

        [[nodiscard]] bool isOpen() const noexcept { return open_; }

        void setWidth(int width) noexcept { width_ = width; }

        [[nodiscard]] int height() const;

        [[nodiscard]] ftxui::Element render() const;

    private:
        [[nodiscard]] std::string_view word() const noexcept;
        [[nodiscard]] std::string_view args() const noexcept;
        [[nodiscard]] std::vector<std::size_t> matches() const;

        void close() noexcept;
        void complete();
        void completeArgs();
        void execute();
        [[nodiscard]] std::size_t choiceLines() const noexcept;
        [[nodiscard]] std::vector<std::string> errorLines() const;

        std::vector<Command> commands_;
        std::vector<std::string> choices_;
        std::string input_;  // text after the slash
        std::string error_;
        std::size_t selected_ = 0;  // index into matches()
        int width_ = 80;
        bool open_ = false;
    };
}  // namespace shmscope
