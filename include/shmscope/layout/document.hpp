#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace shmscope {

    struct Location {
        int line = 0;
        int column = 0;

        [[nodiscard]] bool known() const noexcept { return line > 0; }
    };

    struct LoadError {
        std::string message{};
        std::string source{};
        std::string path{};
        Location location{};
    };

    [[nodiscard]] std::string describe(const LoadError& error);

    class Node {
    public:
        enum class Kind : std::uint8_t { NUL, SCALAR, LIST, MAP };

        struct Entry;

        Node() = default;

        [[nodiscard]] static Node null(Location location = {});
        [[nodiscard]] static Node scalar(std::string text,
                                         Location location = {});
        [[nodiscard]] static Node list(std::vector<Node> items,
                                       Location location = {});
        [[nodiscard]] static Node map(std::vector<Entry> entries,
                                      Location location = {});

        [[nodiscard]] Kind kind() const noexcept { return kind_; }
        [[nodiscard]] bool isNull() const noexcept {
            return kind_ == Kind::NUL;
        }
        [[nodiscard]] bool isScalar() const noexcept {
            return kind_ == Kind::SCALAR;
        }
        [[nodiscard]] bool isList() const noexcept {
            return kind_ == Kind::LIST;
        }
        [[nodiscard]] bool isMap() const noexcept { return kind_ == Kind::MAP; }

        [[nodiscard]] const std::string& text() const noexcept { return text_; }
        [[nodiscard]] const std::vector<Node>& items() const noexcept {
            return items_;
        }
        [[nodiscard]] const std::vector<Entry>& entries() const noexcept {
            return entries_;
        }
        [[nodiscard]] Location location() const noexcept { return location_; }

        [[nodiscard]] const Node* find(std::string_view key) const noexcept;

    private:
        Kind kind_ = Kind::NUL;
        std::string text_{};
        std::vector<Node> items_{};
        std::vector<Entry> entries_{};
        Location location_{};
    };

    struct Node::Entry {
        std::string key{};
        Node value;
        Location location{};
    };

    [[nodiscard]] std::string_view nameOf(Node::Kind kind) noexcept;

}  // namespace shmscope
