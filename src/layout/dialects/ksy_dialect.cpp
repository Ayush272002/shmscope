#include "shmscope/layout/dialects/ksy_dialect.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "shmscope/core/decode.hpp"
#include "shmscope/core/default_formatters.hpp"
#include "shmscope/core/format.hpp"
#include "shmscope/layout/document.hpp"
#include "shmscope/layout/expression.hpp"
#include "shmscope/layout/model.hpp"

namespace shmscope {

    namespace {

        using Status = std::expected<void, LoadError>;
        using Keys = std::span<const std::string_view>;

        constexpr std::size_t MAX_MAGIC_DEPTH = 16;
        constexpr std::string_view SHMSCOPE_PREFIX = "-shmscope-";

        struct Primitive {
            std::string_view name;
            FieldType type;
        };

        constexpr std::array<Primitive, 10> PRIMITIVES = {{
            {"u1", FieldType::U8},
            {"u2", FieldType::U16},
            {"u4", FieldType::U32},
            {"u8", FieldType::U64},
            {"s1", FieldType::I8},
            {"s2", FieldType::I16},
            {"s4", FieldType::I32},
            {"s8", FieldType::I64},
            {"f4", FieldType::F32},
            {"f8", FieldType::F64},
        }};

        constexpr std::array<std::string_view, 6> ROOT_KEYS = {
            "meta", "seq", "types", "instances", "doc", "doc-ref"};
        constexpr std::array<std::string_view, 3> ROOT_UNSUPPORTED = {
            "enums", "params", "to-string"};

        constexpr std::array<std::string_view, 13> META_KEYS = {
            "id",        "title",          "endian",
            "encoding",  "file-extension", "ks-version",
            "license",   "application",    "xref",
            "tags",      "ks-debug",       "ks-opaque-types",
            "bit-endian"};
        constexpr std::array<std::string_view, 1> META_UNSUPPORTED = {
            "imports"};

        constexpr std::array<std::string_view, 4> TYPE_KEYS = {
            "seq", "instances", "doc", "doc-ref"};
        constexpr std::array<std::string_view, 5> TYPE_UNSUPPORTED = {
            "types", "enums", "params", "meta", "to-string"};

        constexpr std::array<std::string_view, 9> SEQ_KEYS = {
            "id",          "type",     "size", "contents", "repeat",
            "repeat-expr", "encoding", "doc",  "doc-ref"};
        constexpr std::array<std::string_view, 9> INSTANCE_KEYS = {
            "pos",         "type",     "size", "contents", "repeat",
            "repeat-expr", "encoding", "doc",  "doc-ref"};
        constexpr std::array<std::string_view, 14> ATTRIBUTE_UNSUPPORTED = {
            "if",      "process",   "enum",      "terminator",   "consume",
            "include", "eos-error", "pad-right", "repeat-until", "size-eos",
            "io",      "value",     "valid",     "parent"};

        constexpr std::array<std::string_view, 2> SWITCH_KEYS = {"switch-on",
                                                                 "cases"};

        constexpr std::array<std::string_view, 0> NONE = {};

        constexpr std::array<std::string_view, 1> ROOT_SHMSCOPE_KEYS = {
            KsyDialect::FORMATS_KEY};

        constexpr std::array<std::string_view, 1> ATTRIBUTE_SHMSCOPE_KEYS = {
            KsyDialect::FORMAT_KEY};

        bool contains(Keys keys, const std::string_view key) {
            return std::ranges::find(keys, key) != keys.end();
        }

        bool isIdentifier(std::string_view text) {
            if (text.empty() || text.front() < 'a' || text.front() > 'z')
                return false;

            return std::ranges::all_of(text, [](char c) {
                return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                       c == '_';
            });
        }

        std::string lower(const std::string_view text) {
            std::string out(text);
            std::ranges::transform(out, out.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });

            return out;
        }

        std::string join(std::string_view path, std::string_view key) {
            if (path.empty()) {
                return std::string(key);
            }
            return std::format("{}.{}", path, key);
        }

        std::string at(std::string_view path, std::size_t index) {
            return std::format("{}[{}]", path, index);
        }

        std::optional<std::uint64_t> parseUnsigned(std::string_view text) {
            int base = 10;
            if (text.starts_with("0x") || text.starts_with("0X")) {
                text.remove_prefix(2);
                base = 16;
            }

            std::uint64_t value = 0;
            const auto* end = text.data() + text.size();
            const auto [ptr, ec] =
                std::from_chars(text.data(), end, value, base);
            if (text.empty() || ec != std::errc{} || ptr != end) {
                return std::nullopt;
            }
            return value;
        }

        std::optional<std::uint64_t> parseInteger(std::string_view text) {
            if (!text.starts_with('-')) return parseUnsigned(text);

            const auto magnitude = parseUnsigned(text.substr(1));
            constexpr auto limit =
                static_cast<std::uint64_t>(
                    std::numeric_limits<std::int64_t>::max()) +
                1;

            if (!magnitude || *magnitude > limit) return std::nullopt;
            return 0 - *magnitude;
        }

        const Node::Entry* entryOf(const Node& map, std::string_view key) {
            const auto it =
                std::ranges::find(map.entries(), key, &Node::Entry::key);
            return it == map.entries().end() ? nullptr : &*it;
        }

        Location locationOf(const Node& map, const std::string_view key) {
            const auto* entry = entryOf(map, key);
            if (entry == nullptr) return map.location();

            return entry->value.location().known() ? entry->value.location()
                                                   : entry->location;
        }

        Location within(const Location start, const std::size_t column) {
            if (!start.known() || column == 0) return start;

            return {.line = start.line,
                    .column = start.column + static_cast<int>(column) - 1};
        }

        class Builder {
        public:
            explicit Builder(std::string_view source) : source_(source) {}

            LayoutResult build(const Node& root) {
                if (!root.isMap()) {
                    return fail(root.location(), "",
                                "a layout must be a map of keys");
                }
                if (auto status =
                        checkKeys(root, "", ROOT_KEYS, ROOT_UNSUPPORTED,
                                  ROOT_SHMSCOPE_KEYS);
                    !status) {
                    return std::unexpected(std::move(status.error()));
                }

                const Node* meta = root.find("meta");
                if (meta == nullptr) {
                    return fail(root.location(), "", "missing 'meta'");
                }
                if (auto status = parseMeta(*meta); !status) {
                    return std::unexpected(std::move(status.error()));
                }

                if (const Node* formats = root.find(KsyDialect::FORMATS_KEY)) {
                    if (auto status = parseFormats(*formats); !status) {
                        return std::unexpected(std::move(status.error()));
                    }
                }

                if (const Node* types = root.find("types")) {
                    if (auto status = parseTypes(*types); !status) {
                        return std::unexpected(std::move(status.error()));
                    }
                }

                layout_.root.name = layout_.id;
                layout_.root.location = root.location();
                if (auto status = parseBody(layout_.root, root, ""); !status) {
                    return std::unexpected(std::move(status.error()));
                }

                if (auto status = checkReferences(layout_.root, ""); !status) {
                    return std::unexpected(std::move(status.error()));
                }
                for (const auto& [name, type] : layout_.types) {
                    if (auto status =
                            checkReferences(type, join("types", name));
                        !status) {
                        return std::unexpected(std::move(status.error()));
                    }
                }

                layout_.magic = magicOf(layout_.root, 0);
                return std::move(layout_);
            }

        private:
            [[nodiscard]] LoadError error(const Location location,
                                          std::string_view path,
                                          std::string message) const {
                return LoadError{.message = std::move(message),
                                 .source = std::string(source_),
                                 .path = std::string(path),
                                 .location = location};
            }

            [[nodiscard]] std::unexpected<LoadError> fail(
                const Location location, const std::string_view path,
                std::string message) const {
                return std::unexpected(
                    error(location, path, std::move(message)));
            }

            [[nodiscard]] Status checkKeys(const Node& map,
                                           std::string_view path, Keys allowed,
                                           Keys unsupported,
                                           Keys shmscopeKeys) const {
                for (const auto& entry : map.entries()) {
                    const std::string_view key = entry.key;

                    if (contains(allowed, key) || contains(shmscopeKeys, key))
                        continue;

                    const auto where = join(path, key);
                    if (key.starts_with(SHMSCOPE_PREFIX)) {
                        return fail(
                            entry.location, where,
                            std::format("unknown shmscope key '{}'", key));
                    }

                    if (key.starts_with('-')) continue;

                    if (contains(unsupported, key)) {
                        return fail(entry.location, where,
                                    std::format("'{}' is not supported by "
                                                "shmscope yet",
                                                key));
                    }

                    return fail(entry.location, where,
                                std::format("unknown key '{}'", key));
                }

                return {};
            }

            [[nodiscard]] std::expected<std::string, LoadError> scalar(
                const Node& node, std::string_view path,
                std::string_view what) const {
                if (!node.isScalar())
                    return fail(
                        node.location(), path,
                        std::format("{} must be a single value, not a {}", what,
                                    nameOf(node.kind())));

                return node.text();
            }

            [[nodiscard]] std::expected<Expression, LoadError> expression(
                const Node& node, const std::string_view path,
                std::string_view what) const {
                auto text = scalar(node, path, what);

                if (!text) return std::unexpected(std::move(text.error()));

                if (text->empty()) {
                    return fail(node.location(), path,
                                std::format("{} is empty", what));
                }

                auto program = compile(*text);
                if (!program) {
                    return fail(within(node.location(), program.error().column),
                                path,
                                std::format("in \"{}\" at column {}: {}", *text,
                                            program.error().column,
                                            program.error().message));
                }

                return Expression{.text = std::move(*text),
                                  .location = node.location(),
                                  .program = std::move(*program)};
            }

            Status parseMeta(const Node& meta) {
                if (!meta.isMap()) {
                    return fail(meta.location(), "meta",
                                "'meta' must be a map");
                }
                if (auto status = checkKeys(meta, "meta", META_KEYS,
                                            META_UNSUPPORTED, NONE);
                    !status) {
                    return status;
                }

                const Node* id = meta.find("id");
                if (id == nullptr) {
                    return fail(meta.location(), "meta", "missing 'id'");
                }
                auto idText = scalar(*id, "meta.id", "'id'");
                if (!idText) {
                    return std::unexpected(std::move(idText.error()));
                }
                if (!isIdentifier(*idText)) {
                    return fail(id->location(), "meta.id",
                                std::format("'{}' is not a valid id; use "
                                            "lower_snake_case",
                                            *idText));
                }
                layout_.id = std::move(*idText);

                if (const Node* title = meta.find("title")) {
                    auto text = scalar(*title, "meta.title", "'title'");
                    if (!text) {
                        return std::unexpected(std::move(text.error()));
                    }
                    layout_.title = std::move(*text);
                }

                if (const Node* endian = meta.find("endian")) {
                    if (endian->isMap()) {
                        return fail(endian->location(), "meta.endian",
                                    "switching endianness is not supported by "
                                    "shmscope yet");
                    }
                    auto text = scalar(*endian, "meta.endian", "'endian'");
                    if (!text) {
                        return std::unexpected(std::move(text.error()));
                    }
                    if (*text == "be") {
                        return fail(endian->location(), "meta.endian",
                                    "big-endian layouts are not supported by "
                                    "shmscope yet");
                    }
                    if (*text != "le") {
                        return fail(endian->location(), "meta.endian",
                                    std::format("'endian' must be le or be, "
                                                "got '{}'",
                                                *text));
                    }
                    littleEndian_ = true;
                }

                if (const Node* encoding = meta.find("encoding")) {
                    auto text =
                        scalar(*encoding, "meta.encoding", "'encoding'");
                    if (!text) {
                        return std::unexpected(std::move(text.error()));
                    }
                    encoding_ = std::move(*text);
                }
                return {};
            }

            Status parseFormats(const Node& formats) {
                const std::string path(KsyDialect::FORMATS_KEY);
                if (!formats.isMap()) {
                    return fail(formats.location(), path,
                                "formats must be a map of names to formats");
                }

                for (const auto& entry : formats.entries()) {
                    const auto where = join(path, entry.key);
                    if (!isIdentifier(entry.key)) {
                        return fail(entry.location, where,
                                    std::format("'{}' is not a valid format "
                                                "name; use lower_snake_case",
                                                entry.key));
                    }
                    auto spec = formatSpec(entry.value, where);
                    if (!spec) {
                        return std::unexpected(std::move(spec.error()));
                    }
                    auto compiled = DefaultFormatters::compile(*spec);
                    if (!compiled) {
                        return fail(entry.value.location(), where,
                                    std::move(compiled.error()));
                    }
                    layout_.formats.insert_or_assign(entry.key,
                                                     std::move(*compiled));
                }
                return {};
            }

            [[nodiscard]] std::expected<FormatSpec, LoadError> formatSpec(
                const Node& node, std::string_view path) const {
                if (!node.isMap()) {
                    return fail(node.location(), path,
                                "a format must be a map with a 'kind'");
                }

                FormatSpec spec;
                const Node* kind = node.find("kind");
                if (kind == nullptr) {
                    return fail(node.location(), path, "missing 'kind'");
                }
                auto kindText = scalar(*kind, join(path, "kind"), "'kind'");
                if (!kindText) {
                    return std::unexpected(std::move(kindText.error()));
                }
                spec.kind = std::move(*kindText);

                for (const auto& entry : node.entries()) {
                    if (entry.key == "kind") {
                        continue;
                    }
                    const auto where = join(path, entry.key);
                    if (entry.key == "values") {
                        if (!entry.value.isMap()) {
                            return fail(entry.location, where,
                                        "'values' must be a map of numbers to "
                                        "names");
                        }
                        for (const auto& value : entry.value.entries()) {
                            auto name = scalar(
                                value.value, join(where, value.key), "a name");
                            if (!name) {
                                return std::unexpected(std::move(name.error()));
                            }
                            spec.entries.emplace_back(value.key,
                                                      std::move(*name));
                        }
                        continue;
                    }
                    auto option = scalar(entry.value, where,
                                         std::format("'{}'", entry.key));
                    if (!option) {
                        return std::unexpected(std::move(option.error()));
                    }
                    spec.options.insert_or_assign(entry.key,
                                                  std::move(*option));
                }
                return spec;
            }

            Status parseTypes(const Node& types) {
                if (!types.isMap()) {
                    return fail(types.location(), "types",
                                "'types' must be a map of names to types");
                }

                for (const auto& entry : types.entries()) {
                    const auto where = join("types", entry.key);
                    if (!isIdentifier(entry.key)) {
                        return fail(entry.location, where,
                                    std::format("'{}' is not a valid type "
                                                "name; use lower_snake_case",
                                                entry.key));
                    }
                    if (entry.key == layout_.id) {
                        return fail(entry.location, where,
                                    std::format("type '{}' has the same name "
                                                "as the layout",
                                                entry.key));
                    }
                    if (!entry.value.isMap()) {
                        return fail(entry.value.location(), where,
                                    "a type must be a map");
                    }
                    if (auto status = checkKeys(entry.value, where, TYPE_KEYS,
                                                TYPE_UNSUPPORTED, NONE);
                        !status) {
                        return status;
                    }

                    Type type{.name = entry.key, .location = entry.location};
                    if (auto status = parseBody(type, entry.value, where);
                        !status) {
                        return status;
                    }
                    layout_.types.emplace(entry.key, std::move(type));
                }
                return {};
            }

            Status parseBody(Type& type, const Node& node,
                             const std::string& path) {
                if (const Node* seq = node.find("seq")) {
                    const auto where = join(path, "seq");
                    if (!seq->isList()) {
                        return fail(seq->location(), where,
                                    "'seq' must be a list of attributes");
                    }
                    for (std::size_t i = 0; i < seq->items().size(); ++i) {
                        auto attribute = parseAttribute(
                            seq->items()[i], at(where, i), false, "");
                        if (!attribute) {
                            return std::unexpected(
                                std::move(attribute.error()));
                        }
                        if (auto status = addAttribute(type.seq, type,
                                                       std::move(*attribute),
                                                       at(where, i));
                            !status) {
                            return status;
                        }
                    }
                }

                if (const Node* instances = node.find("instances")) {
                    const auto where = join(path, "instances");
                    if (!instances->isMap()) {
                        return fail(instances->location(), where,
                                    "'instances' must be a map of names to "
                                    "attributes");
                    }
                    for (const auto& entry : instances->entries()) {
                        const auto here = join(where, entry.key);
                        if (!isIdentifier(entry.key)) {
                            return fail(entry.location, here,
                                        std::format("'{}' is not a valid id; "
                                                    "use lower_snake_case",
                                                    entry.key));
                        }
                        auto attribute =
                            parseAttribute(entry.value, here, true, entry.key);
                        if (!attribute) {
                            return std::unexpected(
                                std::move(attribute.error()));
                        }
                        if (auto status =
                                addAttribute(type.instances, type,
                                             std::move(*attribute), here);
                            !status) {
                            return status;
                        }
                    }
                }
                return {};
            }

            Status addAttribute(std::vector<Attribute>& list, const Type& type,
                                Attribute attribute,
                                std::string_view path) const {
                if (!attribute.id.empty() &&
                    type.find(attribute.id) != nullptr) {
                    return fail(attribute.location, path,
                                std::format("'{}' is defined twice in '{}'",
                                            attribute.id, type.name));
                }
                list.push_back(std::move(attribute));
                return {};
            }

            std::expected<Attribute, LoadError> parseAttribute(
                const Node& node, const std::string& path, bool instance,
                std::string id) {
                if (!node.isMap()) {
                    return fail(node.location(), path,
                                "an attribute must be a map");
                }
                if (!instance && node.find("pos") != nullptr) {
                    return fail(locationOf(node, "pos"), join(path, "pos"),
                                "'pos' is only allowed in instances");
                }
                if (instance && node.find("id") != nullptr) {
                    return fail(locationOf(node, "id"), join(path, "id"),
                                "instances take their id from their key");
                }
                if (auto status = checkKeys(
                        node, path,
                        instance ? Keys(INSTANCE_KEYS) : Keys(SEQ_KEYS),
                        ATTRIBUTE_UNSUPPORTED, ATTRIBUTE_SHMSCOPE_KEYS);
                    !status) {
                    return std::unexpected(std::move(status.error()));
                }

                Attribute attribute{.id = std::move(id),
                                    .location = node.location()};

                if (const Node* idNode = node.find("id")) {
                    auto text = scalar(*idNode, join(path, "id"), "'id'");
                    if (!text) {
                        return std::unexpected(std::move(text.error()));
                    }
                    if (!isIdentifier(*text)) {
                        return fail(idNode->location(), join(path, "id"),
                                    std::format("'{}' is not a valid id; use "
                                                "lower_snake_case",
                                                *text));
                    }
                    attribute.id = std::move(*text);
                }

                if (const Node* size = node.find("size")) {
                    auto parsed =
                        expression(*size, join(path, "size"), "'size'");
                    if (!parsed) {
                        return std::unexpected(std::move(parsed.error()));
                    }
                    attribute.size = std::move(*parsed);
                }

                const Node* contents = node.find("contents");
                const Node* type = node.find("type");
                if (contents != nullptr) {
                    if (type != nullptr || attribute.size) {
                        return fail(locationOf(node, "contents"),
                                    join(path, "contents"),
                                    "'contents' cannot be combined with "
                                    "'type' or 'size'");
                    }
                    auto bytes =
                        parseContents(*contents, join(path, "contents"));
                    if (!bytes) {
                        return std::unexpected(std::move(bytes.error()));
                    }
                    attribute.kind = AttributeKind::CONTENTS;
                    attribute.contents = std::move(*bytes);
                } else if (type != nullptr) {
                    if (auto status = parseType(attribute, node, *type, path);
                        !status) {
                        return std::unexpected(std::move(status.error()));
                    }
                } else if (attribute.size) {
                    attribute.kind = AttributeKind::BYTES;
                } else {
                    return fail(node.location(), path,
                                "needs a 'type', 'contents' or 'size'");
                }

                if (node.find("encoding") != nullptr &&
                    attribute.kind != AttributeKind::STRING) {
                    return fail(locationOf(node, "encoding"),
                                join(path, "encoding"),
                                "'encoding' only applies to 'str'");
                }

                if (auto status = parseRepeat(attribute, node, path); !status) {
                    return std::unexpected(std::move(status.error()));
                }

                if (instance) {
                    const Node* pos = node.find("pos");
                    if (pos == nullptr) {
                        return fail(node.location(), path,
                                    "instances need a 'pos'");
                    }
                    auto parsed = expression(*pos, join(path, "pos"), "'pos'");
                    if (!parsed) {
                        return std::unexpected(std::move(parsed.error()));
                    }
                    attribute.pos = std::move(*parsed);
                }

                if (const Node* format = node.find(KsyDialect::FORMAT_KEY)) {
                    if (auto status = parseFormatReference(
                            attribute, *format,
                            join(path, KsyDialect::FORMAT_KEY));
                        !status) {
                        return std::unexpected(std::move(status.error()));
                    }
                }

                return attribute;
            }

            Status parseType(Attribute& attribute, const Node& node,
                             const Node& type,
                             const std::string& attributePath) {
                const auto path = join(attributePath, "type");
                if (type.isMap()) {
                    return parseSwitch(attribute, type, path);
                }
                auto text = scalar(type, path, "'type'");
                if (!text) {
                    return std::unexpected(std::move(text.error()));
                }

                if (auto primitive = parsePrimitive(*text, type, path)) {
                    if (!*primitive) {
                        return std::unexpected(std::move(primitive->error()));
                    }
                    if (attribute.size) {
                        return fail(locationOf(node, "size"),
                                    join(attributePath, "size"),
                                    std::format("'size' cannot be used with "
                                                "'{}'",
                                                *text));
                    }
                    attribute.kind = AttributeKind::SCALAR;
                    attribute.scalar = **primitive;
                    return {};
                }

                if (*text == "str") {
                    return parseString(attribute, node, path);
                }
                if (*text == "strz") {
                    return fail(type.location(), path,
                                "'strz' is not supported by shmscope yet; use "
                                "'str' with a 'size'");
                }
                if (text->size() > 1 && text->front() == 'b' &&
                    std::ranges::all_of(text->substr(1), [](char c) {
                        return c >= '0' && c <= '9';
                    })) {
                    return fail(type.location(), path,
                                "bit fields are not supported by shmscope yet");
                }
                if (text->find('(') != std::string::npos) {
                    return fail(type.location(), path,
                                "parameterised types are not supported by "
                                "shmscope yet");
                }
                if (text->find("::") != std::string::npos) {
                    return fail(type.location(), path,
                                "nested type paths are not supported by "
                                "shmscope yet");
                }
                if (!isIdentifier(*text)) {
                    return fail(type.location(), path,
                                std::format("unknown type '{}'", *text));
                }

                attribute.kind = AttributeKind::USER;
                attribute.userType = std::move(*text);
                return {};
            }

            [[nodiscard]] std::optional<std::expected<FieldType, LoadError>>
            parsePrimitive(const std::string_view text, const Node& type,
                           const std::string& path) const {
                std::string_view base = text;
                bool explicitLittle = false;
                if (base.size() > 2 && base.ends_with("le")) {
                    base.remove_suffix(2);
                    explicitLittle = true;
                } else if (base.size() > 2 && base.ends_with("be")) {
                    base.remove_suffix(2);
                    if (std::ranges::find(PRIMITIVES, base, &Primitive::name) !=
                        PRIMITIVES.end()) {
                        return std::unexpected(
                            error(type.location(), path,
                                  "big-endian types are not supported by "
                                  "shmscope yet"));
                    }
                    return std::nullopt;
                }

                const auto found =
                    std::ranges::find(PRIMITIVES, base, &Primitive::name);
                if (found == PRIMITIVES.end()) {
                    return std::nullopt;
                }
                if (explicitLittle && widthOf(found->type) == 1) {
                    return std::unexpected(
                        error(type.location(), path,
                              std::format("'{}' has one byte, so it takes no "
                                          "endianness",
                                          base)));
                }
                if (!explicitLittle && widthOf(found->type) > 1 &&
                    !littleEndian_) {
                    return std::unexpected(error(
                        type.location(), path,
                        std::format("'{}' needs 'meta: endian: le', or write "
                                    "'{}le'",
                                    base, base)));
                }
                return found->type;
            }

            Status parseString(Attribute& attribute, const Node& node,
                               const std::string& path) const {
                if (!attribute.size) {
                    return fail(node.location(), path, "'str' needs a 'size'");
                }
                std::string encoding = encoding_;
                if (const Node* own = node.find("encoding")) {
                    auto text =
                        scalar(*own, join(path, "encoding"), "'encoding'");
                    if (!text) {
                        return std::unexpected(std::move(text.error()));
                    }
                    encoding = std::move(*text);
                }
                if (encoding.empty()) {
                    return fail(node.location(), path,
                                "'str' needs an 'encoding' here or in 'meta'");
                }
                const auto normalised = lower(encoding);
                if (normalised != "ascii" && normalised != "utf-8" &&
                    normalised != "utf8") {
                    return fail(node.location(), path,
                                std::format("encoding '{}' is not supported "
                                            "by shmscope yet; use ASCII or "
                                            "UTF-8",
                                            encoding));
                }
                attribute.kind = AttributeKind::STRING;
                return {};
            }

            Status parseSwitch(Attribute& attribute, const Node& type,
                               const std::string& path) const {
                if (auto status =
                        checkKeys(type, path, SWITCH_KEYS, NONE, NONE);
                    !status) {
                    return status;
                }
                const Node* on = type.find("switch-on");
                const Node* cases = type.find("cases");
                if (on == nullptr || cases == nullptr) {
                    return fail(type.location(), path,
                                "a switch needs 'switch-on' and 'cases'");
                }
                auto onExpression =
                    expression(*on, join(path, "switch-on"), "'switch-on'");
                if (!onExpression) {
                    return std::unexpected(std::move(onExpression.error()));
                }
                if (!cases->isMap() || cases->entries().empty()) {
                    return fail(cases->location(), join(path, "cases"),
                                "'cases' must map values to type names");
                }

                SwitchType switchType{.on = std::move(*onExpression)};
                for (const auto& entry : cases->entries()) {
                    const auto where = join(join(path, "cases"), entry.key);
                    SwitchCase switchCase{.location = entry.location};
                    if (entry.key != "_") {
                        const auto value = parseInteger(entry.key);
                        if (!value) {
                            return fail(entry.location, where,
                                        std::format("case '{}' must be an "
                                                    "integer or _",
                                                    entry.key));
                        }
                        switchCase.value = *value;
                    }
                    for (const auto& existing : switchType.cases) {
                        if (existing.value == switchCase.value) {
                            return fail(entry.location, where,
                                        std::format("case '{}' is listed twice",
                                                    entry.key));
                        }
                    }
                    auto name = scalar(entry.value, where, "a case");
                    if (!name) {
                        return std::unexpected(std::move(name.error()));
                    }
                    if (!isIdentifier(*name) ||
                        std::ranges::find(PRIMITIVES, *name,
                                          &Primitive::name) !=
                            PRIMITIVES.end()) {
                        return fail(entry.value.location(), where,
                                    std::format("case type '{}' must name a "
                                                "user type",
                                                *name));
                    }
                    switchCase.type = std::move(*name);
                    switchType.cases.push_back(std::move(switchCase));
                }

                attribute.kind = AttributeKind::SWITCH;
                attribute.switchOn = std::move(switchType);
                return {};
            }

            [[nodiscard]] std::expected<std::vector<std::byte>, LoadError>
            parseContents(const Node& node, const std::string& path) const {
                std::vector<std::byte> bytes;
                const auto addText = [&bytes](std::string_view text) {
                    for (const char c : text) {
                        bytes.push_back(static_cast<std::byte>(c));
                    }
                };

                if (node.isScalar()) {
                    addText(node.text());
                } else if (node.isList()) {
                    for (std::size_t i = 0; i < node.items().size(); ++i) {
                        const Node& item = node.items()[i];
                        if (!item.isScalar()) {
                            return fail(item.location(), at(path, i),
                                        "contents items must be numbers or "
                                        "text");
                        }
                        if (const auto value = parseUnsigned(item.text())) {
                            if (*value > 0xff) {
                                return fail(item.location(), at(path, i),
                                            std::format("{} does not fit in a "
                                                        "byte",
                                                        item.text()));
                            }
                            bytes.push_back(static_cast<std::byte>(*value));
                        } else {
                            addText(item.text());
                        }
                    }
                } else {
                    return fail(node.location(), path,
                                "'contents' must be text or a list");
                }

                if (bytes.empty()) {
                    return fail(node.location(), path, "'contents' is empty");
                }
                return bytes;
            }

            Status parseRepeat(Attribute& attribute, const Node& node,
                               const std::string& path) const {
                const Node* repeat = node.find("repeat");
                const Node* count = node.find("repeat-expr");
                if (repeat == nullptr) {
                    if (count != nullptr) {
                        return fail(locationOf(node, "repeat-expr"),
                                    join(path, "repeat-expr"),
                                    "'repeat-expr' needs 'repeat: expr'");
                    }
                    return {};
                }

                auto kind = scalar(*repeat, join(path, "repeat"), "'repeat'");
                if (!kind) {
                    return std::unexpected(std::move(kind.error()));
                }
                if (*kind == "eos" || *kind == "until") {
                    return fail(repeat->location(), join(path, "repeat"),
                                std::format("'repeat: {}' is not supported by "
                                            "shmscope yet; use 'repeat: expr'",
                                            *kind));
                }
                if (*kind != "expr") {
                    return fail(
                        repeat->location(), join(path, "repeat"),
                        std::format("'repeat' must be expr, got '{}'", *kind));
                }
                if (count == nullptr) {
                    return fail(repeat->location(), join(path, "repeat"),
                                "'repeat: expr' needs 'repeat-expr'");
                }
                auto parsed = expression(*count, join(path, "repeat-expr"),
                                         "'repeat-expr'");
                if (!parsed) {
                    return std::unexpected(std::move(parsed.error()));
                }
                attribute.repeatCount = std::move(*parsed);
                return {};
            }

            Status parseFormatReference(Attribute& attribute, const Node& node,
                                        const std::string& path) {
                auto name = scalar(node, path, "a format name");
                if (!name) {
                    return std::unexpected(std::move(name.error()));
                }
                if (attribute.kind != AttributeKind::SCALAR) {
                    return fail(node.location(), path,
                                std::format("formats only apply to number "
                                            "types (this attribute is {})",
                                            nameOf(attribute.kind)));
                }

                if (!layout_.formats.contains(*name)) {
                    if (!DefaultFormatters::knows(*name)) {
                        return fail(
                            node.location(), path,
                            std::format("unknown format '{}'; declare "
                                        "it under {}",
                                        *name, KsyDialect::FORMATS_KEY));
                    }
                    auto compiled =
                        DefaultFormatters::compile(FormatSpec{.kind = *name});
                    if (!compiled) {
                        return fail(node.location(), path,
                                    std::format("{}; declare it with its "
                                                "options under {}",
                                                compiled.error(),
                                                KsyDialect::FORMATS_KEY));
                    }
                    layout_.formats.emplace(*name, std::move(*compiled));
                }
                attribute.format = std::move(*name);
                return {};
            }

            [[nodiscard]] bool typeExists(const std::string_view name) const {
                return name == layout_.id || layout_.findType(name) != nullptr;
            }

            [[nodiscard]] Status checkReferences(
                const Type& type, const std::string& path) const {
                for (std::size_t i = 0; i < type.seq.size(); ++i) {
                    if (auto status = checkReference(type.seq[i],
                                                     at(join(path, "seq"), i));
                        !status) {
                        return status;
                    }
                }
                for (const auto& attribute : type.instances) {
                    if (auto status = checkReference(
                            attribute,
                            join(join(path, "instances"), attribute.id));
                        !status) {
                        return status;
                    }
                }
                return {};
            }

            [[nodiscard]] Status checkReference(const Attribute& attribute,
                                                const std::string& path) const {
                const auto where = join(path, "type");
                if (attribute.kind == AttributeKind::USER &&
                    !typeExists(attribute.userType)) {
                    return fail(
                        attribute.location, where,
                        std::format("unknown type '{}'", attribute.userType));
                }
                if (attribute.kind == AttributeKind::SWITCH) {
                    for (const auto& switchCase : attribute.switchOn->cases) {
                        if (!typeExists(switchCase.type)) {
                            return fail(switchCase.location,
                                        join(where, "cases"),
                                        std::format("unknown type '{}'",
                                                    switchCase.type));
                        }
                    }
                }
                return {};
            }

            [[nodiscard]] std::vector<std::byte> magicOf(
                const Type& type, const std::size_t depth) const {
                if (depth > MAX_MAGIC_DEPTH || type.seq.empty()) return {};

                const Attribute& first = type.seq.front();
                if (first.kind == AttributeKind::CONTENTS)
                    return first.contents;

                if (first.kind == AttributeKind::USER) {
                    if (const Type* next = layout_.findType(first.userType)) {
                        return magicOf(*next, depth + 1);
                    }
                }

                return {};
            }

            std::string_view source_{};
            Layout layout_{};
            bool littleEndian_ = false;
            std::string encoding_{};
        };
    }  // namespace

    bool KsyDialect::matches(const Node& root) {
        return root.isMap() &&
               (root.find("meta") != nullptr || root.find("seq") != nullptr);
    }

    LayoutResult KsyDialect::load(const Node& root, std::string_view source) {
        return Builder(source).build(root);
    }
}  // namespace shmscope
