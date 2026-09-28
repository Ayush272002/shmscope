#include "shmscope/core/decode.hpp"

#include <bit>
#include <cstring>
#include <format>

namespace shmscope {

    namespace {

        static_assert(std::endian::native == std::endian::little);

        template <typename T>
        T load(std::span<const std::byte> bytes, std::size_t offset) {
            T value{};
            std::memcpy(&value, bytes.subspan(offset, sizeof(T)).data(),
                        sizeof(T));
            return value;
        }

        template <typename T>
        std::string integer(std::span<const std::byte> bytes,
                            std::size_t offset) {
            const T value = load<T>(bytes, offset);
            if constexpr (sizeof(T) == 1)
                return std::format("{}", static_cast<int>(value));
            else
                return std::format("{}", value);
        }

        std::string fixed8(std::span<const std::byte> bytes,
                           std::size_t offset) {
            const auto value = load<std::int64_t>(bytes, offset);
            const auto magnitude = value < 0
                                       ? 0 - static_cast<std::uint64_t>(value)
                                       : static_cast<std::uint64_t>(value);

            constexpr auto scale = static_cast<std::uint64_t>(FIXED8_SCALE);
            return std::format("{}{}.{:08}", value < 0 ? "-" : "",
                               magnitude / scale, magnitude % scale);
        }

        std::string ascii(std::span<const std::byte> bytes,
                          std::size_t offset) {
            std::string text;
            text.reserve(ASCII_WIDTH);
            for (const std::byte b : bytes.subspan(offset, ASCII_WIDTH)) {
                const auto c = std::to_integer<unsigned char>(b);
                text += c >= 0x20 && c < 0x7f ? static_cast<char>(c) : '.';
            }
            return text;
        }

    }  // namespace

    std::string_view nameOf(FieldType type) noexcept {
        switch (type) {
            case FieldType::U8:
                return "u8";
            case FieldType::U16:
                return "u16";
            case FieldType::U32:
                return "u32";
            case FieldType::U64:
                return "u64";
            case FieldType::I8:
                return "i8";
            case FieldType::I16:
                return "i16";
            case FieldType::I32:
                return "i32";
            case FieldType::I64:
                return "i64";
            case FieldType::F32:
                return "f32";
            case FieldType::F64:
                return "f64";
            case FieldType::FIXED8:
                return "fixed8";
            case FieldType::ASCII:
                return "ascii";
        }
        return "?";
    }

    std::size_t widthOf(FieldType type) noexcept {
        switch (type) {
            case FieldType::U8:
            case FieldType::I8:
                return 1;
            case FieldType::U16:
            case FieldType::I16:
                return 2;
            case FieldType::U32:
            case FieldType::I32:
            case FieldType::F32:
                return 4;
            case FieldType::U64:
            case FieldType::I64:
            case FieldType::F64:
            case FieldType::FIXED8:
                return 8;
            case FieldType::ASCII:
                return ASCII_WIDTH;
        }
        return 0;
    }

    std::optional<std::string> decode(FieldType type,
                                      std::span<const std::byte> bytes,
                                      std::size_t offset) {
        if (offset > bytes.size() || bytes.size() - offset < widthOf(type)) {
            return std::nullopt;
        }

        switch (type) {
            case FieldType::U8:
                return integer<std::uint8_t>(bytes, offset);
            case FieldType::U16:
                return integer<std::uint16_t>(bytes, offset);
            case FieldType::U32:
                return integer<std::uint32_t>(bytes, offset);
            case FieldType::U64:
                return integer<std::uint64_t>(bytes, offset);
            case FieldType::I8:
                return integer<std::int8_t>(bytes, offset);
            case FieldType::I16:
                return integer<std::int16_t>(bytes, offset);
            case FieldType::I32:
                return integer<std::int32_t>(bytes, offset);
            case FieldType::I64:
                return integer<std::int64_t>(bytes, offset);
            case FieldType::F32:
                return std::format("{:.6g}", load<float>(bytes, offset));
            case FieldType::F64:
                return std::format("{:.6g}", load<double>(bytes, offset));
            case FieldType::FIXED8:
                return fixed8(bytes, offset);
            case FieldType::ASCII:
                return ascii(bytes, offset);
        }
        return std::nullopt;
    }

}  // namespace shmscope
