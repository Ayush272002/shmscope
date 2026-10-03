#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "shmscope/core/decode.hpp"
#include "shmscope/core/default_formatters.hpp"
#include "shmscope/layout/document.hpp"

namespace shmscope {

    /// A value the layout computes from live bytes, kept unevaluated until
    /// the bytes are known, such as a size, count or offset.
    /// @param text Source text, e.g. "header.count * 8".
    /// @param location Where it was written in the layout file.
    struct Expression {
        std::string text{};
        Location location{};
    };

    /// One branch of a switch: which user type to use for a given value.
    /// @param value Value that selects this branch; empty for the default.
    /// @param type Name of the user type used when it matches.
    /// @param location Where the case was written.
    struct SwitchCase {
        std::optional<std::uint64_t> value{};
        std::string type{};
        Location location{};
    };

    /// An attribute whose type depends on a value read at runtime, such as a
    /// record kind field choosing between record layouts.
    /// @param on Expression whose value picks the case.
    /// @param cases Candidate types, in file order.
    struct SwitchType {
        Expression on{};
        std::vector<SwitchCase> cases{};
    };

    /// What an attribute holds, which decides which Attribute fields apply.
    /// - SCALAR: a number of a fixed storage type.
    /// - STRING: text of a given size.
    /// - BYTES: raw bytes of a given size, often padding.
    /// - CONTENTS: fixed bytes the data must match, such as a magic value.
    /// - USER: a nested named type.
    /// - SWITCH: a nested type chosen at runtime.
    enum class AttributeKind : std::uint8_t {
        SCALAR,
        STRING,
        BYTES,
        CONTENTS,
        USER,
        SWITCH,
    };

    /// One named piece of a type: a field in its sequence or an instance at
    /// a computed offset.
    /// @param id Name used in expressions and the viewer; empty for padding
    ///        and magic bytes.
    /// @param kind What the attribute holds.
    /// @param scalar Storage type, for SCALAR.
    /// @param userType Name of the nested type, for USER.
    /// @param switchOn Type selection rule, for SWITCH.
    /// @param size Length in bytes, for STRING, BYTES, and sized user types.
    /// @param contents Bytes that must appear here, for CONTENTS.
    /// @param repeatCount How many times it repeats; empty when it does not.
    /// @param pos Offset from the start of the mapping, for instances only.
    /// @param format Name of the display format in Layout::formats; empty for
    ///        plain decimal.
    /// @param location Where it was written in the layout file.
    struct Attribute {
        std::string id{};
        AttributeKind kind = AttributeKind::BYTES;
        FieldType scalar = FieldType::U8;
        std::string userType{};
        std::optional<SwitchType> switchOn{};
        std::optional<Expression> size{};
        std::vector<std::byte> contents{};
        std::optional<Expression> repeatCount{};
        std::optional<Expression> pos{};
        std::string format{};
        Location location{};
    };

    /// A struct-like block of memory, such as a header or one record.
    /// @param name Type name, used by USER and SWITCH attributes.
    /// @param seq Fields laid out back to back from the start of the type.
    /// @param instances Fields at explicit offsets, read only when needed.
    /// @param location Where the type was declared.
    struct Type {
        std::string name{};
        std::vector<Attribute> seq{};
        std::vector<Attribute> instances{};
        Location location{};

        /// Finds an attribute by id in seq, then in instances.
        /// @param id Attribute id to look up.
        /// @return The attribute, or nullptr if there is none.
        [[nodiscard]] const Attribute* find(std::string_view id) const noexcept;
    };

    /// A complete description of one shared memory format, loaded from a
    /// layout file and applied to live bytes by the viewer.
    /// @param id Layout identifier, also the name of the root type.
    /// @param title Human readable name; may be empty.
    /// @param root Type that starts at offset 0 of the mapping.
    /// @param types Named types referenced by attributes.
    /// @param formats Display formats, already compiled, by name.
    /// @param magic Bytes the mapping starts with, used to recognise the
    ///        format; empty if the layout declares none.
    struct Layout {
        std::string id{};
        std::string title{};
        Type root{};
        std::map<std::string, Type, std::less<>> types{};
        std::map<std::string, DefaultFormatters::Compiled, std::less<>>
            formats{};
        std::vector<std::byte> magic{};

        /// Finds a named type; the root type is not included.
        /// @param name Type name to look up.
        /// @return The type, or nullptr if there is none.
        [[nodiscard]] const Type* findType(
            std::string_view name) const noexcept;
    };

    /// A loaded layout, or the error that stopped it loading.
    using LayoutResult = std::expected<Layout, LoadError>;

    /// Short name of an attribute kind, for messages.
    /// @param kind Kind to name.
    /// @return e.g. "scalar" or "user type".
    [[nodiscard]] std::string_view nameOf(AttributeKind kind) noexcept;

}  // namespace shmscope
