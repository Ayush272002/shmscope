#pragma once

#include <cstddef>
#include <cstdint>

namespace shmscope {

    [[nodiscard]] bool copyGuarded(std::byte* out, const std::byte* mapping,
                                   std::size_t size) noexcept;

    [[nodiscard]] bool copyDiffGuarded(const std::byte* mapping,
                                       std::byte* current,
                                       const std::byte* previous,
                                       std::uint8_t* heat,
                                       std::size_t size) noexcept;
}  // namespace shmscope
