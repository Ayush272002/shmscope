#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <ftxui/component/component_base.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>

#include "shmscope/command_bar.hpp"
#include "shmscope/source.hpp"

namespace shmscope {

    // Live hex pane
    class Viewer {
    public:
        using CloseFn = std::function<void()>;

        Viewer(int hz, CloseFn onClose);

        Viewer(const Viewer&) = delete;
        Viewer& operator=(const Viewer&) = delete;
        Viewer(Viewer&&) = delete;
        Viewer& operator=(Viewer&&) = delete;
        ~Viewer() = default;

        void attach(std::unique_ptr<Source> source);
        void detach() noexcept;

        void tick() noexcept;
        [[nodiscard]] ftxui::Component component() const { return root_; }

    private:
        static constexpr std::size_t BYTES_PER_ROW = 16;
        static constexpr std::size_t FOLLOW_BLOCK_ROWS = 64;
        static constexpr std::size_t LIVE_RUN_ROWS = 6;
        static constexpr std::size_t LIVE_MERGE_ROWS = 2;

        struct LiveLine {
            enum class Kind : std::uint8_t { ROW, GAP, MORE };
            Kind kind;
            std::size_t value;
        };

        [[nodiscard]] ftxui::Element render();
        [[nodiscard]] ftxui::Element renderRow(std::size_t row) const;
        bool onEvent(const ftxui::Event& event);

        [[nodiscard]] std::size_t rowCount() const noexcept;
        [[nodiscard]] std::size_t maxTop() const noexcept;
        void scrollBy(std::ptrdiff_t rows) noexcept;
        void goTo(std::size_t row) noexcept;
        void toTop() noexcept;
        void toBottom();
        void follow() noexcept;

        [[nodiscard]] bool rowChanged(std::size_t row) const noexcept;
        void buildLive();
        [[nodiscard]] std::size_t liveMaxTop() const noexcept;
        [[nodiscard]] ftxui::Elements renderLive(std::size_t count) const;

        void addCommands();
        [[nodiscard]] std::optional<std::string> jump(std::string_view args);

        int hz_;
        CloseFn onClose_;
        std::unique_ptr<Source> source_;
        Frame frame_{};
        std::size_t top_ = 0;
        std::size_t visibleRows_ = 1;
        bool frozen_ = false;
        bool following_ = false;
        std::size_t followBlock_ = 0;
        bool live_ = false;
        std::size_t liveTop_ = 0;
        std::vector<LiveLine> liveLines_;
        CommandBar commandBar_;
        ftxui::Component root_;
    };

}  // namespace shmscope
