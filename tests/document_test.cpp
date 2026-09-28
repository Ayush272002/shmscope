#include "shmscope/document.hpp"

#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace {

    using shmscope::describe;
    using shmscope::LoadError;
    using shmscope::Location;
    using shmscope::nameOf;
    using shmscope::Node;

    TEST(NodeTest, DefaultIsNull) {
        const Node node;

        EXPECT_TRUE(node.isNull());
        EXPECT_EQ(node.kind(), Node::Kind::NUL);
        EXPECT_FALSE(node.location().known());
    }

    TEST(NodeTest, ScalarKeepsTextAndLocation) {
        const auto node = Node::scalar("u8", {.line = 4, .column = 7});

        EXPECT_TRUE(node.isScalar());
        EXPECT_EQ(node.text(), "u8");
        EXPECT_EQ(node.location().line, 4);
        EXPECT_EQ(node.location().column, 7);
    }

    TEST(NodeTest, ScalarKeepsEmbeddedNul) {
        const auto node = Node::scalar(std::string("ab\0c", 4));

        EXPECT_EQ(node.text().size(), 4U);
        EXPECT_EQ(node.text()[2], '\0');
    }

    TEST(NodeTest, ListKeepsItemsInOrder) {
        const auto node = Node::list({Node::scalar("a"), Node::scalar("b")});

        EXPECT_TRUE(node.isList());
        ASSERT_EQ(node.items().size(), 2U);
        EXPECT_EQ(node.items()[0].text(), "a");
        EXPECT_EQ(node.items()[1].text(), "b");
    }

    TEST(NodeTest, MapKeepsEntriesInOrder) {
        std::vector<Node::Entry> entries;
        entries.push_back({.key = "z", .value = Node::scalar("1")});
        entries.push_back({.key = "a", .value = Node::scalar("2")});
        const auto node = Node::map(std::move(entries));

        EXPECT_TRUE(node.isMap());
        ASSERT_EQ(node.entries().size(), 2U);
        EXPECT_EQ(node.entries()[0].key, "z");
        EXPECT_EQ(node.entries()[1].key, "a");
    }

    TEST(NodeTest, FindReturnsTheValueForAKey) {
        std::vector<Node::Entry> entries;
        entries.push_back({.key = "id", .value = Node::scalar("magic")});
        entries.push_back({.key = "type", .value = Node::scalar("u8")});
        const auto node = Node::map(std::move(entries));

        const Node* type = node.find("type");
        ASSERT_NE(type, nullptr);
        EXPECT_EQ(type->text(), "u8");
    }

    TEST(NodeTest, FindReturnsNullForAMissingKey) {
        const auto node = Node::map({});

        EXPECT_EQ(node.find("seq"), nullptr);
    }

    TEST(NodeTest, FindOnANonMapReturnsNull) {
        EXPECT_EQ(Node::scalar("x").find("x"), nullptr);
        EXPECT_EQ(Node::list({}).find("x"), nullptr);
        EXPECT_EQ(Node().find("x"), nullptr);
    }

    TEST(NodeTest, KindNames) {
        EXPECT_EQ(nameOf(Node::Kind::NUL), "null");
        EXPECT_EQ(nameOf(Node::Kind::SCALAR), "scalar");
        EXPECT_EQ(nameOf(Node::Kind::LIST), "list");
        EXPECT_EQ(nameOf(Node::Kind::MAP), "map");
    }

    TEST(LoadErrorTest, DescribesSourceLineColumnAndMessage) {
        const LoadError error{.message = "duplicate key 'meta'",
                              .source = "qcore.ksy",
                              .location = {.line = 3, .column = 1}};

        EXPECT_EQ(describe(error), "qcore.ksy:3:1: duplicate key 'meta'");
    }

    TEST(LoadErrorTest, DescribesLineWithoutColumn) {
        const LoadError error{
            .message = "bad", .source = "a.ksy", .location = {.line = 9}};

        EXPECT_EQ(describe(error), "a.ksy:9: bad");
    }

    TEST(LoadErrorTest, DescribesPathWhenThereIsNoLine) {
        const LoadError error{.message = "unknown type",
                              .source = "a.json",
                              .path = "types.header.seq[2].type"};

        EXPECT_EQ(describe(error),
                  "a.json: types.header.seq[2].type: unknown type");
    }

    TEST(LoadErrorTest, DescribesLineAndPathTogether) {
        const LoadError error{.message = "bad",
                              .source = "a.ksy",
                              .path = "seq[0]",
                              .location = {.line = 2, .column = 5}};

        EXPECT_EQ(describe(error), "a.ksy:2:5: seq[0]: bad");
    }

    TEST(LoadErrorTest, UnnamedSourceIsCalledInput) {
        const LoadError error{.message = "bad"};

        EXPECT_EQ(describe(error), "<input>: bad");
    }

}  // namespace
