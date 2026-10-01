#include "shmscope/core/decode.hpp"

#include <bit>
#include <cstring>
#include <format>

namespace shmscope {

    namespace {

        static_assert(std::endian::native == std::endian::little);

        template <FieldType T>
        constexpr bool widthMatchesStorage() {
            using Storage = typename FieldTraits<T>::type;
            if constexpr (std::is_arithmetic_v<Storage>) {
                return FieldTraits<T>::WIDTH == sizeof(Storage);
            } else {
                return true;
            }
        }

        template <std::size_t... Is>
        constexpr bool allWidthsMatch(std::index_sequence<Is...>) {
            return (widthMatchesStorage<ALL_FIELD_TYPES[Is]>() && ...);
        }

        static_assert(
            allWidthsMatch(std::make_index_sequence<ALL_FIELD_TYPES.size()>{}),
            "a FieldTraits WIDTH disagrees with its storage type");

        template <typename T>
        T load(std::span<const std::byte> bytes, std::size_t offset) {
            T value{};
            std::memcpy(&value, bytes.subspan(offset, sizeof(T)).data(),
                        sizeof(T));
            return value;
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

    std::optional<Value> read(FieldType type, std::span<const std::byte> bytes,
                              std::size_t offset) {
        if (offset > bytes.size() || bytes.size() - offset < widthOf(type)) {
            return std::nullopt;
        }

        return dispatch(
            type, [&]<FieldType T>(FieldTag<T>) -> std::optional<Value> {
                using Traits = FieldTraits<T>;
                using Storage = typename Traits::type;

                if constexpr (std::is_same_v<Storage, AsciiText>) {
                    return Value{.data = ascii(bytes, offset),
                                 .width = Traits::WIDTH};
                } else if constexpr (std::is_floating_point_v<Storage>) {
                    return Value{.data = static_cast<double>(
                                     load<Storage>(bytes, offset)),
                                 .width = Traits::WIDTH};
                } else if constexpr (std::is_signed_v<Storage>) {
                    return Value{.data = static_cast<std::int64_t>(
                                     load<Storage>(bytes, offset)),
                                 .width = Traits::WIDTH};
                } else {
                    return Value{.data = static_cast<std::uint64_t>(
                                     load<Storage>(bytes, offset)),
                                 .width = Traits::WIDTH};
                }
            });
    }

    std::string toText(const Value& value) {
        return std::visit(
            []<typename T>(const T& data) -> std::string {
                if constexpr (std::is_same_v<T, std::string>)
                    return data;
                else if constexpr (std::is_same_v<T, double>)
                    return std::format("{:.6g}", data);
                else
                    return std::format("{}", data);
            },
            value.data);
    }

    std::optional<std::string> decode(FieldType type,
                                      std::span<const std::byte> bytes,
                                      std::size_t offset) {
        const auto value = read(type, bytes, offset);
        if (!value) return std::nullopt;

        return toText(*value);
    }

}  // namespace shmscope
