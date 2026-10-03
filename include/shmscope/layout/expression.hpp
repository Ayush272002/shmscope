#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace shmscope {

    enum class OpCode : std::uint8_t {
        PUSH,
        NAME,
        MEMBER,
        INDEX,
        NEGATE,
        COMPLEMENT,
        ADD,
        SUBTRACT,
        MULTIPLY,
        DIVIDE,
        MODULO,
        SHIFT_LEFT,
        SHIFT_RIGHT,
        BIT_AND,
        BIT_OR,
        BIT_XOR,
    };

    struct Instruction {
        OpCode code = OpCode::PUSH;
        std::int64_t value = 0;
        std::size_t name = 0;
        std::size_t column = 0;
    };

    struct Program {
        std::vector<Instruction> code{};
        std::vector<std::string> names{};
        std::size_t depth = 0;
    };

    struct ExpressionError {
        std::string message{};
        std::size_t column = 0;
    };

    inline constexpr std::size_t MAX_EXPRESSION_NESTING = 64;

    [[nodiscard]] std::expected<Program, ExpressionError> compile(
        std::string_view text);

    [[nodiscard]] std::string_view nameOf(OpCode code) noexcept;

    [[nodiscard]] std::string disassemble(const Program& program);
}  // namespace shmscope
