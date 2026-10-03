#include "ring.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "shmscope/core/decode.hpp"
#include "shmscope/layout/document.hpp"
#include "shmscope/layout/expression.hpp"
#include "shmscope/layout/load_layout.hpp"
#include "shmscope/layout/model.hpp"
#include "shmscope/layout/placement.hpp"
#include "shmscope/ui/field_panel.hpp"

namespace {

    using shmscope::Attribute;
    using shmscope::Layout;
    using shmscope::Placement;
    using shmscope::Type;

    using Bytes = std::vector<std::byte>;

    std::filesystem::path example(std::string_view name) {
        return std::filesystem::path(__FILE__)
                   .parent_path()
                   .parent_path()
                   .parent_path() /
               "examples" / name;
    }

    Layout loaded(std::string_view name) {
        auto layout = shmscope::loadLayout(example(name));
        if (!layout) {
            ADD_FAILURE() << shmscope::describe(layout.error());
            return {};
        }
        return std::move(*layout);
    }

    std::string code(const std::optional<shmscope::Expression>& expression) {
        return expression ? shmscope::disassemble(expression->program) : "-";
    }

    std::string summary(const Attribute& attribute) {
        std::string text = std::format(
            "{}:{}:{}:{}:{}:size={}:pos={}:repeat={}:format={}", attribute.id,
            shmscope::nameOf(attribute.kind),
            shmscope::nameOf(attribute.scalar), attribute.userType,
            attribute.contents.size(), code(attribute.size),
            code(attribute.pos), code(attribute.repeatCount), attribute.format);
        if (attribute.switchOn) {
            text += ":on=" + code(attribute.switchOn->on);
            for (const auto& option : attribute.switchOn->cases) {
                text += std::format(",{}->{}", option.value.value_or(0),
                                    option.type);
            }
        }
        return text;
    }

    std::string summary(const Type& type) {
        std::string text = type.name + "{";
        for (const auto& attribute : type.seq) text += summary(attribute) + ";";
        text += "|";
        for (const auto& attribute : type.instances) {
            text += summary(attribute) + ";";
        }
        return text + "}";
    }

    std::string summary(const Layout& layout) {
        std::string text = std::format("{}:{}:{}:", layout.id, layout.title,
                                       layout.magic.size());
        text += summary(layout.root);
        for (const auto& [name, type] : layout.types) text += summary(type);
        for (const auto& [name, format] : layout.formats) text += name + ",";
        return text;
    }

    class RingTest : public ::testing::Test {
    protected:
        void SetUp() override {
            ring::writeHeader(bytes_, "demo-ticker", STARTED, 4242);
            ring::writeRecord(bytes_, 0, STARTED + 1,
                              ring::Trade{.price = 1'002'500,
                                          .quantity = 7,
                                          .side = ring::Side::SELL});
            ring::writeRecord(bytes_, 1, STARTED + 2,
                              ring::Quote{.bidPrice = 1'002'495,
                                          .bidQuantity = 30,
                                          .askPrice = 1'002'505,
                                          .askQuantity = 40});
            ring::publish(bytes_, 2, STARTED + 2);
        }

        std::string value(const Placement& placement, std::string_view path) {
            const auto* field = placement.find(path);
            if (field == nullptr) return "<missing>";

            return shmscope::formatValue(layout_, *field, bytes_);
        }

        static constexpr std::uint64_t STARTED = 1'700'000'000'000'000'000ULL;

        Bytes bytes_ = Bytes(ring::BYTES);
        Layout layout_ = loaded("ring.ksy");
    };

    TEST(RingLayoutTest, YamlAndJsonMatchTheKsy) {
        const auto ksy = summary(loaded("ring.ksy"));

        EXPECT_EQ(summary(loaded("ring.yaml")), ksy);
        EXPECT_EQ(summary(loaded("ring.json")), ksy);
    }

    TEST(RingLayoutTest, MagicComesFromTheHeader) {
        const auto layout = loaded("ring.ksy");

        ASSERT_EQ(layout.magic.size(), ring::MAGIC.size());
        for (std::size_t i = 0; i < ring::MAGIC.size(); ++i) {
            EXPECT_EQ(static_cast<char>(layout.magic[i]), ring::MAGIC[i]);
        }
    }

    TEST_F(RingTest, PlacesWithoutProblems) {
        const auto placement = shmscope::place(layout_, bytes_);

        EXPECT_TRUE(placement.problems.empty())
            << placement.problems.front().path << ": "
            << placement.problems.front().message;
        EXPECT_FALSE(placement.truncated);
    }

    TEST_F(RingTest, HeaderMatchesTheWriter) {
        const auto placement = shmscope::place(layout_, bytes_);

        EXPECT_EQ(value(placement, "header.version"), "1");
        EXPECT_EQ(value(placement, "header.record_size"), "64");
        EXPECT_EQ(value(placement, "header.capacity"), "256");
        EXPECT_EQ(value(placement, "header.records_offset"),
                  "0x0000000000000080");
        EXPECT_EQ(value(placement, "header.name"), "\"demo-ticker\"");
        EXPECT_EQ(value(placement, "hot.writer_pid"), "4242");
        EXPECT_EQ(value(placement, "hot.sequence"), "2");
    }

    TEST_F(RingTest, LatestIsTheNewestRecord) {
        const auto placement = shmscope::place(layout_, bytes_);

        EXPECT_EQ(placement.find("latest")->offset,
                  ring::RECORDS_OFFSET + ring::RECORD_SIZE);
        EXPECT_EQ(value(placement, "latest.sequence"), "1");
        EXPECT_EQ(value(placement, "latest.kind"), "quote");
        EXPECT_EQ(value(placement, "latest.body.bid_price"), "100.2495");
        EXPECT_EQ(value(placement, "latest.body.ask_price"), "100.2505");
        EXPECT_EQ(value(placement, "latest.body.ask_quantity"), "40");
    }

    TEST_F(RingTest, RecordsSwitchOnTheirKind) {
        const auto placement = shmscope::place(layout_, bytes_);

        EXPECT_EQ(value(placement, "records[0].kind"), "trade");
        EXPECT_EQ(value(placement, "records[0].body.price"), "100.2500");
        EXPECT_EQ(value(placement, "records[0].body.side"), "sell");
        EXPECT_EQ(value(placement, "records[0].body.quantity"), "7");
        EXPECT_EQ(value(placement, "records[1].kind"), "quote");
        EXPECT_EQ(placement.find("records[255]")->offset,
                  ring::RECORDS_OFFSET + 255 * ring::RECORD_SIZE);
    }

    TEST_F(RingTest, TheRingWrapsAtItsCapacity) {
        ring::writeRecord(bytes_, ring::CAPACITY, STARTED + 3,
                          ring::Trade{.price = 990'000, .quantity = 1});
        ring::publish(bytes_, ring::CAPACITY + 1, STARTED + 3);
        const auto placement = shmscope::place(layout_, bytes_);

        EXPECT_EQ(placement.find("latest")->offset, ring::RECORDS_OFFSET);
        EXPECT_EQ(value(placement, "latest.sequence"), "256");
        EXPECT_EQ(value(placement, "latest.body.price"), "99.0000");
        EXPECT_EQ(value(placement, "latest.body.side"), "buy");
    }

    TEST_F(RingTest, AnEmptyRingStillPlaces) {
        Bytes empty(ring::BYTES);
        ring::writeHeader(empty, "fresh", STARTED, 1);
        const auto placement = shmscope::place(layout_, empty);

        EXPECT_TRUE(placement.problems.empty());
        EXPECT_EQ(placement.find("latest")->offset,
                  ring::RECORDS_OFFSET + 255 * ring::RECORD_SIZE);
    }

    TEST_F(RingTest, TimestampsAreWallClock) {
        const auto placement = shmscope::place(layout_, bytes_);

        EXPECT_TRUE(value(placement, "header.started_at").starts_with("2023-"))
            << value(placement, "header.started_at");
    }

}  // namespace
