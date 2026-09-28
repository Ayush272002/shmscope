#include "shmscope/layout/readers/json_reader.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace shmscope {

    namespace {

        using Json = nlohmann::ordered_json;

        Location locationAt(std::string_view text, std::size_t byte) {
            const std::size_t end = std::min(byte, text.size());
            Location location{.line = 1, .column = 1};
            for (std::size_t i = 0; i < end; ++i) {
                if (text[i] == '\n') {
                    ++location.line;
                    location.column = 1;
                } else {
                    ++location.column;
                }
            }
            return location;
        }

        class Converter {
        public:
            explicit Converter(std::string_view source) : source_(source) {}

            ReadResult convert(const Json& json, int depth,
                               const std::string& path) {
                if (depth > MAX_DOCUMENT_DEPTH) {
                    return fail("document is nested too deeply", "");
                }

                switch (json.type()) {
                    case Json::value_t::null:
                    case Json::value_t::discarded:
                        return Node::null();
                    case Json::value_t::string:
                        return Node::scalar(json.get<std::string>());
                    case Json::value_t::boolean:
                    case Json::value_t::number_integer:
                    case Json::value_t::number_unsigned:
                    case Json::value_t::number_float:
                        return Node::scalar(json.dump());
                    case Json::value_t::array:
                        return convertList(json, depth, path);
                    case Json::value_t::object:
                        return convertMap(json, depth, path);
                    case Json::value_t::binary:
                        break;
                }
                return fail("unsupported JSON value", path);
            }

        private:
            ReadResult convertList(const Json& json, int depth,
                                   const std::string& path) {
                std::vector<Node> items;
                items.reserve(json.size());
                std::size_t index = 0;
                for (const auto& child : json) {
                    auto item =
                        convert(child, depth + 1,
                                path + "[" + std::to_string(index) + "]");
                    if (!item) {
                        return item;
                    }
                    items.push_back(std::move(*item));
                    ++index;
                }
                return Node::list(std::move(items));
            }

            ReadResult convertMap(const Json& json, int depth,
                                  const std::string& path) {
                std::vector<Node::Entry> entries;
                entries.reserve(json.size());
                for (const auto& [key, child] : json.items()) {
                    auto value = convert(child, depth + 1,
                                         path.empty() ? key : path + "." + key);
                    if (!value) {
                        return value;
                    }
                    entries.push_back({.key = key, .value = std::move(*value)});
                }
                return Node::map(std::move(entries));
            }

            ReadResult fail(std::string message,
                            const std::string& path) const {
                return std::unexpected(LoadError{.message = std::move(message),
                                                 .source = std::string(source_),
                                                 .path = path});
            }

            std::string_view source_;
        };
    }  // namespace

    ReadResult JsonReader::read(std::string_view text,
                                std::string_view source) {
        Json root;
        try {
            root = Json::parse(text, nullptr, true, true);
        } catch (const Json::parse_error& e) {
            return std::unexpected(LoadError{
                .message = e.what(),
                .source = std::string(source),
                .location = locationAt(text, e.byte == 0 ? 0 : e.byte - 1)});
        }
        return Converter(source).convert(root, 0, "");
    }
}  // namespace shmscope
