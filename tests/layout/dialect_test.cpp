#include "shmscope/layout/dialect.hpp"

#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "shmscope/layout/document.hpp"
#include "shmscope/layout/model.hpp"

namespace {

    using shmscope::Dialect;
    using shmscope::DialectSet;
    using shmscope::Layout;
    using shmscope::LayoutResult;
    using shmscope::LoadError;
    using shmscope::Node;

    Node mapWith(std::string key) {
        std::vector<Node::Entry> entries;
        entries.push_back({.key = std::move(key), .value = Node::scalar("x")});
        return Node::map(std::move(entries), {.line = 3, .column = 1});
    }

    struct Alpha {
        static constexpr std::string_view NAME = "alpha";
        static bool matches(const Node& root) {
            return root.find("alpha") != nullptr;
        }
        static LayoutResult load(const Node&, std::string_view source) {
            return Layout{.id = "from_alpha", .title = std::string(source)};
        }
    };

    struct Beta {
        static constexpr std::string_view NAME = "beta";
        static bool matches(const Node& root) { return root.isMap(); }
        static LayoutResult load(const Node&, std::string_view) {
            return Layout{.id = "from_beta"};
        }
    };

    struct Failing {
        static constexpr std::string_view NAME = "failing";
        static bool matches(const Node&) { return true; }
        static LayoutResult load(const Node&, std::string_view source) {
            return std::unexpected(
                LoadError{.message = "broken", .source = std::string(source)});
        }
    };

    struct NoName {
        static bool matches(const Node&) { return true; }
        static LayoutResult load(const Node&, std::string_view) {
            return Layout{};
        }
    };

    struct WrongLoad {
        static constexpr std::string_view NAME = "wrong";
        static bool matches(const Node&) { return true; }
        static Layout load(const Node&, std::string_view) { return Layout{}; }
    };

    struct NoMatches {
        static constexpr std::string_view NAME = "nomatch";
        static LayoutResult load(const Node&, std::string_view) {
            return Layout{};
        }
    };

    static_assert(Dialect<Alpha>);
    static_assert(Dialect<Beta>);
    static_assert(Dialect<Failing>);
    static_assert(!Dialect<NoName>);
    static_assert(!Dialect<WrongLoad>);
    static_assert(!Dialect<NoMatches>);
    static_assert(!Dialect<int>);

    using Set = DialectSet<Alpha, Beta>;

    static_assert(Set::NAMES.size() == 2);
    static_assert(Set::NAMES[0] == "alpha");
    static_assert(Set::NAMES[1] == "beta");

    TEST(DialectSetTest, UsesTheFirstDialectThatMatches) {
        const auto layout = Set::load(mapWith("alpha"), "a.ksy");

        ASSERT_TRUE(layout.has_value());
        EXPECT_EQ(layout->id, "from_alpha");
    }

    TEST(DialectSetTest, FallsThroughToALaterDialect) {
        const auto layout = Set::load(mapWith("other"), "a.ksy");

        ASSERT_TRUE(layout.has_value());
        EXPECT_EQ(layout->id, "from_beta");
    }

    TEST(DialectSetTest, PassesTheSourceToTheDialect) {
        const auto layout = Set::load(mapWith("alpha"), "layouts/ring.ksy");

        ASSERT_TRUE(layout.has_value());
        EXPECT_EQ(layout->title, "layouts/ring.ksy");
    }

    TEST(DialectSetTest, OrderDecidesWhichDialectWins) {
        using Reversed = DialectSet<Beta, Alpha>;
        const auto layout = Reversed::load(mapWith("alpha"), "a.ksy");

        ASSERT_TRUE(layout.has_value());
        EXPECT_EQ(layout->id, "from_beta");
    }

    TEST(DialectSetTest, NoMatchListsTheDialects) {
        const auto layout =
            Set::load(Node::scalar("x", {.line = 1, .column = 1}), "a.ksy");

        ASSERT_FALSE(layout.has_value());
        EXPECT_EQ(layout.error().message,
                  "not a layout this build understands (dialects: alpha, "
                  "beta)");
        EXPECT_EQ(layout.error().source, "a.ksy");
        EXPECT_EQ(layout.error().location.line, 1);
    }

    TEST(DialectSetTest, AMatchingDialectsErrorIsReturnedAsIs) {
        using WithFailing = DialectSet<Failing, Beta>;
        const auto layout = WithFailing::load(mapWith("x"), "a.ksy");

        ASSERT_FALSE(layout.has_value());
        EXPECT_EQ(layout.error().message, "broken");
        EXPECT_EQ(layout.error().source, "a.ksy");
    }

    TEST(DialectSetTest, RecogniseAsksEveryDialect) {
        EXPECT_TRUE(Set::recognise(mapWith("alpha")));
        EXPECT_TRUE(Set::recognise(mapWith("other")));
        EXPECT_FALSE(Set::recognise(Node::list({})));
        EXPECT_FALSE(Set::recognise(Node()));
    }

    TEST(DialectSetTest, ASingleDialectSetWorks) {
        using Single = DialectSet<Alpha>;

        EXPECT_TRUE(Single::load(mapWith("alpha"), "a").has_value());
        EXPECT_FALSE(Single::load(mapWith("beta"), "a").has_value());
    }

}  // namespace
