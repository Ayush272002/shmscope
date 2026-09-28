#include "shmscope/document.hpp"

#include <algorithm>
#include <format>
#include <utility>

namespace shmscope {

    std::string describe(const LoadError& error) {
        std::string text = error.source.empty() ? "<input>" : error.source;
        if (error.location.known()) {
            text += std::format(":{}", error.location.line);
            if (error.location.column > 0)
                text += std::format(":{}", error.location.column);
        }

        text += ": ";
        if (!error.path.empty()) text += error.path + ": ";

        text += error.message;
        return text;
    }

    Node Node::null(Location location) {
        Node node;
        node.kind_ = Kind::NUL;
        node.location_ = location;
        return node;
    }

    Node Node::scalar(std::string text, Location location) {
        Node node;
        node.kind_ = Kind::SCALAR;
        node.text_ = std::move(text);
        node.location_ = location;
        return node;
    }

    Node Node::list(std::vector<Node> items, Location location) {
        Node node;
        node.kind_ = Kind::LIST;
        node.items_ = std::move(items);
        node.location_ = location;
        return node;
    }

    Node Node::map(std::vector<Entry> entries, Location location) {
        Node node;
        node.kind_ = Kind::MAP;
        node.entries_ = std::move(entries);
        node.location_ = location;
        return node;
    }

    const Node* Node::find(std::string_view key) const noexcept {
        const auto it = std::ranges::find(entries_, key, &Entry::key);
        return it == entries_.end() ? nullptr : &it->value;
    }

    std::string_view nameOf(Node::Kind kind) noexcept {
        switch (kind) {
            case Node::Kind::NUL:
                return "null";
            case Node::Kind::SCALAR:
                return "scalar";
            case Node::Kind::LIST:
                return "list";
            case Node::Kind::MAP:
                return "map";
        }
        return "?";
    }

}  // namespace shmscope
