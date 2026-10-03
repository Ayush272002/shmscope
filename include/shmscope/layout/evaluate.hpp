#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "shmscope/layout/expression.hpp"

namespace shmscope {

    template <typename Ref>
    using Operand = std::variant<std::int64_t, Ref>;

    template <typename Ref>
    using Lookup = std::expected<Operand<Ref>, std::string>;

    template <typename R>
    concept Resolver =
        requires { typename R::Ref; } && std::copyable<typename R::Ref> &&
        !std::same_as<typename R::Ref, std::int64_t> &&
        requires(R& resolver, const typename R::Ref& ref, std::string_view name,
                 std::int64_t index) {
            { resolver.name(name) } -> std::same_as<Lookup<typename R::Ref>>;
            {
                resolver.member(ref, name)
            } -> std::same_as<Lookup<typename R::Ref>>;
            {
                resolver.element(ref, index)
            } -> std::same_as<Lookup<typename R::Ref>>;
            {
                resolver.number(ref)
            } -> std::same_as<std::expected<std::int64_t, std::string>>;
        };

    struct EvaluationError {
        std::string message{};
        std::size_t column = 0;
    };

    namespace detail {

        using Arithmetic = std::expected<std::int64_t, std::string>;

        inline constexpr std::int64_t MAX =
            std::numeric_limits<std::int64_t>::max();
        inline constexpr std::int64_t MIN =
            std::numeric_limits<std::int64_t>::min();

        inline Arithmetic overflow() {
            return std::unexpected(
                std::string("the result does not fit in a signed 64 bit "
                            "integer"));
        }

        inline Arithmetic shiftCount(std::int64_t count) {
            if (count < 0 || count > 63) {
                return std::unexpected(std::format(
                    "a shift by {} is out of range; use 0 to 63", count));
            }
            return count;
        }

        inline Arithmetic negate(std::int64_t value) {
            if (value == MIN) {
                return overflow();
            }
            return -value;
        }

        inline Arithmetic binary(OpCode code, std::int64_t left,
                                 std::int64_t right) {
            std::int64_t result = 0;
            switch (code) {
                case OpCode::ADD:
                    if (__builtin_add_overflow(left, right, &result)) {
                        return overflow();
                    }
                    return result;
                case OpCode::SUBTRACT:
                    if (__builtin_sub_overflow(left, right, &result)) {
                        return overflow();
                    }
                    return result;
                case OpCode::MULTIPLY:
                    if (__builtin_mul_overflow(left, right, &result)) {
                        return overflow();
                    }
                    return result;
                case OpCode::DIVIDE: {
                    if (right == 0) {
                        return std::unexpected(std::string("division by zero"));
                    }
                    if (left == MIN && right == -1) {
                        return overflow();
                    }
                    std::int64_t quotient = left / right;
                    if (left % right != 0 && ((left < 0) != (right < 0))) {
                        --quotient;
                    }
                    return quotient;
                }
                case OpCode::MODULO: {
                    if (right == 0) {
                        return std::unexpected(std::string("modulo by zero"));
                    }
                    if (right == -1) {
                        return 0;
                    }
                    std::int64_t remainder = left % right;
                    if (remainder != 0 && ((remainder < 0) != (right < 0))) {
                        remainder += right;
                    }
                    return remainder;
                }
                case OpCode::SHIFT_LEFT: {
                    const auto count = shiftCount(right);
                    if (!count) {
                        return count;
                    }
                    if (left > (MAX >> *count) || left < (MIN >> *count)) {
                        return overflow();
                    }
                    return static_cast<std::int64_t>(
                        static_cast<std::uint64_t>(left) << *count);
                }
                case OpCode::SHIFT_RIGHT: {
                    const auto count = shiftCount(right);
                    if (!count) {
                        return count;
                    }
                    return left >> *count;
                }
                case OpCode::BIT_AND:
                    return left & right;
                case OpCode::BIT_OR:
                    return left | right;
                case OpCode::BIT_XOR:
                    return left ^ right;
                default:
                    return std::unexpected(std::format(
                        "'{}' is not a binary operator", nameOf(code)));
            }
        }

        template <Resolver R>
        class Machine {
        public:
            using Ref = typename R::Ref;

            Machine(const Program& program, R& resolver)
                : program_(program), resolver_(resolver) {
                stack_.reserve(program.depth);
            }

            std::expected<std::int64_t, EvaluationError> run() {
                if (program_.code.empty()) {
                    return std::unexpected(
                        EvaluationError{.message = "the program is empty"});
                }
                for (const auto& instruction : program_.code) {
                    if (auto status = step(instruction); !status) {
                        return std::unexpected(EvaluationError{
                            .message = std::move(status.error()),
                            .column = instruction.column});
                    }
                }
                if (stack_.size() != 1) {
                    return std::unexpected(EvaluationError{
                        .message = "the program left more than one value"});
                }
                auto result = number(std::move(stack_.back()));
                if (!result) {
                    return std::unexpected(EvaluationError{
                        .message = std::move(result.error()),
                        .column = program_.code.front().column});
                }
                return *result;
            }

        private:
            using Status = std::expected<void, std::string>;

            Status push(Lookup<Ref> operand) {
                if (!operand)
                    return std::unexpected(std::move(operand.error()));

                stack_.push_back(std::move(*operand));
                return {};
            }

            std::expected<Operand<Ref>, std::string> pop() {
                if (stack_.empty()) {
                    return std::unexpected(
                        std::string("the program is malformed"));
                }
                auto top = std::move(stack_.back());
                stack_.pop_back();
                return top;
            }

            std::expected<std::int64_t, std::string> number(
                Operand<Ref> operand) {
                if (const auto* value = std::get_if<std::int64_t>(&operand)) {
                    return *value;
                }
                return resolver_.number(std::get<Ref>(operand));
            }

            std::expected<std::int64_t, std::string> popNumber() {
                auto operand = pop();
                if (!operand) {
                    return std::unexpected(std::move(operand.error()));
                }
                return number(std::move(*operand));
            }

            std::expected<Ref, std::string> popReference(std::string_view use) {
                auto operand = pop();
                if (!operand) {
                    return std::unexpected(std::move(operand.error()));
                }
                if (std::holds_alternative<std::int64_t>(*operand)) {
                    return std::unexpected(
                        std::format("a number has no {}", use));
                }
                return std::get<Ref>(std::move(*operand));
            }

            Status pushNumber(std::expected<std::int64_t, std::string> value) {
                if (!value) {
                    return std::unexpected(std::move(value.error()));
                }
                stack_.emplace_back(*value);
                return {};
            }

            Status step(const Instruction& instruction) {
                switch (instruction.code) {
                    case OpCode::PUSH:
                        stack_.emplace_back(instruction.value);
                        return {};
                    case OpCode::NAME:
                        return push(resolver_.name(nameAt(instruction)));
                    case OpCode::MEMBER: {
                        const auto name = nameAt(instruction);
                        auto base =
                            popReference(std::format("member '{}'", name));
                        if (!base) {
                            return std::unexpected(std::move(base.error()));
                        }
                        return push(resolver_.member(*base, name));
                    }
                    case OpCode::INDEX: {
                        const auto index = popNumber();
                        if (!index) {
                            return std::unexpected(index.error());
                        }
                        auto base = popReference("elements");
                        if (!base) {
                            return std::unexpected(std::move(base.error()));
                        }
                        return push(resolver_.element(*base, *index));
                    }
                    case OpCode::NEGATE: {
                        const auto value = popNumber();
                        if (!value) {
                            return std::unexpected(value.error());
                        }
                        return pushNumber(negate(*value));
                    }
                    case OpCode::COMPLEMENT: {
                        const auto value = popNumber();
                        if (!value) {
                            return std::unexpected(value.error());
                        }
                        return pushNumber(~*value);
                    }
                    default: {
                        const auto right = popNumber();
                        if (!right) {
                            return std::unexpected(right.error());
                        }
                        const auto left = popNumber();
                        if (!left) {
                            return std::unexpected(left.error());
                        }
                        return pushNumber(
                            binary(instruction.code, *left, *right));
                    }
                }
            }

            [[nodiscard]] std::string_view nameAt(
                const Instruction& instruction) const {
                if (instruction.name >= program_.names.size()) {
                    return {};
                }
                return program_.names[instruction.name];
            }

            const Program& program_;
            R& resolver_;
            std::vector<Operand<Ref>> stack_{};
        };

    }  // namespace detail

    template <Resolver R>
    [[nodiscard]] std::expected<std::int64_t, EvaluationError> evaluate(
        const Program& program, R& resolver) {
        return detail::Machine<R>(program, resolver).run();
    }

}  // namespace shmscope
