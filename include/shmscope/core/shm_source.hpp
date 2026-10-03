#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "shmscope/core/source.hpp"

namespace shmscope {

    struct ShmOptions {
        std::chrono::milliseconds checkEvery{1000};
    };

    struct SegmentIdentity {
        std::uint64_t first = 0;
        std::uint64_t second = 0;

        bool operator==(const SegmentIdentity&) const = default;
    };

    class ShmSource final : public Source {
        struct Token {
            explicit Token() = default;
        };

    public:
        [[nodiscard]] static OpenResult open(std::string_view name,
                                             ShmOptions options = {});

        ShmSource(Token, std::string name, int fd, const std::byte* base,
                  std::size_t size, ShmOptions options);
        ~ShmSource() override;

        ShmSource(const ShmSource&) = delete;
        ShmSource& operator=(const ShmSource&) = delete;
        ShmSource(ShmSource&&) = delete;
        ShmSource& operator=(ShmSource&&) = delete;

        [[nodiscard]] std::string_view name() const noexcept override;
        [[nodiscard]] Frame poll() noexcept override;
        [[nodiscard]] SourceState state() const noexcept override {
            return state_;
        }

    private:
        void followSize() noexcept;
        void remap(std::size_t size) noexcept;
        void checkName() noexcept;

        std::string name_;
        int fd_;
        const std::byte* base_;
        std::size_t size_;
        ShmOptions options_;
        std::optional<SegmentIdentity> identity_{};
        SourceState state_ = SourceState::LIVE;
        std::chrono::steady_clock::time_point checkedAt_{};
        std::vector<std::byte> current_;
        std::vector<std::byte> previous_;
        std::vector<std::uint8_t> heat_;  // represents change of a byte
        std::uint64_t sequence_ = 0;
    };
}  // namespace shmscope
