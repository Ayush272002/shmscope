#include "shmscope/command_bar.hpp"

#include <algorithm>
#include <utility>

#include <ftxui/screen/color.hpp>

namespace shmscope {

    void CommandBar::add(Command command) {
        commands_.push_back(std::move(command));
    }

    std::string_view CommandBar::word() const noexcept {
        const std::string_view text = input_;
        return text.substr(0, text.find(' '));
    }

    std::string_view CommandBar::args() const noexcept {
        const std::string_view text = input_;
        const auto space = text.find(' ');
        if (space == std::string_view::npos) return {};

        const auto start = text.find_first_not_of(' ', space);
        return start == std::string_view::npos ? std::string_view{}
                                               : text.substr(start);
    }

    std::vector<std::size_t> CommandBar::matches() const {
        const bool chosen = input_.find(' ') != std::string::npos;
        std::vector<std::size_t> found;

        for (std::size_t i = 0; i < commands_.size(); ++i) {
            const std::string_view name = commands_[i].name;
            if (chosen ? name == word() : name.starts_with(word())) {
                found.push_back(i);
            }
        }
        return found;
    }

    void CommandBar::close() noexcept {
        open_ = false;
        input_.clear();
        selected_ = 0;
    }

    void CommandBar::complete() {
        const auto found = matches();
        if (found.empty()) return;

        input_ = commands_[found[std::min(selected_, found.size() - 1)]].name;
        input_ += ' ';
        selected_ = 0;
    }

    void CommandBar::execute() {
        const auto found = matches();
        if (found.empty()) {
            error_ = "unknown command /" + std::string(word());
            return;
        }

        const auto exact = std::ranges::find_if(
            found, [&](std::size_t i) { return commands_[i].name == word(); });

        const std::size_t index =
            exact != found.end() ? *exact
                                 : found[std::min(selected_, found.size() - 1)];

        const std::string arguments(args());
        close();
        if (auto failure = commands_[index].run(arguments)) {
            error_ = std::move(*failure);
        }
    }

    bool CommandBar::onEvent(const ftxui::Event& event) {
        if (!open_) {
            if (event == ftxui::Event::Character('/')) {
                open_ = true;
                error_.clear();
                return true;
            }
            return false;
        }

        if (event == ftxui::Event::Escape) {
            close();
        } else if (event == ftxui::Event::Return) {
            execute();
        } else if (event == ftxui::Event::Tab) {
            complete();
        } else if (event == ftxui::Event::ArrowUp) {
            selected_ = selected_ > 0 ? selected_ - 1 : 0;
        } else if (event == ftxui::Event::ArrowDown) {
            const auto count = matches().size();
            selected_ = count > 0 ? std::min(selected_ + 1, count - 1) : 0;
        } else if (event == ftxui::Event::Backspace) {
            if (input_.empty()) {
                close();
            } else {
                while (input_.size() > 1 &&
                       (static_cast<unsigned char>(input_.back()) & 0xC0U) ==
                           0x80U) {
                    input_.pop_back();
                }
                input_.pop_back();
                selected_ = 0;
            }
        } else if (event.is_character()) {
            input_ += event.character();
            selected_ = 0;
        } else if (event.is_mouse()) {
            return false;
        }
        return true;
    }

    int CommandBar::height() const noexcept {
        const int errorLine = error_.empty() ? 0 : 1;
        if (!open_) {
            return 1 + errorLine;
        }
        return 1 + errorLine + static_cast<int>(matches().size());
    }

    ftxui::Element CommandBar::render() const {
        ftxui::Elements lines;

        if (open_) {
            const auto found = matches();
            for (std::size_t n = 0; n < found.size(); ++n) {
                const Command& command = commands_[found[n]];
                auto line = ftxui::hbox({
                    ftxui::text("  /" + command.name) | ftxui::bold,
                    ftxui::text(command.args.empty() ? ""
                                                     : " " + command.args) |
                        ftxui::dim,
                    ftxui::filler(),
                    ftxui::text(command.help + "  ") | ftxui::dim,
                });
                if (n == std::min(selected_, found.size() - 1)) {
                    line = line | ftxui::inverted;
                }
                lines.push_back(std::move(line));
            }
        }

        if (!error_.empty()) {
            lines.push_back(ftxui::text(" " + error_) |
                            ftxui::color(ftxui::Color::Red));
        }

        if (open_) {
            lines.push_back(ftxui::hbox({
                ftxui::text(" › /") | ftxui::bold,
                ftxui::text(input_),
                ftxui::text(" ") | ftxui::inverted,
            }));
        } else {
            lines.push_back(ftxui::text(" / for commands") | ftxui::dim);
        }

        return ftxui::vbox(std::move(lines));
    }

}  // namespace shmscope
