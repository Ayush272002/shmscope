#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace shmscope {

    inline constexpr std::uint8_t HEAT_MAX = 8;

    // One refresh worth of state.
    struct Frame {
        std::span<const std::byte> bytes;
        std::span<const std::uint8_t> heat;  // HEAT_MAX on change, decays to 0
        std::uint64_t sequence = 0;
    };

    enum class SourceState : std::uint8_t { LIVE, REMOVED, REPLACED };

    class Source {
    public:
        Source() = default;
        Source(const Source&) = delete;
        Source& operator=(const Source&) = delete;
        Source(Source&&) = delete;
        Source& operator=(Source&&) = delete;
        virtual ~Source() = default;

        [[nodiscard]] virtual std::string_view name() const noexcept = 0;

        [[nodiscard]] virtual Frame poll() noexcept = 0;

        [[nodiscard]] virtual SourceState state() const noexcept {
            return SourceState::LIVE;
        }
    };

    using OpenResult = std::expected<std::unique_ptr<Source>, std::string>;
    using SourceOpener = std::function<OpenResult(std::string_view name)>;
}  // namespace shmscope
