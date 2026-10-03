#include "shmscope/ui/command_bar.hpp"

#include <algorithm>
#include <cstddef>
#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>

#include "shmscope/ui/paths.hpp"

namespace shmscope {

    namespace {

        constexpr std::size_t ERROR_INDENT = 3;

        std::vector<std::string> wrap(const std::string_view text,
                                      const std::size_t width) {
            std::vector<std::string> lines;
            std::string line;
            std::size_t start = 0;

            while (start < text.size()) {
                const auto space = text.find(' ', start);
                auto word = text.substr(start, space == std::string_view::npos
                                                   ? std::string_view::npos
                                                   : space - start);
                start =
                    space == std::string_view::npos ? text.size() : space + 1;

                while (!word.empty()) {
                    const auto room =
                        line.empty() ? width : width - line.size() - 1;
                    if (word.size() <= room) {
                        if (!line.empty()) line += ' ';
                        line += word;
                        word = {};
                    } else if (!line.empty()) {
                        lines.push_back(std::move(line));
                        line.clear();
                    } else {
                        lines.emplace_back(word.substr(0, width));
                        word.remove_prefix(width);
                    }
                }
            }
            if (!line.empty() || lines.empty())
                lines.push_back(std::move(line));

            return lines;
        }

    }  // namespace

    std::vector<std::string> CommandBar::errorLines() const {
        if (error_.empty()) return {};

        const auto usable =
            width_ > static_cast<int>(ERROR_INDENT) + 1
                ? static_cast<std::size_t>(width_) - ERROR_INDENT
                : std::size_t{1};
        return wrap(error_, usable);
    }

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
            const Command& command = commands_[i];
            if (command.available && !command.available()) {
                continue;
            }
            const std::string_view name = command.name;
            if (chosen ? name == word() : name.starts_with(word())) {
                found.push_back(i);
            }
        }
        return found;
    }

    void CommandBar::close() noexcept {
        open_ = false;
        input_.clear();
        choices_.clear();
        selected_ = 0;
    }

    void CommandBar::completeArgs() {
        choices_.clear();

        const auto found = matches();
        if (found.size() != 1) return;

        const Command& command = commands_[found.front()];
        if (!command.suggest) return;

        const auto partial = args();
        auto choices = command.suggest(partial);
        if (choices.empty()) return;

        const auto prefix = commonPrefix(choices);
        if (prefix.size() > partial.size()) {
            input_ = command.name + ' ' + prefix;
        }
        if (choices.size() > 1) choices_ = std::move(choices);
    }

    std::size_t CommandBar::choiceLines() const noexcept {
        if (choices_.size() <= MAX_CHOICES_SHOWN) return choices_.size();

        return MAX_CHOICES_SHOWN + 1;
    }

    void CommandBar::complete() {
        if (input_.find(' ') != std::string::npos) {
            completeArgs();
            return;
        }

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

        if (!commands_[index].args.empty() && args().empty()) {
            input_ = commands_[index].name + ' ';
            selected_ = 0;
            return;
        }

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
            choices_.clear();
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
            choices_.clear();
            selected_ = 0;
        } else if (event.is_mouse()) {
            return false;
        }
        return true;
    }

    int CommandBar::height() const {
        const int errorLine = static_cast<int>(errorLines().size());
        const int suggestions = !open_ ? 0
                                : choices_.empty()
                                    ? static_cast<int>(matches().size())
                                    : static_cast<int>(choiceLines());
        return 3 + errorLine + suggestions;
    }

    ftxui::Element CommandBar::render() const {
        const auto accent = ftxui::Color::RGB(122, 162, 247);
        const auto muted = ftxui::Color::RGB(110, 110, 110);

        const auto prompt =
            open_ ? ftxui::hbox({
                        ftxui::text(" › ") | ftxui::color(accent),
                        ftxui::text("/" + input_),
                        ftxui::text(" ") | ftxui::focusCursorBarBlinking,
                    })
                  : ftxui::hbox({
                        ftxui::text(" › ") | ftxui::color(muted),
                        ftxui::text("/ for commands") | ftxui::color(muted),
                    });

        ftxui::Elements lines;
        lines.push_back(prompt | ftxui::xflex |
                        ftxui::borderStyled(ftxui::ROUNDED, muted));

        for (const auto& line : errorLines()) {
            lines.push_back(ftxui::text(std::string(ERROR_INDENT, ' ') + line) |
                            ftxui::color(ftxui::Color::Red));
        }

        if (open_ && !choices_.empty()) {
            const auto shown = std::min(choices_.size(), MAX_CHOICES_SHOWN);
            for (std::size_t n = 0; n < shown; ++n) {
                const std::string_view choice = choices_[n];
                const auto end =
                    choice.ends_with('/') ? choice.size() - 1 : choice.size();
                const auto slash = choice.rfind('/', end == 0 ? 0 : end - 1);
                const auto name = slash == std::string_view::npos ||
                                          slash + 1 >= choice.size()
                                      ? choice
                                      : choice.substr(slash + 1);
                lines.push_back(ftxui::text("   " + std::string(name)) |
                                ftxui::color(name.ends_with('/')
                                                 ? accent
                                                 : ftxui::Color::Default));
            }
            if (choices_.size() > shown) {
                lines.push_back(ftxui::text(std::format(
                                    "   … {} more", choices_.size() - shown)) |
                                ftxui::color(muted));
            }
        } else if (open_) {
            const auto found = matches();

            std::size_t width = 0;
            for (const std::size_t i : found) {
                const Command& command = commands_[i];
                width = std::max(width,
                                 command.name.size() + command.args.size() + 2);
            }

            const std::size_t current =
                found.empty() ? 0 : std::min(selected_, found.size() - 1);
            for (std::size_t n = 0; n < found.size(); ++n) {
                const Command& command = commands_[found[n]];
                const bool selected = n == current;

                std::string label = "/" + command.name;
                if (!command.args.empty()) {
                    label += " " + command.args;
                }

                label.resize(width + 2, ' ');

                lines.push_back(ftxui::hbox({
                    ftxui::text("   "),
                    ftxui::text(label) |
                        ftxui::color(selected ? accent : ftxui::Color::Default),
                    ftxui::text(command.help) |
                        ftxui::color(selected ? accent : muted),
                }));
            }
        }

        return ftxui::vbox(std::move(lines));
    }

}  // namespace shmscope
