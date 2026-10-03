#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

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
        ASCII,
    };

    inline constexpr std::array ALL_FIELD_TYPES = {
        FieldType::U8,  FieldType::U16, FieldType::U32,   FieldType::U64,
        FieldType::I8,  FieldType::I16, FieldType::I32,   FieldType::I64,
        FieldType::F32, FieldType::F64, FieldType::ASCII,
    };

    inline constexpr std::size_t ASCII_WIDTH = 8;

    struct AsciiText {};

    template <FieldType T>
    struct FieldTraits;

    template <typename Storage, std::size_t Width, std::string_view const& Name>
    struct FieldTraitsBase {
        using type = Storage;
        static constexpr std::size_t WIDTH = Width;
        static constexpr std::string_view NAME = Name;
    };

    namespace names {
        inline constexpr std::string_view U8 = "u8";
        inline constexpr std::string_view U16 = "u16";
        inline constexpr std::string_view U32 = "u32";
        inline constexpr std::string_view U64 = "u64";
        inline constexpr std::string_view I8 = "i8";
        inline constexpr std::string_view I16 = "i16";
        inline constexpr std::string_view I32 = "i32";
        inline constexpr std::string_view I64 = "i64";
        inline constexpr std::string_view F32 = "f32";
        inline constexpr std::string_view F64 = "f64";
        inline constexpr std::string_view ASCII = "ascii";
    }  // namespace names

    template <>
    struct FieldTraits<FieldType::U8>
        : FieldTraitsBase<std::uint8_t, 1, names::U8> {};

    template <>
    struct FieldTraits<FieldType::U16>
        : FieldTraitsBase<std::uint16_t, 2, names::U16> {};

    template <>
    struct FieldTraits<FieldType::U32>
        : FieldTraitsBase<std::uint32_t, 4, names::U32> {};

    template <>
    struct FieldTraits<FieldType::U64>
        : FieldTraitsBase<std::uint64_t, 8, names::U64> {};

    template <>
    struct FieldTraits<FieldType::I8>
        : FieldTraitsBase<std::int8_t, 1, names::I8> {};

    template <>
    struct FieldTraits<FieldType::I16>
        : FieldTraitsBase<std::int16_t, 2, names::I16> {};

    template <>
    struct FieldTraits<FieldType::I32>
        : FieldTraitsBase<std::int32_t, 4, names::I32> {};

    template <>
    struct FieldTraits<FieldType::I64>
        : FieldTraitsBase<std::int64_t, 8, names::I64> {};

    template <>
    struct FieldTraits<FieldType::F32> : FieldTraitsBase<float, 4, names::F32> {
    };

    template <>
    struct FieldTraits<FieldType::F64>
        : FieldTraitsBase<double, 8, names::F64> {};

    template <>
    struct FieldTraits<FieldType::ASCII>
        : FieldTraitsBase<AsciiText, ASCII_WIDTH, names::ASCII> {};

    template <FieldType T>
    using FieldTag = std::integral_constant<FieldType, T>;

    namespace detail {

        template <typename Fn, std::size_t... Is>
        constexpr auto dispatch(FieldType type, Fn&& fn,
                                std::index_sequence<Is...>) {
            using Result =
                std::invoke_result_t<Fn&, FieldTag<ALL_FIELD_TYPES[0]>>;

            static_assert(
                (std::is_same_v<Result,
                                std::invoke_result_t<
                                    Fn&, FieldTag<ALL_FIELD_TYPES[Is]>>> &&
                 ...),
                "every FieldType branch must return the same type");
            Result result{};

            static_cast<void>(
                ((type == ALL_FIELD_TYPES[Is] &&
                  (result = fn(FieldTag<ALL_FIELD_TYPES[Is]>{}), true)) ||
                 ...));
            return result;
        }
    }  // namespace detail

    template <typename Fn>
    constexpr auto dispatch(FieldType type, Fn&& fn) {
        return detail::dispatch(
            type, std::forward<Fn>(fn),
            std::make_index_sequence<ALL_FIELD_TYPES.size()>{});
    }

    [[nodiscard]] constexpr std::string_view nameOf(FieldType type) noexcept {
        return dispatch(type, []<FieldType T>(FieldTag<T>) {
            return FieldTraits<T>::NAME;
        });
    }

    [[nodiscard]] constexpr std::size_t widthOf(FieldType type) noexcept {
        return dispatch(type, []<FieldType T>(FieldTag<T>) {
            return FieldTraits<T>::WIDTH;
        });
    }

    struct Value {
        std::variant<std::uint64_t, std::int64_t, double, std::string> data{};
        std::size_t width = 0;
    };

    [[nodiscard]] std::optional<Value> read(FieldType type,
                                            std::span<const std::byte> bytes,
                                            std::size_t offset);

    [[nodiscard]] std::string toText(const Value& value);

    [[nodiscard]] std::optional<std::string> decode(
        FieldType type, std::span<const std::byte> bytes, std::size_t offset);

}  // namespace shmscope
