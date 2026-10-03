#include "shmscope/layout/model.hpp"

#include <algorithm>

namespace shmscope {

    const Attribute* Type::find(const std::string_view id) const noexcept {
        for (const auto* list : {&seq, &instances}) {
            const auto it = std::ranges::find(*list, id, &Attribute::id);

            if (it != list->end()) return &*it;
        }

        return nullptr;
    }

    const Type* Layout::findType(const std::string_view name) const noexcept {
        const auto it = types.find(name);
        return it == types.end() ? nullptr : &it->second;
    }

    std::string_view nameOf(const AttributeKind kind) noexcept {
        switch (kind) {
            case AttributeKind::SCALAR:
                return "scalar";
            case AttributeKind::STRING:
                return "string";
            case AttributeKind::BYTES:
                return "bytes";
            case AttributeKind::CONTENTS:
                return "contents";
            case AttributeKind::USER:
                return "user type";
            case AttributeKind::SWITCH:
                return "switch";
        }
        return "?";
    }
}  // namespace shmscope
