#include "shmscope/layout/placement.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "shmscope/core/decode.hpp"
#include "shmscope/layout/evaluate.hpp"
#include "shmscope/layout/model.hpp"

namespace shmscope {

    const PlacedField* Placement::find(std::string_view path) const noexcept {
        const auto it = std::ranges::find(fields, path, &PlacedField::path);
        return it == fields.end() ? nullptr : &*it;
    }

    namespace {

        struct Handle {
            enum class Kind : std::uint8_t { SCOPE, FIELD };

            Kind kind = Kind::FIELD;
            std::size_t index = 0;
        };

        struct Scope {
            const Type* type = nullptr;
            std::string name{};
            std::string path{};
            std::optional<std::size_t> field{};
            std::optional<std::size_t> parent{};
            std::size_t streamStart = 0;
            std::size_t streamEnd = 0;
            std::size_t depth = 0;
            std::map<std::string, std::size_t, std::less<>> members{};
        };

        struct Slot {
            std::size_t scope = 0;
            const Attribute* attribute = nullptr;
            std::string path{};
            std::size_t offset = 0;
            std::optional<std::size_t> parent{};
            std::size_t depth = 0;
        };

        struct Placed {
            std::optional<std::size_t> field{};
            std::optional<std::size_t> end{};
        };

        using Number = std::expected<std::int64_t, std::string>;

        std::string join(const std::string_view path,
                         const std::string_view label) {
            if (path.empty()) return std::string(label);

            return std::format("{}.{}", path, label);
        }

        class Placer {
        public:
            Placer(const Layout& layout, const std::span<const std::byte> bytes,
                   const PlacementLimits& limits)
                : layout_(&layout), bytes_(bytes), limits_(limits) {}

            Placement run() {
                scopes_.push_back(Scope{.type = &layout_->root,
                                        .name = layout_->id,
                                        .streamEnd = bytes_.size()});
                static_cast<void>(placeType(0, 0));
                return std::move(placement_);
            }

        private:
            class ScopeResolver {
            public:
                using Ref = Handle;

                ScopeResolver(Placer& placer, const std::size_t scope)
                    : placer_(&placer), scope_(scope) {}

                Lookup<Ref> name(const std::string_view text) {
                    return placer_->name(scope_, text);
                }

                Lookup<Ref> member(const Ref& ref,
                                   const std::string_view text) {
                    return placer_->member(ref, text);
                }

                Lookup<Ref> element(const Ref& ref, const std::int64_t index) {
                    return placer_->element(ref, index);
                }

                Number number(const Ref& ref) { return placer_->number(ref); }

            private:
                Placer* placer_;
                std::size_t scope_;
            };

            Lookup<Handle> name(const std::size_t scope,
                                const std::string_view text) {
                if (text == "_root") {
                    return Handle{.kind = Handle::Kind::SCOPE, .index = 0};
                }
                if (text == "_parent") {
                    const auto parent = scopes_[scope].parent;
                    if (!parent) {
                        return std::unexpected(std::format(
                            "'{}' has no '_parent'", scopeName(scope)));
                    }
                    return Handle{.kind = Handle::Kind::SCOPE,
                                  .index = *parent};
                }
                if (text == "_index") {
                    if (indexes_.empty()) {
                        return std::unexpected(std::string(
                            "'_index' is only available inside a repeat"));
                    }
                    return indexes_.back();
                }
                if (text.starts_with('_')) {
                    return std::unexpected(std::format(
                        "'{}' is not supported by shmscope yet", text));
                }
                return memberOf(scope, text);
            }

            Lookup<Handle> member(const Handle& ref,
                                  const std::string_view text) {
                if (ref.kind == Handle::Kind::SCOPE) {
                    return memberOf(ref.index, text);
                }
                if (const auto inner = fieldScopes_.find(ref.index);
                    inner != fieldScopes_.end()) {
                    return memberOf(inner->second, text);
                }
                return std::unexpected(
                    std::format("'{}' has no fields", pathOf(ref)));
            }

            Lookup<Handle> element(const Handle& ref,
                                   const std::int64_t index) {
                const auto items = ref.kind == Handle::Kind::FIELD
                                       ? elements_.find(ref.index)
                                       : elements_.end();
                if (items == elements_.end()) {
                    return std::unexpected(
                        std::format("'{}' is not a list", pathOf(ref)));
                }
                const auto& list = items->second;
                if (index < 0 ||
                    static_cast<std::uint64_t>(index) >= list.size()) {
                    return std::unexpected(std::format(
                        "index {} is out of range; '{}' has {} elements", index,
                        pathOf(ref), list.size()));
                }
                return Handle{.kind = Handle::Kind::FIELD,
                              .index = list[static_cast<std::size_t>(index)]};
            }

            Number number(const Handle& ref) {
                if (ref.kind == Handle::Kind::SCOPE) {
                    return std::unexpected(std::format(
                        "'{}' is a type, not a number", pathOf(ref)));
                }
                if (elements_.contains(ref.index)) {
                    return std::unexpected(std::format(
                        "'{}' is a list, not a number", pathOf(ref)));
                }
                const auto& field = placement_.fields[ref.index];
                if (!field.value) {
                    return std::unexpected(
                        std::format("'{}' is not a number", field.path));
                }
                return std::visit(
                    [&]<typename T>(const T& data) -> Number {
                        if constexpr (std::is_same_v<T, std::uint64_t>) {
                            if (data >
                                static_cast<std::uint64_t>(
                                    std::numeric_limits<std::int64_t>::max())) {
                                return std::unexpected(
                                    std::format("'{}' is {}, which does not "
                                                "fit in a signed "
                                                "64 bit integer",
                                                field.path, data));
                            }
                            return static_cast<std::int64_t>(data);
                        } else if constexpr (std::is_same_v<T, std::int64_t>) {
                            return data;
                        } else if constexpr (std::is_same_v<T, double>) {
                            return std::unexpected(std::format(
                                "'{}' is a floating point number; expressions "
                                "only use integers",
                                field.path));
                        } else {
                            return std::unexpected(std::format(
                                "'{}' is a string, not a number", field.path));
                        }
                    },
                    field.value->data);
            }

            [[nodiscard]] bool stopped() const noexcept {
                return placement_.truncated;
            }

            [[nodiscard]] std::string scopeName(const std::size_t scope) const {
                return scopes_[scope].path.empty() ? std::string("_root")
                                                   : scopes_[scope].path;
            }

            [[nodiscard]] std::string pathOf(const Handle& ref) const {
                return ref.kind == Handle::Kind::SCOPE
                           ? scopeName(ref.index)
                           : placement_.fields[ref.index].path;
            }

            void problem(std::string path, const Location location,
                         std::string message) {
                placement_.problems.push_back(
                    PlacementProblem{.path = std::move(path),
                                     .message = std::move(message),
                                     .location = location});
            }

            Lookup<Handle> memberOf(const std::size_t scope,
                                    const std::string_view text) {
                const auto& members = scopes_[scope].members;
                if (const auto found = members.find(text);
                    found != members.end()) {
                    return Handle{.kind = Handle::Kind::FIELD,
                                  .index = found->second};
                }
                const Attribute* attribute = scopes_[scope].type->find(text);
                if (attribute == nullptr) {
                    return std::unexpected(std::format(
                        "'{}' has no field '{}'", scopes_[scope].name, text));
                }
                if (!attribute->pos) {
                    return std::unexpected(
                        std::format("'{}' is not placed yet; a field can only "
                                    "use the fields before it",
                                    text));
                }
                if (active_.contains({scope, attribute})) {
                    return std::unexpected(
                        std::format("'{}' depends on itself", text));
                }
                if (!placeInstance(scope, *attribute)) {
                    return std::unexpected(
                        std::format("'{}' could not be placed", text));
                }
                return memberOf(scope, text);
            }

            std::optional<std::int64_t> evaluateIn(const std::size_t scope,
                                                   const Expression& expression,
                                                   const std::string_view key,
                                                   const std::string& path) {
                ScopeResolver resolver(*this, scope);
                const auto result = evaluate(expression.program, resolver);
                if (result) return *result;

                problem(path, expression.location,
                        std::format("{} \"{}\": {}", key, expression.text,
                                    result.error().message));
                return std::nullopt;
            }

            std::optional<std::size_t> placeType(const std::size_t scope,
                                                 const std::size_t start) {
                const Type& type = *scopes_[scope].type;
                std::size_t cursor = start;
                bool complete = true;

                for (std::size_t i = 0; i < type.seq.size() && complete; ++i) {
                    if (stopped()) return std::nullopt;

                    const auto& attribute = type.seq[i];
                    const auto label = attribute.id.empty()
                                           ? std::format("_unnamed{}", i)
                                           : attribute.id;
                    const auto end =
                        placeAttribute(scope, attribute, label, cursor);
                    if (end) {
                        cursor = *end;
                    } else {
                        complete = false;
                    }
                }

                for (const auto& attribute : type.instances) {
                    if (stopped()) return std::nullopt;

                    static_cast<void>(placeInstance(scope, attribute));
                }

                if (!complete) return std::nullopt;

                return cursor - start;
            }

            bool placeInstance(const std::size_t scope,
                               const Attribute& attribute) {
                if (!attempted_.insert({scope, &attribute}).second) {
                    return scopes_[scope].members.contains(attribute.id);
                }
                active_.insert({scope, &attribute});
                placeAt(scope, attribute);
                active_.erase({scope, &attribute});
                return scopes_[scope].members.contains(attribute.id);
            }

            void placeAt(const std::size_t scope, const Attribute& attribute) {
                if (!attribute.pos) return;

                const auto path = join(scopes_[scope].path, attribute.id);
                const auto pos = evaluateIn(scope, *attribute.pos, "pos", path);
                if (!pos) return;

                const auto available =
                    scopes_[scope].streamEnd - scopes_[scope].streamStart;
                if (*pos < 0 || static_cast<std::uint64_t>(*pos) > available) {
                    problem(
                        path, attribute.pos->location,
                        std::format("pos {} is outside the {} bytes available",
                                    *pos, available));
                    return;
                }
                static_cast<void>(
                    placeAttribute(scope, attribute, attribute.id,
                                   scopes_[scope].streamStart +
                                       static_cast<std::size_t>(*pos)));
            }

            std::optional<std::size_t> placeAttribute(
                const std::size_t scope, const Attribute& attribute,
                const std::string& label, const std::size_t offset) {
                const Slot slot{.scope = scope,
                                .attribute = &attribute,
                                .path = join(scopes_[scope].path, label),
                                .offset = offset,
                                .parent = scopes_[scope].field,
                                .depth = scopes_[scope].depth};

                if (attribute.repeatCount) return placeRepeat(slot);

                const auto placed = placeOne(slot);
                remember(slot, placed.field);
                return placed.end;
            }

            void remember(const Slot& slot,
                          const std::optional<std::size_t> field) {
                if (!field || slot.attribute->id.empty()) return;

                scopes_[slot.scope].members.insert_or_assign(slot.attribute->id,
                                                             *field);
            }

            std::optional<std::size_t> placeRepeat(const Slot& slot) {
                const auto& attribute = *slot.attribute;
                const auto count =
                    evaluateIn(slot.scope, *attribute.repeatCount,
                               "repeat-expr", slot.path);
                if (!count) return std::nullopt;

                if (*count < 0 ||
                    static_cast<std::uint64_t>(*count) > limits_.maxRepeat) {
                    problem(slot.path, attribute.repeatCount->location,
                            std::format("repeat-expr {} is outside 0 to {}",
                                        *count, limits_.maxRepeat));
                    return std::nullopt;
                }

                const auto array = addField(slot);
                if (!array) return std::nullopt;

                remember(slot, array);
                elements_[*array];

                std::size_t cursor = slot.offset;
                bool complete = true;
                for (std::int64_t i = 0; i < *count && complete; ++i) {
                    const Slot item{.scope = slot.scope,
                                    .attribute = slot.attribute,
                                    .path = std::format("{}[{}]", slot.path, i),
                                    .offset = cursor,
                                    .parent = array,
                                    .depth = slot.depth + 1};
                    indexes_.push_back(i);
                    const auto placed = placeOne(item);
                    indexes_.pop_back();

                    if (placed.field)
                        elements_[*array].push_back(*placed.field);
                    if (placed.end) {
                        cursor = *placed.end;
                    } else {
                        complete = false;
                    }
                }

                placement_.fields[*array].size = cursor - slot.offset;
                if (!complete) return std::nullopt;

                return cursor;
            }

            Placed placeOne(const Slot& slot) {
                const auto& attribute = *slot.attribute;
                std::optional<std::size_t> size{};

                if (attribute.size) {
                    const auto value = evaluateIn(slot.scope, *attribute.size,
                                                  "size", slot.path);
                    if (!value) return {};

                    if (*value < 0) {
                        problem(slot.path, attribute.size->location,
                                std::format("size {} is negative", *value));
                        return {};
                    }
                    size = static_cast<std::size_t>(*value);
                }

                switch (attribute.kind) {
                    case AttributeKind::SCALAR:
                        return placeLeaf(slot, widthOf(attribute.scalar));
                    case AttributeKind::STRING:
                    case AttributeKind::BYTES:
                        return placeLeaf(slot, size.value_or(0));
                    case AttributeKind::CONTENTS:
                        return placeLeaf(slot, attribute.contents.size());
                    case AttributeKind::USER:
                        return placeUser(slot, attribute.userType, size);
                    case AttributeKind::SWITCH:
                        return placeSwitch(slot, size);
                }
                return {};
            }

            Placed placeLeaf(const Slot& slot, const std::size_t length) {
                if (!fits(slot, length)) return {};

                const auto field = addField(slot);
                if (!field) return {};

                placement_.fields[*field].size = length;
                placement_.fields[*field].value =
                    valueAt(*slot.attribute, slot.offset, length);
                checkContents(slot);
                return {.field = field, .end = slot.offset + length};
            }

            Placed placeUser(const Slot& slot, const std::string& typeName,
                             const std::optional<std::size_t> size) {
                const Type* type = layout_->findType(typeName);
                if (type == nullptr) {
                    problem(slot.path, slot.attribute->location,
                            std::format("type '{}' does not exist", typeName));
                    return {};
                }
                if (slot.depth + 1 > limits_.maxDepth) {
                    problem(slot.path, slot.attribute->location,
                            std::format("types are nested more than {} deep",
                                        limits_.maxDepth));
                    return {};
                }
                if (size && !fits(slot, *size)) return {};

                const auto field = addField(slot);
                if (!field) return {};

                const auto& outer = scopes_[slot.scope];
                Scope inner{
                    .type = type,
                    .name = typeName,
                    .path = slot.path,
                    .field = field,
                    .parent = slot.scope,
                    .streamStart = size ? slot.offset : outer.streamStart,
                    .streamEnd = size ? slot.offset + *size : outer.streamEnd,
                    .depth = slot.depth + 1};
                scopes_.push_back(std::move(inner));

                const auto scope = scopes_.size() - 1;
                fieldScopes_.emplace(*field, scope);

                const auto used = placeType(scope, slot.offset);
                const auto length = size ? size : used;
                if (!length) return {.field = field};

                placement_.fields[*field].size = *length;
                return {.field = field, .end = slot.offset + *length};
            }

            Placed placeSwitch(const Slot& slot,
                               const std::optional<std::size_t> size) {
                const auto& rule = *slot.attribute->switchOn;
                const auto on =
                    evaluateIn(slot.scope, rule.on, "switch-on", slot.path);
                if (!on) return {};

                const SwitchCase* fallback = nullptr;
                for (const auto& option : rule.cases) {
                    if (!option.value) {
                        fallback = &option;
                    } else if (*option.value ==
                               static_cast<std::uint64_t>(*on)) {
                        return placeUser(slot, option.type, size);
                    }
                }

                if (fallback != nullptr) {
                    return placeUser(slot, fallback->type, size);
                }
                if (size) return placeLeaf(slot, *size);

                problem(
                    slot.path, rule.on.location,
                    std::format("no case matches {} and there is no '_' case",
                                *on));
                return {};
            }

            bool fits(const Slot& slot, const std::size_t length) {
                const auto end = scopes_[slot.scope].streamEnd;
                if (slot.offset <= end && length <= end - slot.offset) {
                    return true;
                }
                problem(
                    slot.path, slot.attribute->location,
                    std::format("{} bytes at offset {} go past the end at {}",
                                length, slot.offset, end));
                return false;
            }

            std::optional<std::size_t> addField(const Slot& slot) {
                if (placement_.fields.size() >= limits_.maxFields) {
                    placement_.truncated = true;
                    return std::nullopt;
                }
                placement_.fields.push_back(
                    PlacedField{.path = slot.path,
                                .attribute = slot.attribute,
                                .offset = slot.offset,
                                .depth = slot.depth,
                                .parent = slot.parent});
                return placement_.fields.size() - 1;
            }

            [[nodiscard]] std::optional<Value> valueAt(
                const Attribute& attribute, const std::size_t offset,
                const std::size_t length) const {
                if (attribute.kind == AttributeKind::SCALAR) {
                    return read(attribute.scalar, bytes_, offset);
                }
                if (attribute.kind != AttributeKind::STRING)
                    return std::nullopt;

                std::string text;
                for (const std::byte b : bytes_.subspan(offset, length)) {
                    const auto c = std::to_integer<unsigned char>(b);
                    if (c == 0) break;

                    text += c >= 0x20 && c < 0x7f ? static_cast<char>(c) : '.';
                }
                return Value{.data = std::move(text), .width = length};
            }

            void checkContents(const Slot& slot) {
                if (slot.attribute->kind != AttributeKind::CONTENTS) return;

                const auto& wanted = slot.attribute->contents;
                if (!std::ranges::equal(
                        bytes_.subspan(slot.offset, wanted.size()), wanted)) {
                    problem(slot.path, slot.attribute->location,
                            "the bytes do not match the expected contents");
                }
            }

            const Layout* layout_;
            std::span<const std::byte> bytes_;
            PlacementLimits limits_;
            Placement placement_{};
            std::vector<Scope> scopes_{};
            std::map<std::size_t, std::size_t> fieldScopes_{};
            std::map<std::size_t, std::vector<std::size_t>> elements_{};
            std::vector<std::int64_t> indexes_{};
            std::set<std::pair<std::size_t, const Attribute*>> attempted_{};
            std::set<std::pair<std::size_t, const Attribute*>> active_{};
        };

    }  // namespace

    Placement place(const Layout& layout,
                    const std::span<const std::byte> bytes,
                    const PlacementLimits& limits) {
        return Placer(layout, bytes, limits).run();
    }

}  // namespace shmscope
