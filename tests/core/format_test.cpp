#include "shmscope/core/format.hpp"

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <variant>

#include <gtest/gtest.h>

#include "shmscope/core/decode.hpp"

namespace {

    using shmscope::Bound;
    using shmscope::FormatSpec;
    using shmscope::Formatter;
    using shmscope::FormatterSet;
    using shmscope::HasOptions;
    using shmscope::OptionsFormatter;
    using shmscope::OptionsOf;
    using shmscope::PlainFormatter;
    using shmscope::Value;

    struct Shout {
        static constexpr std::string_view NAME = "shout";

        static std::string apply(const Value& value) {
            return shmscope::toText(value) + "!";
        }
    };

    struct Repeat {
        static constexpr std::string_view NAME = "repeat";
        struct Options {
            int times = 1;
        };

        static std::expected<Options, std::string> parse(
            const FormatSpec& spec) {
            const auto found = spec.options.find("times");
            if (found == spec.options.end()) {
                return std::unexpected(std::string("needs 'times'"));
            }
            if (found->second != "2" && found->second != "3") {
                return std::unexpected(std::string("'times' must be 2 or 3"));
            }
            return Options{.times = found->second == "2" ? 2 : 3};
        }

        static std::string apply(const Value& value, const Options& options) {
            std::string text;
            for (int i = 0; i < options.times; ++i) {
                text += shmscope::toText(value);
            }
            return text;
        }
    };

    struct AlsoShout {
        static constexpr std::string_view NAME = "shout";

        static std::string apply(const Value&) { return "second"; }
    };

    struct NoName {
        static std::string apply(const Value&) { return ""; }
    };

    struct WrongReturn {
        static constexpr std::string_view NAME = "wrong";

        static int apply(const Value&) { return 0; }
    };

    struct OptionsWithoutParse {
        static constexpr std::string_view NAME = "half";
        struct Options {};

        static std::string apply(const Value&, const Options&) { return ""; }
    };

    struct OptionsButPlainApply {
        static constexpr std::string_view NAME = "mixed";
        struct Options {};

        static std::expected<Options, std::string> parse(const FormatSpec&) {
            return Options{};
        }
        static std::string apply(const Value&) { return ""; }
    };

    static_assert(Formatter<Shout>);
    static_assert(PlainFormatter<Shout>);
    static_assert(!OptionsFormatter<Shout>);
    static_assert(!HasOptions<Shout>);

    static_assert(Formatter<Repeat>);
    static_assert(OptionsFormatter<Repeat>);
    static_assert(!PlainFormatter<Repeat>);
    static_assert(HasOptions<Repeat>);

    static_assert(!Formatter<NoName>);
    static_assert(!Formatter<WrongReturn>);
    static_assert(!Formatter<OptionsWithoutParse>);
    static_assert(!Formatter<OptionsButPlainApply>);
    static_assert(!Formatter<int>);

    static_assert(std::is_same_v<OptionsOf<Shout>, std::monostate>);
    static_assert(std::is_same_v<OptionsOf<Repeat>, Repeat::Options>);

    using Set = FormatterSet<Shout, Repeat>;

    static_assert(std::is_same_v<Set::Compiled,
                                 std::variant<Bound<Shout>, Bound<Repeat>>>);
    static_assert(Set::NAMES.size() == 2);
    static_assert(Set::NAMES[0] == "shout");
    static_assert(Set::NAMES[1] == "repeat");
    static_assert(Set::knows("shout"));
    static_assert(Set::knows("repeat"));
    static_assert(!Set::knows("decimal"));
    static_assert(!Set::knows(""));

    Value number(std::uint64_t n) { return Value{.data = n, .width = 8}; }

    TEST(FormatterSetTest, CompilesAPlainFormatter) {
        const auto compiled = Set::compile({.kind = "shout"});

        ASSERT_TRUE(compiled.has_value());
        EXPECT_TRUE(std::holds_alternative<Bound<Shout>>(*compiled));
        EXPECT_EQ(Set::apply(*compiled, number(7)), "7!");
    }

    TEST(FormatterSetTest, CompilesAFormatterWithOptions) {
        const auto compiled =
            Set::compile({.kind = "repeat", .options = {{"times", "3"}}});

        ASSERT_TRUE(compiled.has_value());
        ASSERT_TRUE(std::holds_alternative<Bound<Repeat>>(*compiled));
        EXPECT_EQ(std::get<Bound<Repeat>>(*compiled).options.times, 3);
        EXPECT_EQ(Set::apply(*compiled, number(7)), "777");
    }

    TEST(FormatterSetTest, OneCompiledFormatAppliesToManyValues) {
        const auto compiled =
            Set::compile({.kind = "repeat", .options = {{"times", "2"}}});

        ASSERT_TRUE(compiled.has_value());
        EXPECT_EQ(Set::apply(*compiled, number(1)), "11");
        EXPECT_EQ(Set::apply(*compiled, number(42)), "4242");
    }

    TEST(FormatterSetTest, UnknownKindListsTheKnownOnes) {
        const auto compiled = Set::compile({.kind = "whisper"});

        ASSERT_FALSE(compiled.has_value());
        EXPECT_EQ(compiled.error(),
                  "unknown format 'whisper' (known: shout, repeat)");
    }

    TEST(FormatterSetTest, KindIsCaseSensitive) {
        EXPECT_FALSE(Set::compile({.kind = "Shout"}).has_value());
    }

    TEST(FormatterSetTest, ParseErrorsArePrefixedWithTheFormatName) {
        const auto compiled = Set::compile({.kind = "repeat"});

        ASSERT_FALSE(compiled.has_value());
        EXPECT_EQ(compiled.error(), "format 'repeat': needs 'times'");
    }

    TEST(FormatterSetTest, PlainFormatterRejectsOptions) {
        const auto compiled =
            Set::compile({.kind = "shout", .options = {{"volume", "11"}}});

        ASSERT_FALSE(compiled.has_value());
        EXPECT_EQ(compiled.error(), "format 'shout': takes no options");
    }

    TEST(FormatterSetTest, PlainFormatterRejectsATable) {
        const auto compiled =
            Set::compile({.kind = "shout", .entries = {{"1", "one"}}});

        ASSERT_FALSE(compiled.has_value());
        EXPECT_EQ(compiled.error(), "format 'shout': takes no options");
    }

    TEST(FormatterSetTest, ASingleFormatterSetWorks) {
        using Single = FormatterSet<AlsoShout>;
        const auto compiled = Single::compile({.kind = "shout"});

        ASSERT_TRUE(compiled.has_value());
        EXPECT_EQ(Single::apply(*compiled, number(1)), "second");
    }

    TEST(FormatterSetTest, CompiledFormatsCanBeCopied) {
        const auto compiled =
            Set::compile({.kind = "repeat", .options = {{"times", "2"}}});
        ASSERT_TRUE(compiled.has_value());

        const Set::Compiled copy = *compiled;
        EXPECT_EQ(Set::apply(copy, number(5)), "55");
    }

}  // namespace
