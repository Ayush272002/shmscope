#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace shmscope {

    enum class FieldType : std::uint8_t {
        U8,
        U16,
        U32,
        U64,
        I8,
        I16,
        I32,
        I64,
        F32,
        F64,
        FIXED8,
        ASCII,
    };

    inline constexpr std::array ALL_FIELD_TYPES = {
        FieldType::U8,  FieldType::U16, FieldType::U32,    FieldType::U64,
        FieldType::I8,  FieldType::I16, FieldType::I32,    FieldType::I64,
        FieldType::F32, FieldType::F64, FieldType::FIXED8, FieldType::ASCII,
    };

    inline constexpr std::size_t ASCII_WIDTH = 8;
    inline constexpr std::int64_t FIXED8_SCALE = 100'000'000;

    [[nodiscard]] std::string_view nameOf(FieldType type) noexcept;
    [[nodiscard]] std::size_t widthOf(FieldType type) noexcept;

    [[nodiscard]] std::optional<std::string> decode(
        FieldType type, std::span<const std::byte> bytes, std::size_t offset);

}  // namespace shmscope
