#include "shmscope/layout/expression.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace shmscope {

    namespace {

        enum class TokenKind : std::uint8_t { INTEGER, NAME, SYMBOL, END };

        struct Token {
            TokenKind kind = TokenKind::END;
            std::string_view text{};
            std::uint64_t value = 0;
            std::size_t column = 0;
        };

        struct BinaryOperator {
            std::string_view symbol;
            OpCode code;
            int precedence;
        };

        constexpr std::array<BinaryOperator, 10> BINARY_OPERATORS = {{
            {"|", OpCode::BIT_OR, 1},
            {"^", OpCode::BIT_XOR, 2},
            {"&", OpCode::BIT_AND, 3},
            {"<<", OpCode::SHIFT_LEFT, 4},
            {">>", OpCode::SHIFT_RIGHT, 4},
            {"+", OpCode::ADD, 5},
            {"-", OpCode::SUBTRACT, 5},
            {"*", OpCode::MULTIPLY, 6},
            {"/", OpCode::DIVIDE, 6},
            {"%", OpCode::MODULO, 6},
        }};

        constexpr std::array<std::string_view, 16> SYMBOLS = {
            "<<", ">>", "+", "-", "*", "/", "%", "&",
            "|",  "^",  "~", ".", "(", ")", "[", "]"};

        constexpr std::array<std::string_view, 9> UNSUPPORTED_SYMBOLS = {
            "==", "!=", "<=", ">=", "<", ">", "?", ":", "!"};

        constexpr std::array<std::string_view, 5> UNSUPPORTED_WORDS = {
            "and", "or", "not", "true", "false"};

        bool isDigit(char c) { return c >= '0' && c <= '9'; }

        bool isNameStart(char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
        }

        bool isNameChar(char c) { return isNameStart(c) || isDigit(c); }

        std::optional<unsigned> digitValue(char c) {
            if (isDigit(c)) {
                return static_cast<unsigned>(c - '0');
            }
            if (c >= 'a' && c <= 'f') {
                return static_cast<unsigned>(c - 'a' + 10);
            }
            if (c >= 'A' && c <= 'F') {
                return static_cast<unsigned>(c - 'A' + 10);
            }
            return std::nullopt;
        }

        class Lexer {
        public:
            explicit Lexer(const std::string_view text) : text_(text) {}

            std::expected<std::vector<Token>, ExpressionError> run() {
                std::vector<Token> tokens;
                while (true) {
                    skipSpace();
                    if (at_ >= text_.size()) {
                        tokens.push_back(
                            Token{.kind = TokenKind::END, .column = at_ + 1});
                        return tokens;
                    }
                    auto token = next();
                    if (!token) {
                        return std::unexpected(std::move(token.error()));
                    }
                    tokens.push_back(*token);
                }
            }

        private:
            void skipSpace() {
                while (at_ < text_.size() &&
                       (text_[at_] == ' ' || text_[at_] == '\t' ||
                        text_[at_] == '\n' || text_[at_] == '\r')) {
                    ++at_;
                }
            }

            static std::unexpected<ExpressionError> fail(const std::size_t at,
                                                         std::string message) {
                return std::unexpected(ExpressionError{
                    .message = std::move(message), .column = at + 1});
            }

            std::expected<Token, ExpressionError> next() {
                const char c = text_[at_];
                if (isDigit(c)) return number();
                if (isNameStart(c)) return name();

                if (c == '"' || c == '\'') {
                    return fail(at_,
                                "strings are not supported in shmscope "
                                "expressions yet");
                }

                return symbol();
            }

            std::expected<Token, ExpressionError> number() {
                const std::size_t start = at_;
                unsigned base = 10;
                if (text_[at_] == '0' && at_ + 1 < text_.size()) {
                    const char prefix = text_[at_ + 1];
                    if (prefix == 'x' || prefix == 'X')
                        base = 16;
                    else if (prefix == 'b' || prefix == 'B')
                        base = 2;
                    else if (prefix == 'o' || prefix == 'O')
                        base = 8;

                    if (base != 10) at_ += 2;
                }

                std::uint64_t value = 0;
                bool any = false;
                bool overflow = false;
                while (at_ < text_.size()) {
                    const char c = text_[at_];
                    if (c == '_') {
                        ++at_;
                        continue;
                    }

                    const auto digit = digitValue(c);
                    if (!digit || (base == 10 && !isDigit(c))) {
                        break;
                    }

                    if (*digit >= base) {
                        return fail(at_, std::format("'{}' is not a base {} "
                                                     "digit",
                                                     c, base));
                    }
                    if (value >
                        (std::numeric_limits<std::uint64_t>::max() - *digit) /
                            base) {
                        overflow = true;
                    }
                    value = value * base + *digit;
                    any = true;
                    ++at_;
                }

                if (!any) {
                    return fail(start,
                                "a number needs digits after its "
                                "prefix");
                }
                if (at_ < text_.size() && text_[at_] == '.' &&
                    at_ + 1 < text_.size() && isDigit(text_[at_ + 1])) {
                    return fail(start,
                                "fractional numbers are not supported "
                                "in shmscope expressions");
                }
                if (at_ < text_.size() && isNameChar(text_[at_])) {
                    return fail(at_, std::format("unexpected '{}' after a "
                                                 "number",
                                                 text_[at_]));
                }
                if (overflow ||
                    value > static_cast<std::uint64_t>(
                                std::numeric_limits<std::int64_t>::max())) {
                    return fail(start,
                                std::format("'{}' does not fit in a signed "
                                            "64 bit integer",
                                            text_.substr(start, at_ - start)));
                }
                return Token{.kind = TokenKind::INTEGER,
                             .text = text_.substr(start, at_ - start),
                             .value = value,
                             .column = start + 1};
            }

            std::expected<Token, ExpressionError> name() {
                const std::size_t start = at_;
                while (at_ < text_.size() && isNameChar(text_[at_])) {
                    ++at_;
                }
                const auto text = text_.substr(start, at_ - start);
                if (std::ranges::find(UNSUPPORTED_WORDS, text) !=
                    UNSUPPORTED_WORDS.end()) {
                    return fail(start, std::format("'{}' is not supported in "
                                                   "shmscope expressions yet",
                                                   text));
                }
                return Token{
                    .kind = TokenKind::NAME, .text = text, .column = start + 1};
            }

            std::expected<Token, ExpressionError> symbol() {
                const auto rest = text_.substr(at_);
                for (const std::string_view unsupported : UNSUPPORTED_SYMBOLS) {
                    if (rest.starts_with(unsupported) &&
                        !rest.starts_with("<<") && !rest.starts_with(">>")) {
                        return fail(at_, std::format("'{}' is not supported "
                                                     "in shmscope expressions "
                                                     "yet",
                                                     unsupported));
                    }
                }
                for (const std::string_view known : SYMBOLS) {
                    if (rest.starts_with(known)) {
                        const std::size_t start = at_;
                        at_ += known.size();
                        return Token{.kind = TokenKind::SYMBOL,
                                     .text = known,
                                     .column = start + 1};
                    }
                }
                return fail(at_, std::format("unexpected '{}'", rest.front()));
            }

            std::string_view text_;
            std::size_t at_ = 0;
        };

        class Parser {
        public:
            explicit Parser(std::vector<Token> tokens)
                : tokens_(std::move(tokens)) {}

            std::expected<Program, ExpressionError> run() {
                if (peek().kind == TokenKind::END) {
                    return fail(peek(), "the expression is empty");
                }
                if (auto status = expression(0, 0); !status) {
                    return std::unexpected(std::move(status.error()));
                }
                if (peek().kind != TokenKind::END) {
                    return fail(peek(),
                                std::format("unexpected '{}'", peek().text));
                }
                program_.depth = measureDepth();
                return std::move(program_);
            }

        private:
            using Status = std::expected<void, ExpressionError>;

            [[nodiscard]] const Token& peek() const { return tokens_[at_]; }

            const Token& take() { return tokens_[at_++]; }

            bool takeSymbol(std::string_view symbol) {
                if (peek().kind == TokenKind::SYMBOL && peek().text == symbol) {
                    ++at_;
                    return true;
                }

                return false;
            }

            static std::unexpected<ExpressionError> fail(const Token& token,
                                                         std::string message) {
                return std::unexpected(ExpressionError{
                    .message = std::move(message), .column = token.column});
            }

            void emit(OpCode code, const Token& token, std::int64_t value = 0,
                      std::size_t name = 0) {
                program_.code.push_back(Instruction{.code = code,
                                                    .value = value,
                                                    .name = name,
                                                    .column = token.column});
            }

            std::size_t intern(std::string_view name) {
                const auto found = std::ranges::find(program_.names, name);
                if (found != program_.names.end()) {
                    return static_cast<std::size_t>(found -
                                                    program_.names.begin());
                }
                program_.names.emplace_back(name);
                return program_.names.size() - 1;
            }

            [[nodiscard]] std::optional<BinaryOperator> binaryOperator() const {
                if (peek().kind != TokenKind::SYMBOL) {
                    return std::nullopt;
                }
                const auto found = std::ranges::find(
                    BINARY_OPERATORS, peek().text, &BinaryOperator::symbol);
                if (found == BINARY_OPERATORS.end()) {
                    return std::nullopt;
                }
                return *found;
            }

            Status expression(int minimum, std::size_t nesting) {
                if (nesting > MAX_EXPRESSION_NESTING) {
                    return fail(peek(), "the expression is nested too deeply");
                }
                if (auto status = unary(nesting); !status) {
                    return status;
                }
                while (const auto op = binaryOperator()) {
                    if (op->precedence < minimum) {
                        break;
                    }
                    const Token& token = take();
                    if (auto status =
                            expression(op->precedence + 1, nesting + 1);
                        !status) {
                        return status;
                    }
                    emit(op->code, token);
                }
                return {};
            }

            Status unary(std::size_t nesting) {
                if (nesting > MAX_EXPRESSION_NESTING) {
                    return fail(peek(), "the expression is nested too deeply");
                }
                if (peek().kind == TokenKind::SYMBOL &&
                    (peek().text == "-" || peek().text == "~")) {
                    const Token& token = take();
                    if (auto status = unary(nesting + 1); !status) {
                        return status;
                    }
                    emit(
                        token.text == "-" ? OpCode::NEGATE : OpCode::COMPLEMENT,
                        token);
                    return {};
                }
                return postfix(nesting);
            }

            Status postfix(std::size_t nesting) {
                if (auto status = primary(nesting); !status) {
                    return status;
                }
                while (true) {
                    const Token& token = peek();
                    if (takeSymbol(".")) {
                        if (peek().kind != TokenKind::NAME) {
                            return fail(peek(), "expected a name after '.'");
                        }
                        const Token& member = take();
                        emit(OpCode::MEMBER, member, 0, intern(member.text));
                    } else if (takeSymbol("[")) {
                        if (auto status = expression(0, nesting + 1); !status) {
                            return status;
                        }
                        if (!takeSymbol("]")) {
                            return fail(peek(), "expected ']'");
                        }
                        emit(OpCode::INDEX, token);
                    } else {
                        return {};
                    }
                }
            }

            Status primary(std::size_t nesting) {
                const Token& token = peek();
                if (token.kind == TokenKind::INTEGER) {
                    take();
                    emit(OpCode::PUSH, token,
                         static_cast<std::int64_t>(token.value));
                    return {};
                }
                if (token.kind == TokenKind::NAME) {
                    take();
                    emit(OpCode::NAME, token, 0, intern(token.text));
                    return {};
                }
                if (takeSymbol("(")) {
                    if (auto status = expression(0, nesting + 1); !status) {
                        return status;
                    }
                    if (!takeSymbol(")")) {
                        return fail(peek(), "expected ')'");
                    }
                    return {};
                }
                if (token.kind == TokenKind::END) {
                    return fail(token, "the expression ends too early");
                }
                return fail(token, std::format("expected a number, a name or "
                                               "'(', got '{}'",
                                               token.text));
            }

            [[nodiscard]] std::size_t measureDepth() const {
                std::size_t depth = 0;
                std::size_t deepest = 0;
                for (const auto& instruction : program_.code) {
                    switch (instruction.code) {
                        case OpCode::PUSH:
                        case OpCode::NAME:
                            ++depth;
                            break;
                        case OpCode::MEMBER:
                        case OpCode::NEGATE:
                        case OpCode::COMPLEMENT:
                            break;
                        default:
                            --depth;
                            break;
                    }
                    deepest = std::max(deepest, depth);
                }
                return deepest;
            }

            std::vector<Token> tokens_;
            std::size_t at_ = 0;
            Program program_{};
        };

    }  // namespace

    std::expected<Program, ExpressionError> compile(
        const std::string_view text) {
        auto tokens = Lexer(text).run();
        if (!tokens) return std::unexpected(std::move(tokens.error()));

        return Parser(std::move(*tokens)).run();
    }

    std::string_view nameOf(OpCode code) noexcept {
        switch (code) {
            case OpCode::PUSH:
                return "PUSH";
            case OpCode::NAME:
                return "NAME";
            case OpCode::MEMBER:
                return "MEMBER";
            case OpCode::INDEX:
                return "INDEX";
            case OpCode::NEGATE:
                return "NEGATE";
            case OpCode::COMPLEMENT:
                return "COMPLEMENT";
            case OpCode::ADD:
                return "ADD";
            case OpCode::SUBTRACT:
                return "SUBTRACT";
            case OpCode::MULTIPLY:
                return "MULTIPLY";
            case OpCode::DIVIDE:
                return "DIVIDE";
            case OpCode::MODULO:
                return "MODULO";
            case OpCode::SHIFT_LEFT:
                return "SHIFT_LEFT";
            case OpCode::SHIFT_RIGHT:
                return "SHIFT_RIGHT";
            case OpCode::BIT_AND:
                return "BIT_AND";
            case OpCode::BIT_OR:
                return "BIT_OR";
            case OpCode::BIT_XOR:
                return "BIT_XOR";
        }
        return "?";
    }

    std::string disassemble(const Program& program) {
        std::string text;
        for (const auto& instruction : program.code) {
            if (!text.empty()) {
                text += "; ";
            }
            text += nameOf(instruction.code);
            if (instruction.code == OpCode::PUSH) {
                text += std::format(" {}", instruction.value);
            } else if (instruction.code == OpCode::NAME ||
                       instruction.code == OpCode::MEMBER) {
                text += " " + program.names[instruction.name];
            }
        }
        return text;
    }

}  // namespace shmscope
