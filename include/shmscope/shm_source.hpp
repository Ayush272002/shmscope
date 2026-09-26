#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "shmscope/source.hpp"

namespace shmscope {

    class ShmSource final : public Source {
        struct Token {
            explicit Token() = default;
        };

    public:
        [[nodiscard]] static OpenResult open(std::string_view name);

        ShmSource(Token, std::string name, int fd, const std::byte* base,
                  std::size_t size);
        ~ShmSource() override;

        ShmSource(const ShmSource&) = delete;
        ShmSource& operator=(const ShmSource&) = delete;
        ShmSource(ShmSource&&) = delete;
        ShmSource& operator=(ShmSource&&) = delete;

        [[nodiscard]] std::string_view name() const noexcept override;
        [[nodiscard]] Frame poll() noexcept override;

    private:
        std::string name_;
        int fd_;
        const std::byte* base_;
        std::size_t size_;
        std::vector<std::byte> current_;
        std::vector<std::byte> previous_;
        std::vector<std::uint8_t> heat_;  // represents change of a byte
        std::uint64_t sequence_ = 0;
    };
}  // namespace shmscope
