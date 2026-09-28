#include "shmscope/yaml_reader.hpp"

#include <format>
#include <string>
#include <utility>
#include <vector>

#include <yaml-cpp/yaml.h>

namespace shmscope {

    namespace {
        Location locationOf(const YAML::Mark& mark) {
            if (mark.line < 0) return {};
            return {.line = mark.line + 1, .column = mark.column + 1};
        }

        class Converter {
        public:
            explicit Converter(std::string_view source) : source_(source) {}

            ReadResult convert(const YAML::Node& node, int depth) {
                const Location location = locationOf(node.Mark());
                if (depth > MAX_DOCUMENT_DEPTH) {
                    return fail("document is nested too deeply", location);
                }

                switch (node.Type()) {
                    case YAML::NodeType::Undefined:
                    case YAML::NodeType::Null:
                        return Node::null(location);
                    case YAML::NodeType::Scalar:
                        return Node::scalar(node.Scalar(), location);
                    case YAML::NodeType::Sequence:
                        return convertList(node, depth, location);
                    case YAML::NodeType::Map:
                        return convertMap(node, depth, location);
                }
                return fail("unknown YAML node", location);
            }

        private:
            ReadResult convertList(const YAML::Node& node, int depth,
                                   Location location) {
                std::vector<Node> items;
                items.reserve(node.size());

                for (const auto& child : node) {
                    auto item = convert(child, depth + 1);
                    if (!item) return item;

                    items.push_back(std::move(*item));
                }

                return Node::list(std::move(items), location);
            }

            ReadResult convertMap(const YAML::Node& node, int depth,
                                  Location location) {
                std::vector<Node::Entry> entries;
                entries.reserve(node.size());

                for (const auto& pair : node) {
                    const Location keyLocation = locationOf(pair.first.Mark());
                    if (!pair.first.IsScalar()) {
                        return fail("map keys must be plain text", keyLocation);
                    }

                    std::string key = pair.first.Scalar();
                    for (const auto& existing : entries) {
                        if (existing.key == key) {
                            return fail(std::format("duplicate key '{}'", key),
                                        keyLocation);
                        }
                    }
                    auto value = convert(pair.second, depth + 1);
                    if (!value) {
                        return value;
                    }
                    entries.push_back({.key = std::move(key),
                                       .value = std::move(*value),
                                       .location = keyLocation});
                }

                return Node::map(std::move(entries), location);
            }

            [[nodiscard]] ReadResult fail(std::string message,
                                          Location location) const {
                return std::unexpected(LoadError{.message = std::move(message),
                                                 .source = std::string(source_),
                                                 .location = location});
            }

            std::string_view source_;
        };
    }  // namespace

    ReadResult YamlReader::read(std::string_view text,
                                std::string_view source) {
        YAML::Node root;

        try {
            root = YAML::Load(std::string(text));
        } catch (const YAML::Exception& e) {
            return std::unexpected(LoadError{.message = e.msg,
                                             .source = std::string(source),
                                             .location = locationOf(e.mark)});
        }

        return Converter(source).convert(root, 0);
    }
}  // namespace shmscope
