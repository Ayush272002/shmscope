#include "shmscope/layout/model.hpp"

#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <utility>

#include <gtest/gtest.h>

#include "shmscope/core/decode.hpp"
#include "shmscope/core/default_formatters.hpp"

namespace {

    using shmscope::Attribute;
    using shmscope::AttributeKind;
    using shmscope::DefaultFormatters;
    using shmscope::Expression;
    using shmscope::FieldType;
    using shmscope::Layout;
    using shmscope::SwitchCase;
    using shmscope::SwitchType;
    using shmscope::Type;

    Attribute scalar(std::string id, FieldType type) {
        return Attribute{
            .id = std::move(id), .kind = AttributeKind::SCALAR, .scalar = type};
    }

    Type sample() {
        Type type{.name = "header"};
        type.seq.push_back(scalar("kind", FieldType::U32));
        type.seq.push_back(scalar("count", FieldType::U64));
        type.instances.push_back(
            Attribute{.id = "records",
                      .kind = AttributeKind::USER,
                      .userType = "record",
                      .pos = Expression{.text = "count * 8"}});
        return type;
    }

    TEST(AttributeTest, DefaultsToUnsizedBytes) {
        const Attribute attribute{.id = "x"};

        EXPECT_EQ(attribute.kind, AttributeKind::BYTES);
        EXPECT_EQ(attribute.scalar, FieldType::U8);
        EXPECT_TRUE(attribute.userType.empty());
        EXPECT_FALSE(attribute.switchOn.has_value());
        EXPECT_FALSE(attribute.size.has_value());
        EXPECT_TRUE(attribute.contents.empty());
        EXPECT_FALSE(attribute.repeatCount.has_value());
        EXPECT_FALSE(attribute.pos.has_value());
        EXPECT_TRUE(attribute.format.empty());
        EXPECT_FALSE(attribute.location.known());
    }

    TEST(AttributeTest, KeepsExpressionsAsTextWithTheirLocation) {
        const Attribute attribute{
            .id = "body",
            .size = Expression{.text = "header.size - 16",
                               .location = {.line = 7, .column = 11}}};

        ASSERT_TRUE(attribute.size.has_value());
        EXPECT_EQ(attribute.size->text, "header.size - 16");
        EXPECT_EQ(attribute.size->location.line, 7);
        EXPECT_EQ(attribute.size->location.column, 11);
    }

    TEST(AttributeTest, SwitchKeepsCasesInOrderWithADefault) {
        const Attribute attribute{
            .id = "body",
            .kind = AttributeKind::SWITCH,
            .switchOn = SwitchType{
                .on = Expression{.text = "kind"},
                .cases = {SwitchCase{.value = 1, .type = "trade"},
                          SwitchCase{.value = std::nullopt, .type = "raw"}}}};

        ASSERT_TRUE(attribute.switchOn.has_value());
        EXPECT_EQ(attribute.switchOn->on.text, "kind");
        ASSERT_EQ(attribute.switchOn->cases.size(), 2U);
        EXPECT_EQ(attribute.switchOn->cases[0].value, 1U);
        EXPECT_EQ(attribute.switchOn->cases[0].type, "trade");
        EXPECT_FALSE(attribute.switchOn->cases[1].value.has_value());
        EXPECT_EQ(attribute.switchOn->cases[1].type, "raw");
    }

    TEST(TypeTest, FindsAnAttributeInSeq) {
        const Type type = sample();

        const Attribute* found = type.find("count");
        ASSERT_NE(found, nullptr);
        EXPECT_EQ(found->scalar, FieldType::U64);
    }

    TEST(TypeTest, FindsAnAttributeInInstances) {
        const Type type = sample();

        const Attribute* found = type.find("records");
        ASSERT_NE(found, nullptr);
        EXPECT_EQ(found->kind, AttributeKind::USER);
        EXPECT_EQ(found->userType, "record");
        ASSERT_TRUE(found->pos.has_value());
        EXPECT_EQ(found->pos->text, "count * 8");
    }

    TEST(TypeTest, SeqIsSearchedBeforeInstances) {
        Type type = sample();
        type.instances.push_back(scalar("kind", FieldType::I8));

        const Attribute* found = type.find("kind");
        ASSERT_NE(found, nullptr);
        EXPECT_EQ(found->scalar, FieldType::U32);
    }

    TEST(TypeTest, MissingAttributeIsNull) {
        const Type type = sample();

        EXPECT_EQ(type.find("nope"), nullptr);
        EXPECT_EQ(type.find("Kind"), nullptr);
    }

    TEST(TypeTest, EmptyTypeFindsNothing) {
        const Type type{.name = "empty"};

        EXPECT_EQ(type.find("anything"), nullptr);
    }

    TEST(TypeTest, FindReturnsThePointerIntoTheType) {
        const Type type = sample();

        EXPECT_EQ(type.find("kind"), &type.seq[0]);
        EXPECT_EQ(type.find("records"), &type.instances[0]);
    }

    TEST(LayoutTest, DefaultIsEmpty) {
        const Layout layout{.id = "x"};

        EXPECT_TRUE(layout.title.empty());
        EXPECT_TRUE(layout.root.seq.empty());
        EXPECT_TRUE(layout.root.instances.empty());
        EXPECT_TRUE(layout.types.empty());
        EXPECT_TRUE(layout.formats.empty());
        EXPECT_TRUE(layout.magic.empty());
    }

    TEST(LayoutTest, FindsANamedType) {
        Layout layout{.id = "ring"};
        layout.types.emplace("header", sample());

        const Type* found = layout.findType("header");
        ASSERT_NE(found, nullptr);
        EXPECT_EQ(found->name, "header");
        EXPECT_NE(found->find("count"), nullptr);
    }

    TEST(LayoutTest, MissingTypeIsNull) {
        Layout layout{.id = "ring"};
        layout.types.emplace("header", sample());

        EXPECT_EQ(layout.findType("record"), nullptr);
        EXPECT_EQ(layout.findType(""), nullptr);
    }

    TEST(LayoutTest, TheRootIsNotANamedType) {
        Layout layout{.id = "ring"};
        layout.root.name = "ring";

        EXPECT_EQ(layout.findType("ring"), nullptr);
    }

    TEST(LayoutTest, FindTypeTakesAStringView) {
        Layout layout{.id = "ring"};
        layout.types.emplace("header", sample());
        const std::string name = "header_and_more";

        EXPECT_NE(layout.findType(std::string_view(name).substr(0, 6)),
                  nullptr);
    }

    TEST(LayoutTest, StoresCompiledFormatsByName) {
        Layout layout{.id = "ring"};
        auto price = DefaultFormatters::compile(
            {.kind = "scaled", .options = {{"digits", "2"}}});
        ASSERT_TRUE(price.has_value());
        layout.formats.emplace("price", std::move(*price));

        const auto found = layout.formats.find("price");
        ASSERT_NE(found, layout.formats.end());
        const shmscope::Value value{.data = std::int64_t{1505}, .width = 8};
        EXPECT_EQ(DefaultFormatters::apply(found->second, value), "15.05");
    }

    TEST(LayoutTest, KeepsMagicBytes) {
        Layout layout{.id = "ring"};
        for (const char c : std::string_view("SAMPLE01")) {
            layout.magic.push_back(static_cast<std::byte>(c));
        }

        ASSERT_EQ(layout.magic.size(), 8U);
        EXPECT_EQ(layout.magic.front(), std::byte{'S'});
        EXPECT_EQ(layout.magic.back(), std::byte{'1'});
    }

    TEST(AttributeKindTest, EveryKindHasADistinctName) {
        std::set<std::string_view> names;
        for (const auto kind : {AttributeKind::SCALAR, AttributeKind::STRING,
                                AttributeKind::BYTES, AttributeKind::CONTENTS,
                                AttributeKind::USER, AttributeKind::SWITCH}) {
            const auto name = shmscope::nameOf(kind);
            EXPECT_NE(name, "?");
            names.insert(name);
        }
        EXPECT_EQ(names.size(), 6U);
    }

    TEST(AttributeKindTest, Names) {
        EXPECT_EQ(shmscope::nameOf(AttributeKind::SCALAR), "scalar");
        EXPECT_EQ(shmscope::nameOf(AttributeKind::STRING), "string");
        EXPECT_EQ(shmscope::nameOf(AttributeKind::BYTES), "bytes");
        EXPECT_EQ(shmscope::nameOf(AttributeKind::CONTENTS), "contents");
        EXPECT_EQ(shmscope::nameOf(AttributeKind::USER), "user type");
        EXPECT_EQ(shmscope::nameOf(AttributeKind::SWITCH), "switch");
    }

}  // namespace
