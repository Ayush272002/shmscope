#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <ftxui/component/component_base.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/box.hpp>

#include "shmscope/core/source.hpp"
#include "shmscope/layout/model.hpp"
#include "shmscope/layout/placement.hpp"
#include "shmscope/ui/command_bar.hpp"

namespace shmscope {

    // Live hex pane
    class Viewer {
    public:
        using CloseFn = std::function<void()>;
        using LayoutLoader = std::function<std::expected<Layout, std::string>(
            const std::filesystem::path& file)>;

        Viewer(int hz, CloseFn onClose, LayoutLoader loader = {},
               SourceOpener opener = {});

        Viewer(const Viewer&) = delete;
        Viewer& operator=(const Viewer&) = delete;
        Viewer(Viewer&&) = delete;
        Viewer& operator=(Viewer&&) = delete;
        ~Viewer() = default;

        void attach(std::unique_ptr<Source> source);
        void detach() noexcept;
        void setLayout(std::optional<Layout> layout);
        [[nodiscard]] std::optional<std::string> loadLayout(
            const std::filesystem::path& file);

        void tick() noexcept;
        [[nodiscard]] ftxui::Component component() const { return root_; }

    private:
        static constexpr std::size_t BYTES_PER_ROW = 16;
        static constexpr std::size_t FOLLOW_BLOCK_ROWS = 64;
        static constexpr std::size_t LIVE_RUN_ROWS = 6;
        static constexpr std::size_t LIVE_MERGE_ROWS = 2;
        static constexpr int INSPECTOR_WIDTH = 30;
        static constexpr int FIELD_PANEL_WIDTH = 44;
        static constexpr int HEX_WIDTH =
            12 + static_cast<int>(BYTES_PER_ROW) * 3 + 1;

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

        void setCursor(std::size_t offset) noexcept;
        void moveCursor(std::ptrdiff_t bytes) noexcept;
        void scrollWithCursor(std::ptrdiff_t rows) noexcept;
        [[nodiscard]] ftxui::Element renderInspector() const;
        [[nodiscard]] ftxui::Element renderSide();
        bool onMouse(ftxui::Event event);
        void scrollPanel(std::ptrdiff_t rows) noexcept;
        bool clickPanel(int y);
        bool clickHex(int x, int y);

        [[nodiscard]] bool rowChanged(std::size_t row) const noexcept;
        void buildLive();
        [[nodiscard]] std::size_t liveMaxTop() const noexcept;
        [[nodiscard]] ftxui::Elements renderLive(std::size_t count) const;

        void addCommands();
        [[nodiscard]] std::optional<std::string> jump(std::string_view args);
        void updatePlacement();
        [[nodiscard]] std::optional<std::string> jumpToField(
            std::string_view path);
        [[nodiscard]] bool inSelectedField(std::size_t offset) const noexcept;
        void track() noexcept;
        [[nodiscard]] std::optional<std::string> layoutCommand(
            std::string_view args);
        [[nodiscard]] std::optional<std::string> reopen();

        int hz_;
        CloseFn onClose_;
        LayoutLoader loader_;
        SourceOpener opener_;
        std::optional<std::filesystem::path> layoutFile_{};
        std::unique_ptr<Source> source_;
        Frame frame_{};
        std::size_t top_ = 0;
        std::size_t cursor_ = 0;
        std::size_t visibleRows_ = 1;
        bool frozen_ = false;
        bool following_ = false;
        std::size_t followBlock_ = 0;
        bool live_ = false;
        std::size_t liveTop_ = 0;
        std::vector<LiveLine> liveLines_;
        std::optional<Layout> layout_{};
        Placement placement_{};
        bool showFields_ = true;
        std::optional<std::size_t> selected_{};
        std::optional<std::string> tracking_{};
        std::optional<std::size_t> panelTop_{};
        std::size_t shownPanelTop_ = 0;
        ftxui::Box hexBox_{};
        ftxui::Box sideBox_{};
        CommandBar commandBar_;
        ftxui::Component root_;
    };

}  // namespace shmscope
