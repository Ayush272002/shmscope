#pragma once

#include <cstddef>
#include <cstdint>

namespace shmscope {

    void copyDiff(const std::byte* mapping, std::byte* current,
                  const std::byte* previous, std::uint8_t* heat,
                  std::size_t size) noexcept;

}  // namespace shmscope
